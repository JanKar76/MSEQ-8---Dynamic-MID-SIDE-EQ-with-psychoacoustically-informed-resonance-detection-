#include "PluginEditor.h"

//==============================================================================
MSEQ8AudioProcessorEditor::MSEQ8AudioProcessorEditor (MSEQ8AudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), graph (p),
      monitorSelector (p.apvts, "monitor",
                       juce::StringArray { "ST", "M", "S" },
                       { Theme::midSide, Theme::mid, Theme::side })
{
    // Instance-bound LookAndFeel (NOT juce::LookAndFeel::setDefaultLookAndFeel,
    // which is a process-global pointer shared by ALL plugin instances in the
    // same host process). With multiple instances open at once (e.g. two
    // MSEQ 8 instances in FL Studio), each instance would overwrite the same
    // global default in its constructor and null it in its destructor - that
    // could leave other instances' still-open windows without a valid
    // LookAndFeel and caused extreme CPU load/hangs as soon as more than one
    // instance was open. setLookAndFeel() on the editor itself is inherited
    // by all child components that don't have their own explicit one set
    // (PopupMenu/AlertWindow/CallOutBox are standalone top-level windows and
    // get their own explicit instance, see EQGraphComponent and
    // promptSavePreset()).
    setLookAndFeel (&knobLnf);

    addAndMakeVisible (graph);

    for (int i = 0; i < MSEQ8AudioProcessor::numBands; ++i)
        addAndMakeVisible (bandColumns.add (new BandColumn (processor, i, knobLnf)));

    // Highlight the band column when its node is hovered in the graph
    graph.onBandHover = [this] (int band)
    {
        for (int i = 0; i < bandColumns.size(); ++i)
            bandColumns[i]->setHighlighted (i == band);
    };

    // Preset dropdown (factory + user presets + "Save preset...")
    rebuildPresetBox();
    presetBox.setSelectedId (1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id == 0)
            return;

        if (id == savePresetItemId)
        {
            presetBox.setSelectedId (lastPresetId, juce::dontSendNotification);
            promptSavePreset();
            return;
        }

        lastPresetId = id;

        if (id >= userPresetBaseId)
        {
            const int index = id - userPresetBaseId;
            if (index < userPresetFiles.size())
                processor.loadUserPreset (userPresetFiles[index]);
        }
        else
        {
            processor.applyPreset (id - 1);
        }
    };
    addAndMakeVisible (presetBox);

    // A/B/C/D snapshots
    for (int s = 0; s < MSEQ8AudioProcessor::numSlots; ++s)
    {
        slotButtons[s].setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + s)));
        slotButtons[s].onClick = [this, s] { processor.switchToSlot (s); updateSlotButtons(); };
        addAndMakeVisible (slotButtons[s]);
    }
    updateSlotButtons();

    // Monitor: Stereo / Mid solo / Side solo
    addAndMakeVisible (monitorSelector);

    // Delta: listen to what's being removed (dry - wet)
    deltaButton.setClickingTogglesState (true);
    deltaButton.setColour (juce::TextButton::buttonColourId, Theme::panelLight);
    deltaButton.setColour (juce::TextButton::buttonOnColourId, Theme::side);
    deltaButton.setColour (juce::TextButton::textColourOffId, Theme::textDim);
    deltaButton.setColour (juce::TextButton::textColourOnId, Theme::background);
    deltaButton.setTooltip ("Delta: listen to what is being removed");
    addAndMakeVisible (deltaButton);
    deltaAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "delta", deltaButton);

    // Global bypass: clear on/off colours
    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonColourId, Theme::panelLight);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, Theme::side);
    bypassButton.setColour (juce::TextButton::textColourOffId, Theme::textDim);
    bypassButton.setColour (juce::TextButton::textColourOnId, Theme::background);
    addAndMakeVisible (bypassButton);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "global_bypass", bypassButton);

    // Output gain
    outGainKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    outGainKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    outGainKnob.setLookAndFeel (&knobLnf);
    addAndMakeVisible (outGainKnob);
    outGainAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "output_gain", outGainKnob);

    // Meters (to the right of the graph)
    addAndMakeVisible (meterPanel);

    // Resizable window; the latest size is saved in the plugin state.
    // Minimum width raised 900 -> 1020: below ~980 px the preset/A-B-C-D
    // block in the middle collides with the monitor/delta/gain/bypass block
    // on the right. The jlimit floor below matches this, so an old saved
    // (narrower) uiWidth from a previous version gets clamped up on load
    // instead of recreating the overlap.
    setResizable (true, true);
    setResizeLimits (1020, 550, 1800, 1100);

    const int w = (int) processor.apvts.state.getProperty ("uiWidth", 1180);
    const int h = (int) processor.apvts.state.getProperty ("uiHeight", 700);
    setSize (juce::jlimit (1020, 1800, w), juce::jlimit (550, 1100, h));

    // Ctrl+Z/Ctrl+Y (undo/redo) - requires the editor to actually be able to
    // receive key presses. grabKeyboardFocus() makes it work immediately
    // when the window opens, not only after the first click into it.
    setWantsKeyboardFocus (true);
    grabKeyboardFocus();

    lastSeenGeneration = processor.paramChangeGeneration.load (std::memory_order_relaxed);

    startTimerHz (30);
}

MSEQ8AudioProcessorEditor::~MSEQ8AudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    outGainKnob.setLookAndFeel (nullptr);
}

//==============================================================================
void MSEQ8AudioProcessorEditor::timerCallback()
{
    meterPanel.inMeter.setLevel (processor.inLevel.load());
    meterPanel.outMeter.setLevel (processor.outLevel.load());

    // Undo/redo debounce (see PluginProcessor.h for the reasoning): a
    // continuous knob/node drag generates many parameterChanged calls in a
    // row - wait until it's been quiet for ~400 ms before committing ONE
    // snapshot, instead of one per mouse movement.
    const auto gen = processor.paramChangeGeneration.load (std::memory_order_relaxed);
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (gen != lastSeenGeneration)
    {
        lastSeenGeneration = gen;
        lastParamChangeMs = now;
        pendingUndoCommit = true;
    }
    else if (pendingUndoCommit && now - lastParamChangeMs >= 400.0)
    {
        processor.pushUndoSnapshot();
        pendingUndoCommit = false;
    }
}

bool MSEQ8AudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();

    // Ctrl+Z (Windows/Linux) resp. Cmd+Z (Mac, via isCommandDown()) - with
    // Shift = redo, otherwise undo. Ctrl+Y is also supported for redo
    // (a common Windows convention in addition to Ctrl+Shift+Z).
    const bool isZ = key.getKeyCode() == 'Z';
    const bool isY = key.getKeyCode() == 'Y';

    if (mods.isCommandDown() && (isZ || isY))
    {
        // Commit any in-progress (but not yet debounce-committed) change
        // FIRST, so the last adjustment made right before pressing undo
        // isn't lost.
        if (pendingUndoCommit)
        {
            processor.pushUndoSnapshot();
            pendingUndoCommit = false;
        }

        if (isZ && mods.isShiftDown())
            processor.redo();
        else if (isZ)
            processor.undo();
        else
            processor.redo();

        // undo()/redo() triggers its own flurry of parameterChanged calls
        // (via replaceState) - sync those away immediately so the debounce
        // logic above doesn't mistake it for a new user change and commit an
        // extra, redundant snapshot 400 ms later.
        lastSeenGeneration = processor.paramChangeGeneration.load (std::memory_order_relaxed);
        pendingUndoCommit = false;

        repaint();
        return true;
    }

    return false;
}

void MSEQ8AudioProcessorEditor::updateSlotButtons()
{
    const int active = processor.getActiveSlot();

    for (int s = 0; s < MSEQ8AudioProcessor::numSlots; ++s)
    {
        slotButtons[s].setColour (juce::TextButton::buttonColourId,
                                  s == active ? Theme::mid.withAlpha (0.8f) : Theme::panelLight);
        slotButtons[s].setColour (juce::TextButton::textColourOffId,
                                  s == active ? Theme::background : Theme::textDim);
    }
}

//==============================================================================
void MSEQ8AudioProcessorEditor::rebuildPresetBox()
{
    presetBox.clear (juce::dontSendNotification);
    presetBox.addItemList (processor.getPresetNames(), 1);

    userPresetFiles = processor.getUserPresetFiles();
    if (! userPresetFiles.isEmpty())
    {
        presetBox.addSeparator();
        for (int i = 0; i < userPresetFiles.size(); ++i)
            presetBox.addItem (userPresetFiles[i].getFileNameWithoutExtension(),
                               userPresetBaseId + i);
    }

    presetBox.addSeparator();
    presetBox.addItem ("Save preset...", savePresetItemId);
}

void MSEQ8AudioProcessorEditor::promptSavePreset()
{
    auto* aw = new juce::AlertWindow ("Save preset",
                                      "Preset name:",
                                      juce::MessageBoxIconType::NoIcon,
                                      this);
    // AlertWindow is its own top-level window, not a child in the component
    // tree, so it doesn't automatically inherit the editor's setLookAndFeel()
    // - must be set explicitly (same reason knobLnf is no longer a global
    // default).
    aw->setLookAndFeel (&knobLnf);
    aw->addTextEditor ("name", "", "");
    aw->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MSEQ8AudioProcessorEditor> safe (this);

    aw->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe, aw] (int result)
        {
            const auto name = aw->getTextEditorContents ("name").trim();

            if (safe != nullptr && result == 1 && name.isNotEmpty())
            {
                safe->processor.saveUserPreset (name);
                safe->rebuildPresetBox();

                // Select the newly saved preset
                for (int i = 0; i < safe->userPresetFiles.size(); ++i)
                {
                    if (safe->userPresetFiles[i].getFileNameWithoutExtension()
                            == juce::File::createLegalFileName (name))
                    {
                        safe->lastPresetId = userPresetBaseId + i;
                        safe->presetBox.setSelectedId (safe->lastPresetId,
                                                       juce::dontSendNotification);
                        break;
                    }
                }
            }
        }), true);   // deleteWhenDismissed
}

//==============================================================================
void MSEQ8AudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // Logo diamond
    juce::Path diamond;
    diamond.addQuadrilateral (40.0f, 16.0f, 56.0f, 28.0f, 40.0f, 40.0f, 24.0f, 28.0f);
    g.setColour (Theme::mid);
    g.fillPath (diamond);

    g.setColour (Theme::text);
    g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    g.drawText ("MSEQ 8", 66, 10, 200, 22, juce::Justification::left);

    g.setColour (Theme::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("8-BAND MID/SIDE EQUALIZER", 66, 32, 250, 14, juce::Justification::left);

    g.drawText ("MONITOR", getWidth() - 336, 8, 104, 12, juce::Justification::centred);
    g.drawText ("DELTA", getWidth() - 226, 8, 38, 12, juce::Justification::centred);
    g.drawText ("GAIN", getWidth() - 178, 8, 40, 12, juce::Justification::centred);

    // Footer text
    g.drawText ("Drag nodes = freq/gain  -  scroll node = Q  -  right-click band = type + dynamics"
                "  -  scroll dB axis = zoom  -  double-click knob = type value"
                "  -  HP/LP: right-click = menu  -  legend: spectra, RES, speed, FREEZE",
                12, getHeight() - 20, getWidth() - 24, 14, juce::Justification::left);
}

void MSEQ8AudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Header
    auto header = area.removeFromTop (56);
    presetBox.setBounds (header.getCentreX() - 180, 14, 170, 26);

    for (int s = 0; s < MSEQ8AudioProcessor::numSlots; ++s)
        slotButtons[s].setBounds (header.getCentreX() + 2 + s * 38, 14, 34, 26);

    monitorSelector.setBounds (getWidth() - 336, 22, 104, 22);
    deltaButton.setBounds (getWidth() - 222, 20, 30, 26);
    outGainKnob.setBounds (getWidth() - 176, 20, 36, 32);
    bypassButton.setBounds (getWidth() - 130, 14, 56, 26);

    area.removeFromBottom (24);   // footer text

    // Band columns
    auto bandArea = area.removeFromBottom (170).reduced (12, 6);
    const int colW = bandArea.getWidth() / MSEQ8AudioProcessor::numBands;
    for (int i = 0; i < bandColumns.size(); ++i)
        bandColumns[i]->setBounds (bandArea.withX (bandArea.getX() + i * colW)
                                           .withWidth (colW).reduced (4, 0));

    // Meter panel to the right of the graph
    auto meterStrip = area.removeFromRight (88);
    meterPanel.setBounds (meterStrip.reduced (0, 6));

    // Graph
    graph.setBounds (area.reduced (12, 6));

    // Remember the size
    processor.apvts.state.setProperty ("uiWidth",  getWidth(),  nullptr);
    processor.apvts.state.setProperty ("uiHeight", getHeight(), nullptr);
}

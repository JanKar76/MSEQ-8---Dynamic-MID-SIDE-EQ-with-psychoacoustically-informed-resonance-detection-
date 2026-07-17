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

    // Preset button (factory presets grouped by genre + user presets + "Save preset...")
    userPresetFiles = processor.getUserPresetFiles();
    presetButton.setButtonText ("Default");
    presetButton.setColour (juce::TextButton::buttonColourId, Theme::panelLight);
    presetButton.setColour (juce::TextButton::textColourOffId, Theme::text);
    presetButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetButton);
    lastPresetId = 1;

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

    // Match Gain: one-shot action, auto-sets output gain so pre/post-EQ
    // loudness match (~2 s running average, see MSEQ8AudioProcessor::
    // matchGain()) - makes A/B comparisons fair without a boost/cut skewing
    // perceived loudness.
    matchGainButton.setColour (juce::TextButton::buttonColourId, Theme::panelLight);
    matchGainButton.setColour (juce::TextButton::textColourOffId, Theme::text);
    matchGainButton.setTooltip ("Set output gain so the processed signal matches the input's loudness");
    matchGainButton.onClick = [this] { processor.matchGain(); };
    addAndMakeVisible (matchGainButton);

    // Meters (to the right of the graph)
    addAndMakeVisible (meterPanel);

    // Resizable window; the latest size is saved in the plugin state.
    // Minimum width: 900 -> 1020 -> 1140 -> 1080. The header cluster's
    // previous center/right-anchored layout meant every control added to
    // the right-hand block (most recently MATCH) required raising this
    // floor again, since the gap between the two anchored halves shrinks
    // faster than the floor grows. Switching resized() to a single fixed,
    // left-anchored cluster (see there) removed that dependency entirely -
    // the cluster's own content now ends at x=1052, so 1080 is a fixed,
    // comfortable floor that no longer needs to grow when new header
    // controls are added. The jlimit floor below matches this, so an old
    // saved (wider or narrower) uiWidth from a previous version is clamped
    // to a sane range on load.
    setResizable (true, true);
    setResizeLimits (1080, 550, 1800, 1100);

    const int w = (int) processor.apvts.state.getProperty ("uiWidth", 1180);
    const int h = (int) processor.apvts.state.getProperty ("uiHeight", 700);
    setSize (juce::jlimit (1080, 1800, w), juce::jlimit (550, 1100, h));

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
    meterPanel.corrMeter.setValue (processor.correlation.load());

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
// Two-level preset menu: one PopupMenu submenu per distinct genre, in the
// order returned by getPresetList() (which must stay in sync with the
// applyPreset() switch in PluginProcessor.cpp). "Default" has no genre and
// sits at the top level, above the submenus.
void MSEQ8AudioProcessorEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&knobLnf);

    const auto& list = MSEQ8AudioProcessor::getPresetList();
    int i = 0;
    while (i < (int) list.size())
    {
        const auto& p = list[(size_t) i];
        if (p.genre.isEmpty())
        {
            menu.addItem (i + 1, p.name, true, i + 1 == lastPresetId);
            ++i;
            continue;
        }

        juce::PopupMenu sub;
        const juce::String genre = p.genre;
        while (i < (int) list.size() && list[(size_t) i].genre == genre)
        {
            sub.addItem (i + 1, list[(size_t) i].name, true, i + 1 == lastPresetId);
            ++i;
        }
        menu.addSubMenu (genre, sub);
    }

    if (! userPresetFiles.isEmpty())
    {
        menu.addSeparator();
        juce::PopupMenu userMenu;
        for (int u = 0; u < userPresetFiles.size(); ++u)
            userMenu.addItem (userPresetBaseId + u, userPresetFiles[u].getFileNameWithoutExtension(),
                              true, userPresetBaseId + u == lastPresetId);
        menu.addSubMenu ("User Presets", userMenu);
    }

    menu.addSeparator();
    menu.addItem (savePresetItemId, "Save preset...");

    juce::Component::SafePointer<MSEQ8AudioProcessorEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
        [safe] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            if (result == savePresetItemId)
            {
                safe->promptSavePreset();
                return;
            }

            safe->lastPresetId = result;

            if (result >= userPresetBaseId)
            {
                const int index = result - userPresetBaseId;
                if (index < safe->userPresetFiles.size())
                {
                    safe->processor.loadUserPreset (safe->userPresetFiles[index]);
                    safe->presetButton.setButtonText (safe->userPresetFiles[index].getFileNameWithoutExtension());
                }
            }
            else
            {
                const auto& lst = MSEQ8AudioProcessor::getPresetList();
                const int idx = result - 1;
                if (idx >= 0 && idx < (int) lst.size())
                {
                    safe->processor.applyPreset (idx);
                    safe->presetButton.setButtonText (lst[(size_t) idx].name);
                }
            }
        });
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
                safe->userPresetFiles = safe->processor.getUserPresetFiles();

                // Select the newly saved preset
                for (int i = 0; i < safe->userPresetFiles.size(); ++i)
                {
                    if (safe->userPresetFiles[i].getFileNameWithoutExtension()
                            == juce::File::createLegalFileName (name))
                    {
                        safe->lastPresetId = userPresetBaseId + i;
                        safe->presetButton.setButtonText (
                            safe->userPresetFiles[i].getFileNameWithoutExtension());
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

    // Header control cluster labels: positions track headerClusterX (set in
    // resized(), measured from the subtitle text width - see there), not
    // window-width-dependent.
    g.drawText ("MONITOR", headerClusterX + 350, 8, 104, 12, juce::Justification::centred);
    g.drawText ("DELTA", headerClusterX + 470, 8, 38, 12, juce::Justification::centred);
    g.drawText ("GAIN", headerClusterX + 522, 8, 40, 12, juce::Justification::centred);

    // Footer text
    g.drawText ("Drag nodes = freq/gain  -  scroll node = Q  -  right-click band = type + dynamics"
                "  -  scroll dB axis = zoom  -  double-click knob = type value"
                "  -  HP/LP: right-click = menu  -  legend: spectra, RES, speed, FREEZE",
                12, getHeight() - 20, getWidth() - 24, 14, juce::Justification::left);
}

void MSEQ8AudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Header: a single compact, left-anchored control cluster (preset ->
    // A/B/C/D -> monitor -> delta -> gain -> match -> bypass), placed right
    // after the logo instead of split between window-centre anchoring
    // (preset/A-B-C-D) and window-right anchoring (monitor onward). Fixed
    // pixel positions (relative to headerClusterX) rather than
    // getWidth()-relative ones - the previous split scheme left a growing
    // dead gap in the middle as the window widened, and required repeatedly
    // raising the minimum width every time a control was added to the
    // right-hand block. A fixed, tightly packed cluster is narrower
    // overall, doesn't depend on window width at all, and stays usable on
    // small screens.
    area.removeFromTop (56);

    // headerClusterX = right after the actual "8-BAND MID/SIDE EQUALIZER"
    // subtitle text (drawn at x=66 in paint(), same font here), measured
    // rather than guessed so this can never overlap the subtitle even if
    // the OS substitutes a wider fallback font. Capped at the previous
    // fixed value (340) as a safety ceiling in case some platform's font
    // metrics come out wider than expected.
    {
        const juce::Font subtitleFont (juce::FontOptions (11.0f));
        const int subtitleTextWidth = juce::GlyphArrangement::getStringWidthInt (
            subtitleFont, "8-BAND MID/SIDE EQUALIZER");
        headerClusterX = juce::jmin (340, 66 + subtitleTextWidth + 24);
    }

    presetButton.setBounds (headerClusterX, 14, 170, 26);

    for (int s = 0; s < MSEQ8AudioProcessor::numSlots; ++s)
        slotButtons[s].setBounds (headerClusterX + 182 + s * 38, 14, 34, 26);

    monitorSelector.setBounds (headerClusterX + 350, 22, 104, 22);
    deltaButton.setBounds (headerClusterX + 474, 20, 30, 26);
    outGainKnob.setBounds (headerClusterX + 524, 20, 36, 32);
    matchGainButton.setBounds (headerClusterX + 580, 20, 56, 26);
    bypassButton.setBounds (headerClusterX + 656, 14, 56, 26);

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

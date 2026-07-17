#pragma once

#include "PluginProcessor.h"
#include "EQGraphComponent.h"
#include "LookAndFeel.h"

#include <array>

//==============================================================================
/** M / MS / S segmented selector bound to an AudioParameterChoice. */
class ModeSelector : public juce::Component,
                     private juce::AudioProcessorValueTreeState::Listener
{
public:
    ModeSelector (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
                  const juce::StringArray& labels = juce::StringArray { "M", "MS", "S" },
                  std::array<juce::Colour, 3> segmentColours
                      = { Theme::mid, Theme::midSide, Theme::side })
        : apvts (state), paramID (parameterID), colours (segmentColours)
    {
        for (int i = 0; i < 3; ++i)
        {
            auto& b = buttons[i];
            b.setButtonText (labels[i]);
            b.setClickingTogglesState (false);
            b.onClick = [this, i]
            {
                if (auto* p = apvts.getParameter (paramID))
                    p->setValueNotifyingHost (p->convertTo0to1 ((float) i));
            };
            addAndMakeVisible (b);
        }

        apvts.addParameterListener (paramID, this);
        updateFromParameter();
    }

    ~ModeSelector() override   { apvts.removeParameterListener (paramID, this); }

    void resized() override
    {
        auto r = getLocalBounds();
        const int w = r.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
            buttons[i].setBounds (r.removeFromLeft (w).reduced (1, 0));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::panelLight);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);

        const int mode = (int) apvts.getRawParameterValue (paramID)->load();
        auto highlight = getLocalBounds();
        const int w = highlight.getWidth() / 3;
        highlight = highlight.withX (highlight.getX() + mode * w).withWidth (w);

        g.setColour (colours[(size_t) juce::jlimit (0, 2, mode)].withAlpha (0.85f));
        g.fillRoundedRectangle (highlight.toFloat().reduced (1.0f), 3.0f);
    }

private:
    void parameterChanged (const juce::String&, float) override
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ModeSelector> (this)]
        {
            if (safe != nullptr)
                safe->updateFromParameter();
        });
    }

    void updateFromParameter()   { repaint(); }

    struct FlatButton : juce::TextButton
    {
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            g.setColour (over ? Theme::text : Theme::textDim);
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
        }
    };

    juce::AudioProcessorValueTreeState& apvts;
    juce::String paramID;
    std::array<juce::Colour, 3> colours;
    FlatButton buttons[3];
};

//==============================================================================
/** Rotary slider with value entry: double-click opens a text field.
    A "k" suffix is supported for frequencies (e.g. "1.5k" = 1500). */
class ValueEntryKnob : public juce::Slider
{
public:
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (valueEditor != nullptr)
            return;

        valueEditor = std::make_unique<juce::TextEditor>();
        valueEditor->setJustification (juce::Justification::centred);
        valueEditor->setFont (juce::Font (juce::FontOptions (12.0f)));
        valueEditor->setColour (juce::TextEditor::backgroundColourId, Theme::panelLight);
        valueEditor->setColour (juce::TextEditor::textColourId, Theme::text);
        valueEditor->setColour (juce::TextEditor::outlineColourId, Theme::mid);
        valueEditor->setColour (juce::TextEditor::focusedOutlineColourId, Theme::mid);
        valueEditor->setInputRestrictions (9, "0123456789.-kK");
        valueEditor->setText (juce::String (getValue(), 2), juce::dontSendNotification);
        valueEditor->setSelectAllWhenFocused (true);

        valueEditor->setBounds (getLocalBounds()
                                    .withSizeKeepingCentre (juce::jmax (48, getWidth()), 18));
        addAndMakeVisible (*valueEditor);
        valueEditor->grabKeyboardFocus();

        valueEditor->onReturnKey = [this] { closeEditor (true); };
        valueEditor->onEscapeKey = [this] { closeEditor (false); };
        valueEditor->onFocusLost = [this] { closeEditor (true); };
    }

private:
    void closeEditor (bool apply)
    {
        if (valueEditor == nullptr)
            return;

        const auto text = valueEditor->getText().trim().toLowerCase();

        // Release ownership first: makes the function reentry-safe and lets
        // us destroy the editor asynchronously (we may currently be inside
        // its own callback).
        auto* ed = valueEditor.release();
        ed->onReturnKey = nullptr;
        ed->onEscapeKey = nullptr;
        ed->onFocusLost = nullptr;
        removeChildComponent (ed);
        juce::MessageManager::callAsync ([ed] { delete ed; });

        if (apply && text.isNotEmpty())
        {
            const double mult = text.contains ("k") ? 1000.0 : 1.0;
            const double v = text.retainCharacters ("0123456789.-").getDoubleValue() * mult;
            setValue (v, juce::sendNotificationSync);
        }
    }

    std::unique_ptr<juce::TextEditor> valueEditor;
};

//==============================================================================
/** A band column: number, M/MS/S, Freq/Gain/Q knobs, bypass. */
class BandColumn : public juce::Component,
                   private juce::AudioProcessorValueTreeState::Listener
{
public:
    BandColumn (MSEQ8AudioProcessor& proc, int bandIndex, KnobLookAndFeel& lnf)
        : processor (proc), band (bandIndex),
          modeSelector (proc.apvts, MSEQ8AudioProcessor::modeID (bandIndex))
    {
        auto setupKnob = [&] (ValueEntryKnob& s, const juce::String& paramID,
                              std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& att)
        {
            s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s.setLookAndFeel (&lnf);
            addAndMakeVisible (s);
            att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                processor.apvts, paramID, s);
        };

        setupKnob (freqKnob, MSEQ8AudioProcessor::freqID (band), freqAtt);
        setupKnob (gainKnob, MSEQ8AudioProcessor::gainID (band), gainAtt);
        setupKnob (qKnob,    MSEQ8AudioProcessor::qID (band),    qAtt);

        addAndMakeVisible (modeSelector);

        bypassButton.setButtonText ("0");
        bypassButton.setClickingTogglesState (true);
        addAndMakeVisible (bypassButton);
        bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processor.apvts, MSEQ8AudioProcessor::bypassID (band), bypassButton);

        // Update the value text when knobs move
        freqKnob.onValueChange = [this] { repaint(); };
        gainKnob.onValueChange = [this] { repaint(); };
        qKnob.onValueChange    = [this] { repaint(); };

        // Notch has no gain: grey out the gain knob when the type changes
        processor.apvts.addParameterListener (MSEQ8AudioProcessor::typeID (band), this);
        refreshTypeState();
    }

    ~BandColumn() override
    {
        processor.apvts.removeParameterListener (MSEQ8AudioProcessor::typeID (band), this);
        freqKnob.setLookAndFeel (nullptr);
        gainKnob.setLookAndFeel (nullptr);
        qKnob.setLookAndFeel (nullptr);
    }

    void setHighlighted (bool shouldHighlight)
    {
        if (highlighted != shouldHighlight)
        {
            highlighted = shouldHighlight;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (highlighted ? Theme::panelLight : Theme::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        g.setColour (highlighted ? Theme::mid : Theme::outline);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f,
                                highlighted ? 1.5f : 1.0f);

        const int mode = (int) processor.apvts.getRawParameterValue (
                                   MSEQ8AudioProcessor::modeID (band))->load();
        const auto accent = Theme::bandColour (mode);

        // Band number in a circle
        g.setColour (accent.withAlpha (0.2f));
        g.fillEllipse ((float) getWidth() * 0.5f - 10.0f, 6.0f, 20.0f, 20.0f);
        g.setColour (accent);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (juce::String (band + 1), 0, 6, getWidth(), 20, juce::Justification::centred);

        // Column headers + values
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        const int knobW = getWidth() / 3;
        const char* names[3] = { "FREQ", "GAIN", "Q" };

        const bool gainless = (int) processor.apvts.getRawParameterValue (
                                  MSEQ8AudioProcessor::typeID (band))->load()
                              == MSEQ8AudioProcessor::typeNotch;

        juce::String values[3] = {
            formatFreq ((float) freqKnob.getValue()),
            gainless ? juce::String::charToString ((juce::juce_wchar) 0x2014)   // "—"
                     : juce::String (gainKnob.getValue() >= 0 ? "+" : "")
                           + juce::String (gainKnob.getValue(), 1),
            juce::String (qKnob.getValue(), 2)
        };

        for (int i = 0; i < 3; ++i)
        {
            g.setColour (Theme::textDim);
            g.drawText (names[i], i * knobW, 56, knobW, 12, juce::Justification::centred);
            g.setColour (Theme::text);
            g.drawText (values[i], i * knobW, 110, knobW, 12, juce::Justification::centred);
        }
    }

    void resized() override
    {
        modeSelector.setBounds (getLocalBounds().withY (32).withHeight (18)
                                                .reduced (getWidth() / 6, 0));
        const int knobW = getWidth() / 3;
        freqKnob.setBounds (0,         68, knobW, 42);
        gainKnob.setBounds (knobW,     68, knobW, 42);
        qKnob.setBounds    (knobW * 2, 68, knobW, 42);

        bypassButton.setBounds (getWidth() / 2 - 12, getHeight() - 28, 24, 22);
    }

private:
    void parameterChanged (const juce::String&, float) override
    {
        juce::MessageManager::callAsync (
            [safe = juce::Component::SafePointer<BandColumn> (this)]
            {
                if (safe != nullptr)
                    safe->refreshTypeState();
            });
    }

    void refreshTypeState()
    {
        const bool gainless = (int) processor.apvts.getRawParameterValue (
                                  MSEQ8AudioProcessor::typeID (band))->load()
                              == MSEQ8AudioProcessor::typeNotch;
        gainKnob.setEnabled (! gainless);
        repaint();
    }

    static juce::String formatFreq (float f)
    {
        return f >= 1000.0f ? juce::String (f / 1000.0f, 1) + "k"
                            : juce::String ((int) f);
    }

    MSEQ8AudioProcessor& processor;
    int band;
    bool highlighted = false;

    ModeSelector modeSelector;
    ValueEntryKnob freqKnob, gainKnob, qKnob;
    juce::TextButton bypassButton;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAtt, gainAtt, qAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;
};

//==============================================================================
/** Small horizontal broadband phase correlation meter (-1..+1), fed by
    MSEQ8AudioProcessor::correlation (polled once per editor timer tick).
    -1 = out of phase/mono-incompatible, 0 = wide/uncorrelated stereo image,
    +1 = mono-compatible. A cheap, always-visible companion to the mono-
    compatibility text warning already shown on the graph. */
class CorrelationMeter : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    void setValue (float newValue)
    {
        value = juce::jlimit (-1.0f, 1.0f, newValue);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::panelLight);
        g.fillRoundedRectangle (r, 2.0f);

        const float mid = r.getX() + r.getWidth() * 0.5f;
        g.setColour (Theme::outline);
        g.drawVerticalLine (juce::roundToInt (mid), r.getY(), r.getBottom());

        const float w = (value * 0.5f) * r.getWidth();
        auto fill = juce::Rectangle<float> (juce::jmin (mid, mid + w), r.getY(),
                                            std::abs (w), r.getHeight());
        g.setColour (value < -0.3f ? Theme::side : Theme::meterFill);
        g.fillRoundedRectangle (fill, 2.0f);
    }

private:
    float value = 1.0f;
};

//==============================================================================
/** Large in/out meter panel with a dB scale, placed to the right of the graph.
    Also hosts a small correlation strip at the bottom (see CorrelationMeter
    above) - a cheap, always-on companion to the level meters. */
class MeterPanel : public juce::Component
{
public:
    MeterPanel()
    {
        addAndMakeVisible (inMeter);
        addAndMakeVisible (outMeter);
        corrMeter.setTooltip ("Broadband phase correlation: -1 = out of phase, +1 = mono-compatible");
        addAndMakeVisible (corrMeter);
    }

    LevelMeter inMeter, outMeter;
    CorrelationMeter corrMeter;

    void paint (juce::Graphics& g) override
    {
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.setColour (Theme::textDim);
        g.drawText ("IN",  2, 0, 16, 14, juce::Justification::centred);
        g.drawText ("OUT", 26, 0, 16, 14, juce::Justification::centred);

        // dB scale (the meter maps -60..0 dB linearly in dB)
        const auto area = meterArea();
        for (int db : { 0, -12, -24, -36, -48 })
        {
            const float norm = ((float) db + 60.0f) / 60.0f;
            const int y = area.getY() + (int) ((1.0f - norm) * (float) area.getHeight());

            g.setColour (Theme::outline);
            g.drawHorizontalLine (y, 46.0f, 52.0f);
            g.setColour (Theme::textDim);
            g.drawText (juce::String (db), 54, y - 6, 28, 12, juce::Justification::left);
        }

        // Correlation strip: -1/+1 range labels bound the bar directly
        // below, which is sized to match the combined IN+OUT meter width
        // (x=2..42) rather than the full panel - keeps it visually aligned
        // with the meters above it instead of stretching into the dB-scale
        // number column to the right.
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.drawText ("-1", 2,  getHeight() - 24, 20, 12, juce::Justification::left);
        g.drawText ("+1", 22, getHeight() - 24, 20, 12, juce::Justification::right);
    }

    void resized() override
    {
        const auto area = meterArea();
        inMeter.setBounds  (2,  area.getY(), 16, area.getHeight());
        outMeter.setBounds (26, area.getY(), 16, area.getHeight());
        corrMeter.setBounds (2, getHeight() - 12, 40, 8);
    }

private:
    juce::Rectangle<int> meterArea() const
    {
        // Bottom trimmed further than before (4 -> 28) to make room for the
        // correlation strip's label + bar.
        return getLocalBounds().withTrimmedTop (18).withTrimmedBottom (28);
    }
};

//==============================================================================
class MSEQ8AudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit MSEQ8AudioProcessorEditor (MSEQ8AudioProcessor&);
    ~MSEQ8AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    void timerCallback() override;
    void updateSlotButtons();
    void showPresetMenu();
    void promptSavePreset();

    // Undo/redo debounce: see processor.paramChangeGeneration. Polled here
    // (the message thread) in timerCallback() - commits a snapshot only
    // after ~400 ms of quiet, so a continuous drag becomes a single step.
    uint32_t lastSeenGeneration = 0;
    double   lastParamChangeMs = 0.0;
    bool     pendingUndoCommit = false;

    static constexpr int userPresetBaseId = 100;
    static constexpr int savePresetItemId = 999;

    // x position of the header control cluster (preset -> A/B/C/D ->
    // monitor -> delta -> gain -> match -> bypass), measured in resized()
    // from the actual width of the "8-BAND MID/SIDE EQUALIZER" subtitle
    // text rather than a guessed fixed offset - so the cluster always
    // starts right after it regardless of the OS's font substitution.
    // Cached here (instead of remeasuring every paint()) since paint() also
    // needs it for the MONITOR/DELTA/GAIN labels above those controls.
    int headerClusterX = 340;

    MSEQ8AudioProcessor& processor;

    KnobLookAndFeel knobLnf;

    EQGraphComponent graph;
    juce::OwnedArray<BandColumn> bandColumns;

    juce::TextButton presetButton;
    juce::Array<juce::File> userPresetFiles;
    int lastPresetId = 1;

    juce::TextButton slotButtons[MSEQ8AudioProcessor::numSlots];

    ModeSelector monitorSelector;

    juce::TextButton deltaButton { juce::CharPointer_UTF8 ("\xce\x94") };   // Δ
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> deltaAtt;

    juce::TextButton bypassButton { "BYPASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;

    ValueEntryKnob outGainKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outGainAtt;

    // Match Gain: one-shot action (not a parameter), sets output_gain so
    // pre/post-EQ loudness match - see MSEQ8AudioProcessor::matchGain().
    juce::TextButton matchGainButton { "MATCH" };

    MeterPanel meterPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MSEQ8AudioProcessorEditor)
};

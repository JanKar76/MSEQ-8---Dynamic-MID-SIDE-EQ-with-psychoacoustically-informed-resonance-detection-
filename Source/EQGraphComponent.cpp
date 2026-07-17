#include "EQGraphComponent.h"
#include "LookAndFeel.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

//==============================================================================
// Standalone helper functions (don't need class membership, so they can live
// here without changing the header).
namespace
{
    constexpr float kLeftAxis     = 44.0f;   // left dB axis zone (matches yAxisZone)
    constexpr float kRightMargin  = 10.0f;
    constexpr float kTopMargin    = 60.0f;   // room for the cursor/resonance rows + legend
    constexpr float kBottomMargin = 22.0f;   // room for Hz labels

    // The magnitude from JUCE's unnormalised forward transform is uncalibrated:
    // a full-scale (0 dBFS) tone in one bin produces magnitude ~= (N/2) * the
    // window's coherent gain. With a Hann window (coherent gain 0.5) the
    // reference becomes N/4. Without this normalisation, all material reads
    // tens of dB too high - it needlessly hits the spectrum's display ceiling
    // AND makes Find Resonances' Threshold suggestion unmeasurable against the
    // real audio engine's (completely differently calibrated) envelope detector.
    constexpr float kFftMagRef = (float) MSEQ8AudioProcessor::fftSize * 0.25f;

    juce::Rectangle<float> plotArea (juce::Rectangle<float> full)
    {
        return { full.getX() + kLeftAxis, full.getY() + kTopMargin,
                 juce::jmax (10.0f, full.getWidth()  - kLeftAxis - kRightMargin),
                 juce::jmax (10.0f, full.getHeight() - kTopMargin - kBottomMargin) };
    }

    juce::Rectangle<int> undoButtonBounds (juce::Rectangle<int> full)
    {
        return { full.getRight() - 92, 6, 80, 20 };
    }

    // In proposal mode, APPLY + DISCARD are shown side by side (guide section 9).
    juce::Rectangle<int> applyButtonBounds (juce::Rectangle<int> full)
    {
        return { full.getRight() - 180, 6, 80, 20 };
    }

    juce::Rectangle<int> discardButtonBounds (juce::Rectangle<int> full)
    {
        return { full.getRight() - 92, 6, 80, 20 };
    }

    // Chooses a "nice" dB grid step from the current zoom level
    // (displayMaxDb, see EQGraphComponent::mouseWheelMove()) so the
    // horizontal grid lines stay useful at any zoom instead of a fixed 6 dB
    // spacing - which is a coarse step once you've zoomed in far enough that
    // 1-3 dB differences actually matter, and previously didn't even extend
    // out to the full +/-30 dB range when zoomed out.
    float dbGridStep (float displayMaxDb)
    {
        if (displayMaxDb <= 9.0f)  return 2.0f;
        if (displayMaxDb <= 15.0f) return 3.0f;
        return 6.0f;
    }

    // A rough but visually reasonable magnitude approximation for graph
    // rendering (the actual audio engine uses real biquad coefficients in
    // PluginProcessor; this is purely for drawing the curve).
    float bandMagnitudeDb (int type, float freq, float f0, float q, float gainDb)
    {
        if (freq <= 0.0f || f0 <= 0.0f)
            return 0.0f;

        const float oct = std::log2 (freq / f0);

        switch (type)
        {
            case MSEQ8AudioProcessor::typeLowShelf:
            {
                const float t = 1.0f / (1.0f + std::pow (freq / f0, 2.0f * juce::jmax (0.3f, q)));
                return gainDb * t;
            }
            case MSEQ8AudioProcessor::typeHighShelf:
            {
                const float t = 1.0f / (1.0f + std::pow (f0 / freq, 2.0f * juce::jmax (0.3f, q)));
                return gainDb * t;
            }
            case MSEQ8AudioProcessor::typeNotch:
            {
                const float bw = 1.0f / juce::jmax (0.1f, q);
                // Floor is just numerical safety (avoids a divide-by-zero if
                // q were ever huge) - it must stay below the narrowest bw*0.5
                // the frequency-dependent Q ceiling can produce, currently
                // 0.0125 at q=40 (see MSEQ8AudioProcessor::maxQForFreq).
                // 0.02 used to sit above that once the ceiling was raised
                // past the old flat max of 10, which silently froze the
                // curve's width for every Q above 10 - it never got a chance
                // to trigger back when 10 was the hard ceiling everywhere.
                const float x  = oct / juce::jmax (0.002f, bw * 0.5f);
                return -30.0f / (1.0f + x * x);
            }
            default: // Bell
            {
                const float bw = 1.0f / juce::jmax (0.1f, q);
                // Same fix as the notch case above (0.05 used to freeze the
                // visual width at the old Q=10 shape for anything higher).
                const float x  = oct / juce::jmax (0.005f, bw * 0.5f);
                return gainDb / (1.0f + x * x);
            }
        }
    }
}

//==============================================================================
/** Small floating panel (CallOutBox) for a band's filter type + dynamics. */
namespace
{
    class BandDynPanel : public juce::Component
    {
    public:
        BandDynPanel (MSEQ8AudioProcessor& proc, int band)
            : processor (proc), bandIndex (band),
              title ("BAND " + juce::String (band + 1))
        {
            static const char* typeLabels[4] = { "BELL", "LO SHELF", "HI SHELF", "NOTCH" };
            for (int i = 0; i < 4; ++i)
            {
                typeButtons[i].setButtonText (typeLabels[i]);
                typeButtons[i].onClick = [this, i]
                {
                    if (auto* p = processor.apvts.getParameter (
                            MSEQ8AudioProcessor::typeID (bandIndex)))
                        p->setValueNotifyingHost (p->convertTo0to1 ((float) i));
                    refreshTypeState();
                };
                typeButtons[i].setLookAndFeel (&lnf);
                addAndMakeVisible (typeButtons[i]);
            }

            // External sidechain toggle (see MSEQ8AudioProcessor::scID()):
            // when on and the host has actually connected a stereo
            // sidechain input, this band's detector listens to that
            // external signal instead of the band's own audio - lets one
            // band's gain reduction be triggered by e.g. a kick drum
            // elsewhere. Silently falls back to the band's own signal if no
            // sidechain is connected (see PluginProcessor::processBlock()).
            scButton.setClickingTogglesState (true);
            scButton.setColour (juce::TextButton::buttonColourId, Theme::panelLight);
            scButton.setColour (juce::TextButton::buttonOnColourId, Theme::side);
            scButton.setColour (juce::TextButton::textColourOffId, Theme::textDim);
            scButton.setColour (juce::TextButton::textColourOnId, Theme::background);
            scButton.setTooltip ("Trigger this band's dynamics from an external sidechain input");
            scButton.setLookAndFeel (&lnf);
            addAndMakeVisible (scButton);
            scAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                processor.apvts, MSEQ8AudioProcessor::scID (bandIndex), scButton);

            // NoTextBox + the value drawn manually in paint() (label above,
            // value below) - EXACTLY the same pattern as BandColumn's
            // FREQ/GAIN/Q knobs. These previously used TextBoxBelow, which
            // made JUCE's internal layout steal height from the rotary
            // control itself and made it visibly smaller than the knobs in
            // the band columns - with the same mechanism as the reference
            // (fixed height 42, no built-in text box) the diameter is
            // guaranteed to be identical regardless of column width.
            auto setup = [this] (juce::Slider& s)
            {
                s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
                s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                s.setLookAndFeel (&lnf);
                addAndMakeVisible (s);
            };

            setup (threshSlider);
            setup (rangeSlider);
            setup (attSlider);
            setup (relSlider);

            threshAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                processor.apvts, MSEQ8AudioProcessor::dynThreshID (bandIndex), threshSlider);
            rangeAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                processor.apvts, MSEQ8AudioProcessor::dynRangeID (bandIndex), rangeSlider);
            attAtt    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                processor.apvts, MSEQ8AudioProcessor::dynAttID (bandIndex), attSlider);
            relAtt    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                processor.apvts, MSEQ8AudioProcessor::dynRelID (bandIndex), relSlider);

            // Repaint when the knobs move, so the manually drawn values
            // (paint()) stay live in sync - same pattern as BandColumn.
            threshSlider.onValueChange = [this] { repaint(); };
            rangeSlider.onValueChange  = [this] { repaint(); };
            attSlider.onValueChange    = [this] { repaint(); };
            relSlider.onValueChange    = [this] { repaint(); };

            refreshTypeState();
        }

        ~BandDynPanel() override
        {
            threshSlider.setLookAndFeel (nullptr);
            rangeSlider.setLookAndFeel  (nullptr);
            attSlider.setLookAndFeel    (nullptr);
            relSlider.setLookAndFeel    (nullptr);
            scButton.setLookAndFeel (nullptr);
            for (auto& b : typeButtons)
                b.setLookAndFeel (nullptr);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::panel);

            g.setColour (Theme::text);
            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            // Width trimmed to leave room for the SC toggle button in the
            // top-right corner (see resized()).
            g.drawText (title, 12, 8, getWidth() - 64, 20, juce::Justification::left);

            // The label row's and value row's y must match the reserved gaps
            // in resized() exactly (8 inset + 26 title + 24 type row + 6 gap
            // etc.) - otherwise the text ends up behind/inside the rotary
            // control's arc instead of above/below it. The knob row is a
            // fixed 42px tall - the same height as BandColumn's FREQ/GAIN/Q,
            // which guarantees the rotary controls are drawn at exactly the
            // same diameter.
            static const char* knobLabels[4] = { "THRESHOLD", "DYN RANGE", "ATTACK", "RELEASE" };
            const int colW = (getWidth() - 16) / 4;
            const int labelY = 8 + 26 + 24 + 6;
            const int valueY = labelY + 14 + 2 + 42 + 2;
            g.setColour (Theme::textDim);
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            for (int i = 0; i < 4; ++i)
                g.drawText (knobLabels[i], 8 + i * colW, labelY, colW, 14, juce::Justification::centred);

            const juce::String values[4] = {
                juce::String (threshSlider.getValue() >= 0 ? "+" : "") + juce::String (threshSlider.getValue(), 1) + " dB",
                juce::String (rangeSlider.getValue()  >= 0 ? "+" : "") + juce::String (rangeSlider.getValue(),  1) + " dB",
                juce::String (attSlider.getValue(), 1) + " ms",
                juce::String (relSlider.getValue(), 1) + " ms"
            };
            g.setColour (Theme::text);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            for (int i = 0; i < 4; ++i)
                g.drawText (values[i], 8 + i * colW, valueY, colW, 14, juce::Justification::centred);
        }

        void resized() override
        {
            scButton.setBounds (getWidth() - 48, 6, 40, 18);

            auto r = getLocalBounds().reduced (8);
            r.removeFromTop (26);

            auto typeRow = r.removeFromTop (24);
            const int btnW = typeRow.getWidth() / 4;
            for (int i = 0; i < 4; ++i)
                typeButtons[i].setBounds (typeRow.removeFromLeft (btnW).reduced (2, 0));

            r.removeFromTop (6);
            r.removeFromTop (14);   // reserved for the column labels, drawn in paint()
            r.removeFromTop (2);
            auto knobRow = r.removeFromTop (42);   // same fixed height as BandColumn's knobs
            const int colW = knobRow.getWidth() / 4;
            threshSlider.setBounds (knobRow.removeFromLeft (colW).reduced (6, 0));
            rangeSlider.setBounds  (knobRow.removeFromLeft (colW).reduced (6, 0));
            attSlider.setBounds    (knobRow.removeFromLeft (colW).reduced (6, 0));
            relSlider.setBounds    (knobRow.removeFromLeft (colW).reduced (6, 0));
            // The rest of r (if any) is left blank below the value row, which is drawn in paint().
        }

    private:
        void refreshTypeState()
        {
            float t = 0.0f;
            if (auto* p = processor.apvts.getParameter (MSEQ8AudioProcessor::typeID (bandIndex)))
                t = p->getValue() * 3.0f; // 0..3

            const int activeType = juce::roundToInt (t);
            for (int i = 0; i < 4; ++i)
            {
                typeButtons[i].setToggleState (i == activeType, juce::dontSendNotification);
                typeButtons[i].setColour (juce::TextButton::buttonColourId,
                                          i == activeType ? Theme::mid : Theme::panelLight);
                typeButtons[i].setColour (juce::TextButton::textColourOffId,
                                          i == activeType ? Theme::background : Theme::textDim);
            }
        }

        MSEQ8AudioProcessor& processor;
        const int bandIndex;
        const juce::String title;

        juce::TextButton typeButtons[4];
        juce::TextButton scButton { "SC" };
        juce::Slider threshSlider, rangeSlider, attSlider, relSlider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> threshAtt, rangeAtt, attAtt, relAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> scAtt;

        KnobLookAndFeel lnf;
    };
}

//==============================================================================
EQGraphComponent::EQGraphComponent (MSEQ8AudioProcessor& p)
    : processor (p)
{
    spectra[specStereo].visible = false; // ST off by default, like the reference image

    for (int i = 0; i < numPoints; ++i)
        resWeight[(size_t) i] = perceptualWeightDb (pointFreq ((float) i));

    // Step 2: ERB-scaled neighbourhood windows (critical bands), precomputed
    // per point since it only depends on frequency, not on audio data.
    for (int i = 0; i < numPoints; ++i)
    {
        const float f = pointFreq ((float) i);
        const float erb = 24.7f * (4.37f * f / 1000.0f + 1.0f);   // Glasberg & Moore
        const int pLow  = pointForFreq (juce::jmax (minFreq, f - erb * 0.5f));
        const int pHigh = pointForFreq (juce::jmin (maxFreq, f + erb * 0.5f));
        nbHalf[(size_t) i] = juce::jmax (1, (pHigh - pLow) / 2);
    }

    // 30 fps is plenty for the spectrum/detection - everything is already
    // dt-based (exponential smoothing), so the result is identical at a
    // lower rate, only the redraw/FFT polling gets sparser. This also halves
    // the actual FFT frequency (from ~43/s production rate down to 30/s
    // polling rate), which lowers CPU load per open window - noticeable with
    // multiple instances.
    startTimerHz (30);
}

EQGraphComponent::~EQGraphComponent()
{
    stopTimer();

    // If the window closes while Ctrl+hover audition happens to be active,
    // it shouldn't get stuck "on" in the processor forever (no more
    // timerCallback to clear it).
    processor.auditionBand.store (-1);
}

//==============================================================================
void EQGraphComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    drawGrid (g);

    // Real-time spectra (post-EQ): mid, side, stereo
    drawSpectrumFill (g, spectra[specMid],    Theme::mid);
    drawSpectrumFill (g, spectra[specSide],   Theme::side);
    drawSpectrumFill (g, spectra[specStereo], Theme::midSide);

    drawFrozen (g);

    // The EQ curve (mid + side)
    juce::Path midCurve, sideCurve;
    buildCurve (midCurve,  false);
    buildCurve (sideCurve, true);
    g.setColour (Theme::mid.withAlpha (0.9f));
    g.strokePath (midCurve, juce::PathStrokeType (2.0f));
    g.setColour (Theme::side.withAlpha (0.9f));
    g.strokePath (sideCurve, juce::PathStrokeType (2.0f));

    if (resEnabled)
        drawResonances (g);

    drawProposals (g);

    drawAxisLabels (g);
    drawLegend (g);
    drawNodes (g);
    drawAuditionMarkers (g);

    if (hoveredNode >= 0)
        drawHoverReadout (g);
    if (cursorValid)
        drawCursorInfo (g);
    drawMonoWarning (g);

    drawAnalyzeChips (g);

    // BYPASSED badge in the top right (guide section 10)
    if (processor.apvts.getRawParameterValue ("global_bypass")->load() > 0.5f)
    {
        g.setColour (Theme::side);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText ("BYPASSED", getLocalBounds().removeFromTop (26).withTrimmedRight (12),
                    juce::Justification::centredRight);
    }
    else if (processor.apvts.getRawParameterValue ("delta")->load() > 0.5f)
    {
        // DELTA badge (guide section 10)
        g.setColour (Theme::side);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText ("DELTA", getLocalBounds().removeFromTop (26).withTrimmedRight (12),
                    juce::Justification::centredRight);
    }
}

void EQGraphComponent::resized()
{
    lastResizeMs = juce::Time::getMillisecondCounter();
    renderPaused = true;
}

//==============================================================================
void EQGraphComponent::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    // Ctrl+hover audition: updated every tick (not just on mouseMove) so it
    // feels immediate whether Ctrl is pressed/released while the mouse is
    // still, and always - regardless of the focus throttling below - so it
    // can never get stuck "on" in the processor if this particular window
    // starts being throttled while Ctrl is still held down.
    {
        const bool ctrlDown = juce::ModifierKeys::currentModifiers.isCtrlDown();
        const int wantAuditionBand = (ctrlDown && hoveredNode >= 0
                                       && hoveredNode < MSEQ8AudioProcessor::numBands)
                                    ? hoveredNode : -1;
        processor.auditionBand.store (wantAuditionBand);
    }

    // The UNDO affordance disappears 10 s after Apply - the parameters
    // already written are untouched, only the button/legend shortcut is hidden.
    if (analyzeState == AnalyzeState::applied && now - appliedAtMs > undoVisibleMs)
    {
        analyzeState = AnalyzeState::idle;
        proposals.clear();
        setAnalyzeMessage ({});
        repaint();
    }

    if (renderPaused)
    {
        // An ongoing analysis must never be paused - that would freeze the FFT data it's collecting.
        if (analyzeState == AnalyzeState::listening
            || juce::Time::getMillisecondCounter() - lastResizeMs > 200)
            renderPaused = false;
    }

    // Focus-based throttling: several plugin windows open at once often
    // share the host's UI thread (reported: FL Studio becomes sluggish/
    // unresponsive with 3+ open MSEQ 8 windows). A window that doesn't
    // currently have OS focus doesn't need FFT/smoothing/resonance
    // detection/repaint at the full 30 Hz - it isn't actively visible
    // anyway. Instead runs every 4th tick (~7.5 Hz), enough that the graph
    // doesn't look completely frozen. EXCEPTION: an ongoing Find Resonances
    // listening pass must always run at full rate, otherwise the measured
    // data becomes incomplete - it doesn't matter whether that particular
    // window happens to lack focus at that moment.
    bool windowFocused = true;
    if (auto* top = getTopLevelComponent())
        if (auto* peer = top->getPeer())
            windowFocused = peer->isFocused();

    if (! windowFocused && analyzeState != AnalyzeState::listening)
    {
        if (++backgroundSkipCounter < 4)
            return;
        backgroundSkipCounter = 0;
    }
    else
    {
        backgroundSkipCounter = 0;
    }

    // dt is counted from the last HEAVY update (not the last raw timer tick).
    // Must be set here, AFTER the throttle jump above - otherwise all
    // dt-based smoothing/detectors (smoothDisplay, updateDetector, resonance
    // jitter) would think only ~33 ms had passed when in background mode
    // (every 4th tick) actually ~133 ms had passed, which made the curve
    // only move a quarter of the way to the target and look choppy/stepped.
    float dt = lastFrameMs > 0.0 ? (float) ((now - lastFrameMs) / 1000.0) : (1.0f / 30.0f);
    dt = juce::jlimit (0.001f, 0.2f, dt);
    lastFrameMs = now;

    if (! renderPaused)
    {
        const bool gotMid  = processTap (processor.tapMid,  tapSeqMid,  midMagsDet,  midMagsDisp);
        const bool gotSide = processTap (processor.tapSide, tapSeqSide, sideMagsDet, sideMagsDisp);

        if (gotMid)  setDisplayTargets (spectra[specMid],  midMagsDisp,  now);
        if (gotSide) setDisplayTargets (spectra[specSide], sideMagsDisp, now);

        if (gotMid || gotSide)
        {
            for (int i = 0; i < numPoints; ++i)
                stereoMagsDisp[(size_t) i] = std::sqrt (midMagsDisp[(size_t) i] * midMagsDisp[(size_t) i]
                                                       + sideMagsDisp[(size_t) i] * sideMagsDisp[(size_t) i]);
            setDisplayTargets (spectra[specStereo], stereoMagsDisp, now);
        }

        for (auto& s : spectra)
            smoothDisplay (s, dt, now);

        // Fractional-octave-like frequency smoothing: a small triangle-
        // weighted window over NEIGHBOURING POINTS (not time), the same
        // established method used by REW/SMAART and others for a smooth
        // curve. Replaces the previous fixed EMA(1s) that inadvertently
        // overrode the SPEED control (SPEED still controls all time response
        // in smoothDisplay() above, untouched here). The detection (RES/Find
        // Resonances) has its own, completely separate ~400 ms averaging in
        // updateDetector() and is not affected.
        for (auto& s : spectra)
        {
            static constexpr float kernel[5] = { 1.0f, 2.0f, 3.0f, 2.0f, 1.0f };
            constexpr float kernelSum = 9.0f;

            for (int p = 0; p < numPoints; ++p)
            {
                float acc = 0.0f;
                for (int k = -2; k <= 2; ++k)
                {
                    const int idx = juce::jlimit (0, numPoints - 1, p + k);
                    acc += s.levels[(size_t) idx] * kernel[(size_t) (k + 2)];
                }
                s.visLevels[(size_t) p] = acc / kernelSum;
            }
        }

        updateResonances (dt);
        updateMonoCompat (dt);
    }

    if (analyzeState == AnalyzeState::listening)
    {
        analyzeElapsed += (double) dt * 1000.0;
        ++analyzeFrames;

        for (int i = 0; i < numPoints; ++i)
        {
            sumLevMid[(size_t) i]  += spectra[specMid].levels[(size_t) i];
            sumLevSide[(size_t) i] += spectra[specSide].levels[(size_t) i];
            sumDbMid[(size_t) i]   += detDbMid[(size_t) i];
            sumDbSide[(size_t) i]  += detDbSide[(size_t) i];
            // Max-based (detLevMid/Side), not the average display - otherwise
            // it falsely triggers "NO SIGNAL" since the average sits
            // systematically lower than the old max levels the threshold
            // (0.02) was calibrated against.
            analyzePeakLevel = juce::jmax (analyzePeakLevel,
                                           detLevMid[(size_t) i],
                                           detLevSide[(size_t) i]);
        }

        // Live countdown in the message row (whole seconds remaining).
        const int secondsLeft = juce::jmax (0, (int) std::ceil ((analyzeDurMs - analyzeElapsed) / 1000.0));
        analyzeMsg = "Listening... " + juce::String (secondsLeft) + "s";
        analyzeMsgUntilMs = 0.0;   // don't clear while we're listening

        if (analyzeElapsed >= analyzeDurMs)
            finishListening();
    }
    else if (analyzeMsgUntilMs > 0.0 && now > analyzeMsgUntilMs)
    {
        analyzeMsg = {};
        analyzeMsgUntilMs = 0.0;
    }

    repaint();
}

//==============================================================================
bool EQGraphComponent::processTap (MSEQ8AudioProcessor::SpectrumTap& tap, juce::uint32& lastSeen,
                                    std::array<float, numPoints>& magsMax,
                                    std::array<float, numPoints>& magsAvg)
{
    std::array<float, MSEQ8AudioProcessor::fftSize> raw;
    if (! tap.readLatest (raw.data(), lastSeen))
        return false;

    std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
    std::copy (raw.begin(), raw.end(), fftBuffer.begin());

    fftWindow.multiplyWithWindowingTable (fftBuffer.data(), MSEQ8AudioProcessor::fftSize);
    fft.performFrequencyOnlyForwardTransform (fftBuffer.data());

    const float sr = (float) processor.getCurrentSampleRate();

    for (int p = 0; p < numPoints; ++p)
    {
        const float f0 = pointFreq ((float) p);
        const float f1 = pointFreq ((float) juce::jmin (numPoints - 1, p + 1));

        const int bin0 = juce::jlimit (1, MSEQ8AudioProcessor::fftSize / 2 - 1,
                                       (int) (f0 * (float) MSEQ8AudioProcessor::fftSize / sr));
        const int bin1 = juce::jlimit (bin0, MSEQ8AudioProcessor::fftSize / 2 - 1,
                                       (int) (f1 * (float) MSEQ8AudioProcessor::fftSize / sr));

        // Max preserves sensitivity to narrow peaks (RES/Find Resonances
        // detection). Power/RMS averaging is the physically correct way to
        // combine several bins into a wider point and gives a much smoother
        // curve - max jumps between different individual bins frame to
        // frame, which is the actual cause of the jaggedness (not just the
        // bin width itself).
        float peak = fftBuffer[(size_t) bin0];
        float sumSq = 0.0f;
        for (int b = bin0; b <= bin1; ++b)
        {
            const float v = fftBuffer[(size_t) b];
            peak = juce::jmax (peak, v);
            sumSq += v * v;
        }
        const float avg = std::sqrt (sumSq / (float) (bin1 - bin0 + 1));

        // Calibrate to the real dBFS reference (see kFftMagRef above) -
        // without this the whole downstream chain (display + Find
        // Resonances threshold) is uncalibrated.
        magsMax[(size_t) p] = peak / kFftMagRef;
        magsAvg[(size_t) p] = avg  / kFftMagRef;
    }

    return true;
}

void EQGraphComponent::setDisplayTargets (SpectrumDisplay& disp, const std::array<float, numPoints>& mags,
                                          double nowMs)
{
    for (int p = 0; p < numPoints; ++p)
    {
        const float freq  = pointFreq ((float) p);
        const float tilt  = 3.0f * std::log2 (freq / 1000.0f);   // +3 dB/octave visual tilt
        const float db    = juce::Decibels::gainToDecibels (mags[(size_t) p], -100.0f) + tilt;
        disp.targets[(size_t) p] = juce::jlimit (0.0f, 1.0f, (db + 90.0f) / 90.0f);
    }
    disp.lastDataMs = nowMs;
}

void EQGraphComponent::smoothDisplay (SpectrumDisplay& disp, float dt, double)
{
    const float atk = 1.0f - std::exp (-dt / juce::jmax (0.001f, attackTau()));
    const float rel = 1.0f - std::exp (-dt / juce::jmax (0.001f, releaseTau()));

    for (int p = 0; p < numPoints; ++p)
    {
        const float target = disp.targets[(size_t) p];
        float& lvl = disp.levels[(size_t) p];
        lvl += (target - lvl) * (target > lvl ? atk : rel);
    }
}

float EQGraphComponent::attackTau() const     { return 0.015f; }

float EQGraphComponent::releaseTau() const
{
    switch (speedIndex)
    {
        case 0:  return 0.12f;   // FAST
        case 2:  return 0.80f;   // SLOW
        default: return 0.30f;   // MED
    }
}

void EQGraphComponent::toggleFreeze()
{
    frozenActive = ! frozenActive;
    if (frozenActive)
    {
        for (int s = 0; s < numSpectra; ++s)
        {
            frozenLevels[s]  = spectra[s].visLevels;   // freeze the same curve that's being displayed
            frozenVisible[s] = spectra[s].visible;
        }
    }
}

//==============================================================================
void EQGraphComponent::updateDetector (bool sideChannel, float dt)
{
    // Step 1: dedicated RMS detector spectrum (~400 ms power average),
    // completely independent of the SPEED setting - which only controls the
    // VISUAL curve (spectra[].levels). The detector instead reads raw FFT
    // magnitude directly.
    auto& powArr = sideChannel ? detPowSide : detPowMid;
    auto& dbArr  = sideChannel ? detDbSide  : detDbMid;
    auto& levArr = sideChannel ? detLevSide : detLevMid;
    const auto& mags = sideChannel ? sideMagsDet : midMagsDet;   // max - preserves narrow peaks

    // ~400 ms power average, frame-rate independent: alpha = 1 - exp(-dt/tau).
    const float kDetAlpha = 1.0f - std::exp (-juce::jlimit (0.001f, 0.2f, dt) / 0.4f);

    for (int p = 0; p < numPoints; ++p)
    {
        const float power = mags[(size_t) p] * mags[(size_t) p];
        powArr[(size_t) p] += (power - powArr[(size_t) p]) * kDetAlpha;

        const float rawDb = juce::Decibels::gainToDecibels (std::sqrt (powArr[(size_t) p]), -100.0f);
        // Perceptual weighting (~80 phon) so detection prioritises what the
        // ear is actually sensitive to, not just raw energy.
        dbArr[(size_t) p] = rawDb + resWeight[(size_t) p] * 0.5f;

        const float tilt = 3.0f * std::log2 (pointFreq ((float) p) / 1000.0f);
        levArr[(size_t) p] = juce::jlimit (0.0f, 1.0f, (rawDb + tilt + 90.0f) / 90.0f);
    }
}

void EQGraphComponent::detectChannelResonances (bool isSide, float dt)
{
    const auto& dbArr    = isSide ? detDbSide   : detDbMid;
    auto& jitterArr       = isSide ? jitterSide  : jitterMid;
    auto& lastOffArr      = isSide ? lastOffSide : lastOffMid;
    auto& scoreArr         = isSide ? resScoreSide : resScoreMid;
    auto& prevPoints       = isSide ? prevSidePoints : prevMidPoints;
    auto& flagArr          = isSide ? flagTimeSide : flagTimeMid;

    // Persistence: a point must have qualified for ~1 s before it counts as
    // a real resonance (guide section 8). qualifiedThisFrame is filled below.
    constexpr float persistSeconds = 1.0f;
    std::array<bool, numPoints> qualifiedThisFrame {};

    std::fill (scoreArr.begin(), scoreArr.end(), 0.0f);

    constexpr float maskingMarginDb        = 4.0f;   // how much the peak must stick up above its neighbours (lowered 7 -> 4 dB)
    constexpr float hysteresisDb           = 1.5f;   // lower threshold for points that are already active
    constexpr float jitterGateThreshold    = 0.35f;  // step 3: max allowed offset jitter in points
    constexpr float harmonicToleranceCents = 40.0f;  // step 5: how close a harmonic series counts as the same resonance
    constexpr float floorDb                = -60.0f;

    struct Candidate { int point; float level; float freq; };
    std::vector<Candidate> candidates;

    for (int p = 3; p < numPoints - 3; ++p)
    {
        const float c = dbArr[(size_t) p];
        const bool isLocalPeak = c > dbArr[(size_t) (p - 1)] && c > dbArr[(size_t) (p + 1)];

        if (! isLocalPeak || c < floorDb)
        {
            jitterArr[(size_t) p] *= 0.9f; // relax the jitter memory on points that aren't peaks right now
            continue;
        }

        // --- Step 3: frequency stability (parabolic peak-offset jitter gate) ---
        const float denom = dbArr[(size_t) (p - 1)] - 2.0f * c + dbArr[(size_t) (p + 1)];
        const float offset = std::abs (denom) > 1.0e-6f
                            ? 0.5f * (dbArr[(size_t) (p - 1)] - dbArr[(size_t) (p + 1)]) / denom
                            : 0.0f;

        const float jitterNow = std::abs (offset - lastOffArr[(size_t) p]);
        jitterArr[(size_t) p] += (jitterNow - jitterArr[(size_t) p]) * juce::jlimit (0.05f, 1.0f, dt * 4.0f);
        lastOffArr[(size_t) p] = offset;

        if (jitterArr[(size_t) p] > jitterGateThreshold)
            continue; // the peak wanders too much between frames -> likely noise, not a real resonance

        // --- Steps 2 + 4: ERB neighbourhood and masking gate ---
        const int half = nbHalf[(size_t) p];
        const int lo = juce::jmax (0, p - half);
        const int hi = juce::jmin (numPoints - 1, p + half);

        float neighbourSum = 0.0f;
        int neighbourCount = 0;
        for (int n = lo; n <= hi; ++n)
        {
            if (std::abs (n - p) < 2)
                continue; // skip the peak's own core

            neighbourSum += dbArr[(size_t) n];
            ++neighbourCount;
        }
        const float neighbourAvg = neighbourCount > 0 ? neighbourSum / (float) neighbourCount : c - 10.0f;

        const bool wasActive = std::find (prevPoints.begin(), prevPoints.end(), p) != prevPoints.end();
        const float requiredMargin = wasActive ? (maskingMarginDb - hysteresisDb) : maskingMarginDb;

        if (c < neighbourAvg + requiredMargin)
            continue; // masked by neighbouring energy, doesn't count as a distinct resonance

        // The point passes all gates this frame: accumulate persistence time.
        qualifiedThisFrame[(size_t) p] = true;
        flagArr[(size_t) p] = juce::jmin (persistSeconds * 1.5f, flagArr[(size_t) p] + dt);

        if (flagArr[(size_t) p] >= persistSeconds)
            candidates.push_back ({ p, c, pointFreq ((float) p) });
    }

    // Let the persistence time decay for points that didn't qualify this frame.
    for (int p = 0; p < numPoints; ++p)
        if (! qualifiedThisFrame[(size_t) p])
            flagArr[(size_t) p] = juce::jmax (0.0f, flagArr[(size_t) p] - dt * 2.0f);

    std::sort (candidates.begin(), candidates.end(),
              [] (const Candidate& a, const Candidate& b) { return a.level > b.level; });

    // --- Step 5: harmonic discrimination ---
    // Penalises peaks that are musical harmonics (or subharmonics) of an
    // already accepted, stronger peak, so a single resonant fundamental
    // isn't counted as several separate problems.
    std::vector<Candidate> accepted;
    for (auto& cand : candidates)
    {
        bool isHarmonicOfStronger = false;

        for (auto& acc : accepted)
        {
            for (int ratioNum = 1; ratioNum <= 5 && ! isHarmonicOfStronger; ++ratioNum)
            {
                for (int ratioDen = 1; ratioDen <= 5; ++ratioDen)
                {
                    if (ratioNum == ratioDen)
                        continue;

                    const float expected = acc.freq * (float) ratioNum / (float) ratioDen;
                    const float cents = 1200.0f * std::log2 (cand.freq / expected);
                    if (std::abs (cents) < harmonicToleranceCents)
                    {
                        isHarmonicOfStronger = true;
                        break;
                    }
                }
            }
            if (isHarmonicOfStronger)
                break;
        }

        if (! isHarmonicOfStronger)
        {
            accepted.push_back (cand);
            if (accepted.size() >= 3)
                break;
        }
    }

    prevPoints.clear();
    for (auto& acc : accepted)
    {
        scoreArr[(size_t) acc.point] = acc.level;
        prevPoints.push_back (acc.point);
        resonances.push_back ({ acc.point, acc.level, isSide });
    }
}

void EQGraphComponent::updateResonances (float dt)
{
    // The detector pipeline (steps 1-5) must always run, regardless of the
    // RES toggle: Find Resonances' signal check (detLevMid/Side) and dB
    // averaging (detDbMid/Side, flagTimeMid/Side via
    // detectChannelResonances) read the same data. Previously everything
    // here was aborted if RES was off, which made Find Resonances falsely
    // report "NO SIGNAL" if the user hadn't happened to have the RES
    // visualisation turned on. The drawing (drawResonances) has its own
    // resEnabled check and isn't affected by this.
    updateDetector (false, dt);
    updateDetector (true, dt);

    resonances.clear();

    detectChannelResonances (false, dt);
    detectChannelResonances (true, dt);
}

void EQGraphComponent::updateMonoCompat (float dt)
{
    // Average excess (dB) of side over mid in the low-frequency range
    // 20-500 Hz. Positive side energy there risks phase cancellation on mono
    // downmix (e.g. club PA, phone, some streaming services). Reuses
    // detDbMid/Side - the same data as the RES detector, no dedicated
    // measurement.
    const int hi = juce::jlimit (0, numPoints - 1, pointForFreq (500.0f));

    float sumExcess = 0.0f;
    int n = 0;
    for (int p = 0; p <= hi; ++p)
    {
        sumExcess += detDbSide[(size_t) p] - detDbMid[(size_t) p];
        ++n;
    }
    const float avgExcessDb = n > 0 ? sumExcess / (float) n : -100.0f;

    // Hysteresis: a higher threshold to TURN ON the warning than to keep it
    // on, so it doesn't flicker when the level sits right at the boundary.
    constexpr float onThresholdDb  = 3.0f;
    constexpr float offThresholdDb = 1.0f;
    constexpr float persistSeconds = 1.5f;   // short transients shouldn't trigger it

    const bool overThreshold = avgExcessDb > (monoWarnActive ? offThresholdDb : onThresholdDb);

    if (overThreshold)
        monoWarnTime = juce::jmin (persistSeconds * 1.5f, monoWarnTime + dt);
    else
        monoWarnTime = juce::jmax (0.0f, monoWarnTime - dt * 2.0f);

    monoWarnActive = monoWarnTime >= persistSeconds;
}

float EQGraphComponent::perceptualWeightDb (float freq)
{
    const float f = juce::jlimit (20.0f, 20000.0f, freq);
    const float logf = std::log10 (f);
    return -6.0f * (logf - 3.0f) * (logf - 3.0f) + 2.0f;
}

//==============================================================================
void EQGraphComponent::drawGrid (juce::Graphics& g) const
{
    const auto area = plotArea (getLocalBounds().toFloat());

    // See dbGridStep(): step shrinks as displayMaxDb shrinks (zooming in),
    // and the loop now covers the full +/-displayMaxDb range instead of a
    // fixed set of lines that could leave the outer part of the range bare
    // when zoomed out.
    const float step = dbGridStep (displayMaxDb);
    for (float db = 0.0f; db <= displayMaxDb + 0.01f; db += step)
    {
        const float y = gainToY (db);
        g.setColour (db == 0.0f ? Theme::outline.brighter (0.15f).withAlpha (0.6f)
                                 : Theme::outline.withAlpha (0.3f));
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());

        if (db > 0.0f)
        {
            const float yNeg = gainToY (-db);
            g.setColour (Theme::outline.withAlpha (0.3f));
            g.drawHorizontalLine ((int) yNeg, area.getX(), area.getRight());
        }
    }

    const float freqs[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
    g.setColour (Theme::outline.withAlpha (0.22f));
    for (float f : freqs)
        g.drawVerticalLine ((int) freqToX (f), area.getY(), area.getBottom());
}

void EQGraphComponent::drawAxisLabels (juce::Graphics& g) const
{
    const auto area = plotArea (getLocalBounds().toFloat());
    g.setFont (juce::Font (juce::FontOptions (10.0f)));

    // Matches drawGrid()'s zoom-adaptive step (see dbGridStep()) so labels
    // always line up with the grid lines actually drawn.
    auto drawDbLabel = [&] (float db)
    {
        const float y = gainToY (db);
        g.setColour (Theme::textDim.withAlpha (0.7f));
        g.drawText ((db > 0.0f ? "+" : "") + juce::String ((int) db),
                    4, (int) y - 6, (int) area.getX() - 6, 12, juce::Justification::right);
    };

    const float step = dbGridStep (displayMaxDb);
    for (float db = 0.0f; db <= displayMaxDb + 0.01f; db += step)
    {
        drawDbLabel (db);
        if (db > 0.0f)
            drawDbLabel (-db);
    }

    const float freqs[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
    for (float f : freqs)
    {
        g.setColour (Theme::textDim.withAlpha (0.55f));
        g.drawText (formatFreq (f), (int) freqToX (f) - 20, (int) area.getBottom() + 2, 40, 14,
                    juce::Justification::centred);
    }
}

void EQGraphComponent::drawSpectrumFill (juce::Graphics& g, const SpectrumDisplay& disp,
                                         juce::Colour colour) const
{
    if (! disp.visible)
        return;

    const auto area = plotArea (getLocalBounds().toFloat());

    // The spectrum is a background layer: the peaks reach at most 70% up so
    // it never hits the ceiling and the EQ curve/grid above it has room to breathe.
    constexpr float spectrumHeightScale = 0.7f;

    juce::Path path;
    bool first = true;
    for (int p = 0; p < numPoints; ++p)
    {
        const float x = freqToX (pointFreq ((float) p));
        const float y = area.getBottom() - disp.visLevels[(size_t) p] * area.getHeight() * spectrumHeightScale;
        if (first) { path.startNewSubPath (x, y); first = false; }
        else        path.lineTo (x, y);
    }

    juce::Path fill = path;
    fill.lineTo (area.getRight(), area.getBottom());
    fill.lineTo (area.getX(),     area.getBottom());
    fill.closeSubPath();

    juce::ColourGradient grad (colour.withAlpha (0.30f), 0.0f, area.getY(),
                                colour.withAlpha (0.02f), 0.0f, area.getBottom(), false);
    g.setGradientFill (grad);
    g.fillPath (fill);

    g.setColour (colour.withAlpha (0.65f));
    g.strokePath (path, juce::PathStrokeType (1.4f));
}

void EQGraphComponent::drawFrozen (juce::Graphics& g) const
{
    if (! frozenActive)
        return;

    const auto area = plotArea (getLocalBounds().toFloat());
    const juce::Colour cols[numSpectra] = { Theme::mid, Theme::side, Theme::midSide };
    const float dashLengths[] = { 4.0f, 3.0f };

    for (int s = 0; s < numSpectra; ++s)
    {
        if (! frozenVisible[s])
            continue;

        juce::Path path;
        bool first = true;
        for (int p = 0; p < numPoints; ++p)
        {
            const float x = freqToX (pointFreq ((float) p));
            const float y = area.getBottom() - frozenLevels[s][(size_t) p] * area.getHeight() * 0.7f;
            if (first) { path.startNewSubPath (x, y); first = false; }
            else        path.lineTo (x, y);
        }

        juce::PathStrokeType stroke (1.2f);
        juce::Path dashed;
        stroke.createDashedStroke (dashed, path, dashLengths, 2);

        g.setColour (cols[s].withAlpha (0.45f));
        g.strokePath (dashed, stroke);
    }
}

void EQGraphComponent::drawResonances (juce::Graphics& g) const
{
    if (! resEnabled || resonances.empty())
        return;

    const auto area = plotArea (getLocalBounds().toFloat());

    const auto labelFont = juce::Font (juce::FontOptions (9.5f, juce::Font::bold));

    // Sort by x position (frequency) so the row assignment below is
    // consistently left-to-right, regardless of the order the detection
    // (mid/side, strength) happened to add them to resonances.
    std::vector<const Resonance*> sorted;
    sorted.reserve (resonances.size());
    for (auto& r : resonances)
        sorted.push_back (&r);
    std::sort (sorted.begin(), sorted.end(),
              [this] (const Resonance* a, const Resonance* b)
              {
                  return freqToX (pointFreq ((float) a->point)) < freqToX (pointFreq ((float) b->point));
              });

    // Labels that lie close together in frequency would overlap
    // horizontally on the same row - instead stack them into separate rows
    // underneath each other (the marker/line stays at the right x position,
    // only the text moves down a row on collision).
    constexpr int labelW = 52, labelH = 12, rowGap = 13, maxRows = 4;
    float rowRightEdge[maxRows];
    std::fill (std::begin (rowRightEdge), std::end (rowRightEdge), -1.0e6f);

    for (auto* rp : sorted)
    {
        const auto& r = *rp;
        const float x = freqToX (pointFreq ((float) r.point));
        const auto colour = r.side ? Theme::side : Theme::mid;   // orange = side, green = mid

        g.setColour (colour.withAlpha (0.8f));
        // The line goes all the way down to the 0 dB line (not just a stub
        // at the ceiling) so the flag visually connects to the EQ curve's
        // zero position.
        g.drawVerticalLine ((int) x, area.getY(), gainToY (0.0f));
        g.fillEllipse (x - 2.5f, area.getY() + 6.0f, 5.0f, 5.0f);

        int row = 0;
        const float labelLeft = x + 5.0f;
        while (row < maxRows - 1 && labelLeft < rowRightEdge[row])
            ++row;
        rowRightEdge[row] = labelLeft + (float) labelW;

        // Frequency label directly to the right of each marker, in the same
        // channel colour - replaces the old combined "RES ..." row.
        g.setColour (colour);
        g.setFont (labelFont);
        g.drawText (formatFreq (pointFreq ((float) r.point)) + "Hz",
                    (int) labelLeft, (int) area.getY() + 1 + row * rowGap, labelW, labelH,
                    juce::Justification::left);
    }
}

void EQGraphComponent::drawLegend (juce::Graphics& g) const
{
    g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));

    struct Item { juce::Colour colour; bool active; bool dot; };
    const Item items[numLegendItems] = {
        { Theme::mid,     spectra[specMid].visible,           true  },
        { Theme::side,    spectra[specSide].visible,          true  },
        { Theme::midSide, spectra[specStereo].visible,        true  },
        { Theme::text,    resEnabled,                         false },
        { Theme::text,    true,                               false },
        { Theme::text,    frozenActive,                       false },
        { Theme::mid,     analyzeState != AnalyzeState::idle, true  }
    };

    for (int i = 0; i < numLegendItems; ++i)
    {
        auto b = legendItemBounds (i).toFloat();
        const auto& it = items[(size_t) i];
        const auto label = legendItemLabel (i);

        if (it.dot)
        {
            g.setColour (it.active ? it.colour : Theme::textDim.withAlpha (0.45f));
            g.fillEllipse (b.getX(), b.getCentreY() - 3.0f, 6.0f, 6.0f);
            g.drawText (label, b.withTrimmedLeft (12.0f), juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (it.active ? Theme::text : Theme::textDim.withAlpha (0.6f));
            g.drawText (label, b, juce::Justification::centredLeft);
        }
    }

    auto drawChipButton = [&g] (juce::Rectangle<int> b, const juce::String& text, juce::Colour accent)
    {
        g.setColour (Theme::panelLight);
        g.fillRoundedRectangle (b.toFloat(), 4.0f);
        g.setColour (accent.withAlpha (0.7f));
        g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 4.0f, 1.0f);
        g.setColour (Theme::text);
        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        g.drawText (text, b, juce::Justification::centred);
    };

    if (analyzeState == AnalyzeState::proposal)
    {
        drawChipButton (applyButtonBounds (getLocalBounds()),   "APPLY",   Theme::mid);
        drawChipButton (discardButtonBounds (getLocalBounds()), "DISCARD", Theme::side);
    }
    else if (analyzeState == AnalyzeState::applied)
    {
        drawChipButton (undoButtonBounds (getLocalBounds()), "UNDO", Theme::side);
    }
}

void EQGraphComponent::drawNodes (juce::Graphics& g) const
{
    {
        const bool hpIndep = paramValue ("hp_independent") > 0.5f;
        const bool lpIndep = paramValue ("lp_independent") > 0.5f;

        auto drawCutNode = [&] (int node, bool isHp, bool isSideVariant)
        {
            const juce::String prefix = isHp ? "hp" : "lp";
            const bool on = processor.apvts.getRawParameterValue (prefix + "_on")->load() > 0.5f;
            const bool indep = isHp ? hpIndep : lpIndep;
            const auto pos = nodePosition (node);
            // Without independent mode: white/dimmed triangle as before.
            // With independent mode: green (mid) or orange (side) to signal
            // that they're now two separate, independent controls - the same
            // convention as the rest of the graph (RES labels, curves).
            const auto baseColour = ! indep ? Theme::text
                                             : (isSideVariant ? Theme::side : Theme::mid);
            const auto colour = on ? baseColour : Theme::textDim;
            const float r = (hoveredNode == node) ? 7.0f : 5.5f;

            g.setColour (colour.withAlpha (on ? 0.9f : 0.4f));
            juce::Path tri;
            if (isHp) tri.addTriangle (pos.x - r, pos.y + r, pos.x + r, pos.y + r, pos.x, pos.y - r);
            else      tri.addTriangle (pos.x - r, pos.y - r, pos.x + r, pos.y - r, pos.x, pos.y + r);
            g.fillPath (tri);
        };

        drawCutNode (hpNode, true, false);
        drawCutNode (lpNode, false, false);
        if (hpIndep) drawCutNode (hpSideNode, true, true);
        if (lpIndep) drawCutNode (lpSideNode, false, true);
    }

    for (int i = 0; i < MSEQ8AudioProcessor::numBands; ++i)
    {
        const int mode = juce::jlimit (0, 2,
            (int) processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::modeID (i))->load());
        const auto accent = Theme::bandColour (mode);
        const auto pos = nodePosition (i);
        const float r = (hoveredNode == i || draggedNode == i) ? 9.0f : 7.0f;

        g.setColour (accent.withAlpha (0.30f));
        g.fillEllipse (pos.x - r * 1.6f, pos.y - r * 1.6f, r * 3.2f, r * 3.2f);

        g.setColour (accent.withAlpha (0.30f));
        g.drawVerticalLine ((int) pos.x, juce::jmin (pos.y, gainToY (0.0f)), juce::jmax (pos.y, gainToY (0.0f)));

        // Ghost ring: the band's actual real-time gain (static + dynamic).
        // Only shown for active dynamic bands (guide section 4). Mid =
        // green, side = orange; for MS bands, the mid channel's effective
        // gain is used.
        const bool isDyn = std::abs (processor.apvts.getRawParameterValue (
                               MSEQ8AudioProcessor::dynRangeID (i))->load()) > 0.01f;
        if (isDyn)
        {
            const bool useSide = (mode == MSEQ8AudioProcessor::modeSide);
            const float effGain = useSide ? processor.dynGainSide[i].load()
                                          : processor.dynGainMid[i].load();
            const float ghostY = gainToY (juce::jlimit (-displayMaxDb, displayMaxDb, effGain));
            const auto ghostColour = useSide ? Theme::side : Theme::mid;

            // Thin line from the static node to the ghost ring
            g.setColour (ghostColour.withAlpha (0.5f));
            g.drawLine (pos.x, pos.y, pos.x, ghostY, 1.0f);

            // The ring itself
            g.setColour (ghostColour.withAlpha (0.85f));
            g.drawEllipse (pos.x - r * 0.7f, ghostY - r * 0.7f, r * 1.4f, r * 1.4f, 1.5f);
        }

        juce::ColourGradient grad (accent.brighter (0.4f), pos.x - r * 0.3f, pos.y - r * 0.3f,
                                    accent.darker (0.3f), pos.x + r, pos.y + r, true);
        g.setGradientFill (grad);
        g.fillEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f);

        g.setColour (Theme::text.withAlpha (0.4f));
        g.drawEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f, 1.0f);

        g.setColour (Theme::text.withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions (r * 1.1f, juce::Font::bold)));
        g.drawText (juce::String (i + 1), (int) (pos.x - r), (int) (pos.y - r),
                    (int) (r * 2.0f), (int) (r * 2.0f), juce::Justification::centred);
    }
}

void EQGraphComponent::drawHoverReadout (juce::Graphics& g) const
{
    if (hoveredNode < 0 || hoveredNode >= MSEQ8AudioProcessor::numBands)
        return;

    const auto pos = nodePosition (hoveredNode);
    const float freq = processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::freqID (hoveredNode))->load();
    const float gain = processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::gainID (hoveredNode))->load();
    const float q    = processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::qID (hoveredNode))->load();
    const float dynRange = processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::dynRangeID (hoveredNode))->load();

    juce::String text = formatFreq (freq) + "Hz   " + (gain >= 0.0f ? "+" : "")
                       + juce::String (gain, 1) + "dB   Q " + juce::String (q, 2);

    // Active dynamic band: show the DYN settings (guide section 4).
    if (std::abs (dynRange) > 0.01f)
    {
        const float dynThresh = processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::dynThreshID (hoveredNode))->load();
        const float dynAtt    = processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::dynAttID (hoveredNode))->load();
        const float dynRel    = processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::dynRelID (hoveredNode))->load();
        text << "   DYN " << (dynRange >= 0.0f ? "+" : "") << juce::String (dynRange, 1)
             << "dB @ " << juce::String (dynThresh, 0) << "dB  "
             << juce::String (dynAtt, 0) << "/" << juce::String (dynRel, 0) << "ms";
    }

    const auto font = juce::Font (juce::FontOptions (11.0f, juce::Font::bold));
    g.setFont (font);

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, 0.0f, 0.0f);
    const int w = (int) glyphs.getBoundingBox (0, -1, true).getWidth() + 16;
    auto box = juce::Rectangle<int> (juce::roundToInt (pos.x - (float) w * 0.5f),
                                     juce::roundToInt (pos.y - 32.0f), w, 20)
                   .constrainedWithin (getLocalBounds());

    g.setColour (Theme::panel.withAlpha (0.95f));
    g.fillRoundedRectangle (box.toFloat(), 4.0f);
    g.setColour (Theme::outline);
    g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 4.0f, 1.0f);
    g.setColour (Theme::text);
    g.drawText (text, box, juce::Justification::centred);
}

void EQGraphComponent::drawCursorInfo (juce::Graphics& g) const
{
    // If a node is hovered, drawHoverReadout() already shows everything in
    // the same place - avoid two overlapping boxes.
    if (! cursorValid || hoveredNode >= 0)
        return;

    const float freq = xToFreq (cursorPos.x);
    const float gain = yToGain (cursorPos.y);

    const juce::String text = formatFreq (freq) + "Hz   " + noteName (freq, true) + "   "
                             + (gain >= 0.0f ? "+" : "") + juce::String (gain, 1) + "dB";

    // Fixed position in the plot's top-left corner - NOT in the header rows
    // (legend/Find Resonances chips/RES label) and not under the mouse
    // cursor either (which made the box cover the MID/SIDE/RES clicks when
    // the mouse was up there).
    const auto font = juce::Font (juce::FontOptions (10.5f, juce::Font::bold));
    g.setFont (font);

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, 0.0f, 0.0f);
    const int w = (int) glyphs.getBoundingBox (0, -1, true).getWidth() + 16;

    const auto area = plotArea (getLocalBounds().toFloat());
    auto box = juce::Rectangle<int> ((int) area.getX() + 6, (int) area.getY() + 6, w, 20);

    g.setColour (Theme::panel.withAlpha (0.95f));
    g.fillRoundedRectangle (box.toFloat(), 4.0f);
    g.setColour (Theme::outline);
    g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 4.0f, 1.0f);
    g.setColour (Theme::text);
    g.drawText (text, box, juce::Justification::centred);
}

void EQGraphComponent::drawMonoWarning (juce::Graphics& g) const
{
    if (! monoWarnActive)
        return;

    const juce::String text = "SIDE SIGNAL HIGH IN LOW FREQUENCIES";

    const auto font = juce::Font (juce::FontOptions (10.0f, juce::Font::bold));
    g.setFont (font);

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, 0.0f, 0.0f);
    const int w = (int) glyphs.getBoundingBox (0, -1, true).getWidth() + 16;

    // Same fixed corner as the crosshair/hover readout, but one row further
    // down (that one sits at area.getY()+6, height 20) so they never
    // collide even if both happen to show at once.
    const auto area = plotArea (getLocalBounds().toFloat());
    auto box = juce::Rectangle<int> ((int) area.getX() + 6, (int) area.getY() + 30, w, 20);

    g.setColour (Theme::side.withAlpha (0.18f));
    g.fillRoundedRectangle (box.toFloat(), 4.0f);
    g.setColour (Theme::side);
    g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 4.0f, 1.0f);
    g.drawText (text, box, juce::Justification::centred);
}

void EQGraphComponent::drawAuditionMarkers (juce::Graphics& g) const
{
    // Same condition as the DSP side (see timerCallback/PluginProcessor::processBlock):
    // Ctrl held down + hovering a band node. Purely visual, no dedicated
    // state - just reads the current freq/Q directly from APVTS for the
    // hovered band.
    if (hoveredNode < 0 || hoveredNode >= MSEQ8AudioProcessor::numBands
        || ! juce::ModifierKeys::currentModifiers.isCtrlDown())
        return;

    const float freq = processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::freqID (hoveredNode))->load();
    // Capped the same way as the actual audition bandpass filter
    // (PluginProcessor::processBlock) - these markers show the real
    // auditioned width, not the uncapped parameter value.
    const float q     = juce::jmin (juce::jmax (0.5f, processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::qID (hoveredNode))->load()),
                                    MSEQ8AudioProcessor::maxQForFreq (freq));

    // Same rough bandwidth approximation (bw ~= freq/Q) that Find
    // Resonances' Q measurement uses in reverse in buildProposals() -
    // consistent with how Q is interpreted visually elsewhere in the graph.
    const float bw  = juce::jmax (1.0f, freq / q);
    const float fLo = juce::jlimit (minFreq, maxFreq, freq - bw * 0.5f);
    const float fHi = juce::jlimit (minFreq, maxFreq, freq + bw * 0.5f);

    const auto area = plotArea (getLocalBounds().toFloat());
    const auto colour = processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::modeID (hoveredNode))->load()
                             == (float) MSEQ8AudioProcessor::modeSide ? Theme::side : Theme::mid;

    g.setColour (colour.withAlpha (0.6f));
    g.drawVerticalLine ((int) freqToX (fLo), area.getY(), area.getBottom());
    g.drawVerticalLine ((int) freqToX (fHi), area.getY(), area.getBottom());
}

//==============================================================================
void EQGraphComponent::buildCurve (juce::Path& p, bool sideChannel) const
{
    p.clear();
    const auto area = plotArea (getLocalBounds().toFloat());
    const int wantMode = sideChannel ? MSEQ8AudioProcessor::modeSide : MSEQ8AudioProcessor::modeMid;

    const bool hpOn = processor.apvts.getRawParameterValue ("hp_on")->load() > 0.5f;
    const bool lpOn = processor.apvts.getRawParameterValue ("lp_on")->load() > 0.5f;
    const bool hpIndep = processor.apvts.getRawParameterValue ("hp_independent")->load() > 0.5f;
    const bool lpIndep = processor.apvts.getRawParameterValue ("lp_independent")->load() > 0.5f;
    const int hpMode = (int) processor.apvts.getRawParameterValue ("hp_mode")->load();
    const int lpMode = (int) processor.apvts.getRawParameterValue ("lp_mode")->load();

    // Independent mode + the side curve: read hp_side_freq/hp_side_slope
    // instead of the shared hp_freq/hp_slope (and the same for LP).
    const float hpFreq = processor.apvts.getRawParameterValue (
        (hpIndep && sideChannel) ? "hp_side_freq" : "hp_freq")->load();
    const float lpFreq = processor.apvts.getRawParameterValue (
        (lpIndep && sideChannel) ? "lp_side_freq" : "lp_freq")->load();
    const float hpSlopeDbOct = 12.0f * (1.0f + (int) processor.apvts.getRawParameterValue (
        (hpIndep && sideChannel) ? "hp_side_slope" : "hp_slope")->load());
    const float lpSlopeDbOct = 12.0f * (1.0f + (int) processor.apvts.getRawParameterValue (
        (lpIndep && sideChannel) ? "lp_side_slope" : "lp_slope")->load());

    // In independent mode, hp_mode/lp_mode (routing) is ignored entirely -
    // both channels are always affected, each with its own freq/slope
    // (mirroring the processBlock() logic in PluginProcessor.cpp).
    const bool hpAffects = hpOn && (hpIndep || hpMode == MSEQ8AudioProcessor::cutStereo
                          || (sideChannel ? hpMode == MSEQ8AudioProcessor::cutSide
                                          : hpMode == MSEQ8AudioProcessor::cutMid));
    const bool lpAffects = lpOn && (lpIndep || lpMode == MSEQ8AudioProcessor::cutStereo
                          || (sideChannel ? lpMode == MSEQ8AudioProcessor::cutSide
                                          : lpMode == MSEQ8AudioProcessor::cutMid));

    bool first = true;
    for (int px = (int) area.getX(); px <= (int) area.getRight(); ++px)
    {
        const float freq = xToFreq ((float) px);
        float totalDb = 0.0f;

        for (int b = 0; b < MSEQ8AudioProcessor::numBands; ++b)
        {
            if (processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::bypassID (b))->load() > 0.5f)
                continue;

            const int mode = (int) processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::modeID (b))->load();
            if (mode != wantMode && mode != MSEQ8AudioProcessor::modeMidSide)
                continue;

            const int type = (int) processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::typeID (b))->load();
            const float f0 = processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::freqID (b))->load();
            // Same frequency-dependent Q ceiling as the actual filter (see
            // MSEQ8AudioProcessor::maxQForFreq) - otherwise the curve would
            // keep drawing narrower above the effective cap while the real
            // audio silently stays at the capped width, which is exactly
            // the mismatch that made it look like nothing happened past 10
            // on a low-frequency band.
            const float q  = juce::jmin (processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::qID (b))->load(),
                                         MSEQ8AudioProcessor::maxQForFreq (f0));
            const float gainDb = sideChannel ? processor.dynGainSide[b].load() : processor.dynGainMid[b].load();

            totalDb += bandMagnitudeDb (type, freq, f0, q, gainDb);
        }

        if (hpAffects && freq < hpFreq)
            totalDb -= std::log2 (juce::jmax (1.0f, hpFreq / juce::jmax (1.0f, freq))) * hpSlopeDbOct;

        if (lpAffects && freq > lpFreq)
            totalDb -= std::log2 (juce::jmax (1.0f, freq / juce::jmax (1.0f, lpFreq))) * lpSlopeDbOct;

        const float y = gainToY (juce::jlimit (-displayMaxDb, displayMaxDb, totalDb));
        if (first) { p.startNewSubPath ((float) px, y); first = false; }
        else        p.lineTo ((float) px, y);
    }
}

//==============================================================================
float EQGraphComponent::freqToX (float freq) const
{
    const auto area = plotArea (getLocalBounds().toFloat());
    const float norm = std::log (juce::jmax (minFreq, freq) / minFreq) / std::log (maxFreq / minFreq);
    return area.getX() + norm * area.getWidth();
}

float EQGraphComponent::xToFreq (float x) const
{
    const auto area = plotArea (getLocalBounds().toFloat());
    const float norm = juce::jlimit (0.0f, 1.0f, (x - area.getX()) / area.getWidth());
    return minFreq * std::pow (maxFreq / minFreq, norm);
}

float EQGraphComponent::gainToY (float gain) const
{
    const auto area = plotArea (getLocalBounds().toFloat());
    const float norm = (displayMaxDb - gain) / (2.0f * displayMaxDb);
    return area.getY() + norm * area.getHeight();
}

float EQGraphComponent::yToGain (float y) const
{
    const auto area = plotArea (getLocalBounds().toFloat());
    const float norm = (y - area.getY()) / area.getHeight();
    return displayMaxDb - norm * (2.0f * displayMaxDb);
}

//==============================================================================
float EQGraphComponent::pointFreq (float pointIndex)
{
    const float norm = pointIndex / (float) (numPoints - 1);
    return minFreq * std::pow (maxFreq / minFreq, norm);
}

int EQGraphComponent::pointForFreq (float freq)
{
    freq = juce::jlimit (minFreq, maxFreq, freq);
    const float norm = std::log (freq / minFreq) / std::log (maxFreq / minFreq);
    return juce::jlimit (0, numPoints - 1, juce::roundToInt (norm * (float) (numPoints - 1)));
}

juce::String EQGraphComponent::noteName (float freq, bool withCents)
{
    if (freq <= 0.0f)
        return {};

    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    const float midiF = 69.0f + 12.0f * std::log2 (freq / 440.0f);
    const int midi = juce::roundToInt (midiF);
    const float cents = (midiF - (float) midi) * 100.0f;
    const int octave = midi / 12 - 1;

    juce::String s = juce::String (names[((midi % 12) + 12) % 12]) + juce::String (octave);
    if (withCents)
        s += " " + juce::String (cents >= 0.0f ? "+" : "") + juce::String ((int) cents) + "c";
    return s;
}

juce::String EQGraphComponent::formatFreq (float freq)
{
    return freq >= 1000.0f ? juce::String (freq / 1000.0f, freq >= 10000.0f ? 1 : 2) + "k"
                           : juce::String ((int) freq);
}

//==============================================================================
juce::Point<float> EQGraphComponent::nodePosition (int node) const
{
    const auto area = plotArea (getLocalBounds().toFloat());

    if (node == hpNode)
    {
        const float f = processor.apvts.getRawParameterValue ("hp_freq")->load();
        return { freqToX (f), area.getY() + 8.0f };
    }
    if (node == lpNode)
    {
        const float f = processor.apvts.getRawParameterValue ("lp_freq")->load();
        return { freqToX (f), area.getY() + 8.0f };
    }
    if (node == hpSideNode)
    {
        // Offset one row below the mid triangle (otherwise impossible to
        // tell apart/click correctly when they happen to sit at the same
        // frequency, e.g. before the user has dragged them apart for the
        // first time).
        const float f = processor.apvts.getRawParameterValue ("hp_side_freq")->load();
        return { freqToX (f), area.getY() + 22.0f };
    }
    if (node == lpSideNode)
    {
        const float f = processor.apvts.getRawParameterValue ("lp_side_freq")->load();
        return { freqToX (f), area.getY() + 22.0f };
    }

    const float f = processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::freqID (node))->load();
    const float g = processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::gainID (node))->load();
    return { freqToX (f), gainToY (g) };
}

int EQGraphComponent::findNodeAt (juce::Point<float> pos) const
{
    int best = -1;
    float bestDist = 260.0f;

    for (int i = 0; i < MSEQ8AudioProcessor::numBands; ++i)
    {
        const float d = pos.getDistanceSquaredFrom (nodePosition (i));
        if (d < bestDist) { bestDist = d; best = i; }
    }

    const bool hpIndep = paramValue ("hp_independent") > 0.5f;
    const bool lpIndep = paramValue ("lp_independent") > 0.5f;

    for (int node : { hpNode, lpNode, hpSideNode, lpSideNode })
    {
        // The side nodes only exist (are clickable/hoverable) when the
        // respective filter is in independent mode - otherwise they're
        // invisible duplicates of the mid node and would just steal clicks
        // needlessly.
        if (node == hpSideNode && ! hpIndep) continue;
        if (node == lpSideNode && ! lpIndep) continue;

        const float d = pos.getDistanceSquaredFrom (nodePosition (node));
        if (d < bestDist) { bestDist = d; best = node; }
    }

    return best;
}

juce::String EQGraphComponent::legendItemLabel (int item) const
{
    static const char* speedNames[3] = { "FAST", "MED", "SLOW" };

    switch (item)
    {
        case legMid:     return "MID";
        case legSide:    return "SIDE";
        case legStereo:  return "ST";
        case legRes:     return "RES";
        case legSpeed:   return juce::String ("SPEED: ") + speedNames[juce::jlimit (0, 2, speedIndex)];
        case legFreeze:  return "FREEZE";
        case legAnalyze: return "FIND RESONANCES";
        default:         return {};
    }
}

juce::Rectangle<int> EQGraphComponent::legendItemBounds (int item) const
{
    // The layout is measured from the actual text (the same font drawLegend
    // uses) so the click zone never drifts apart from what's drawn. "dot"
    // items have a 12 px dot indent before the text.
    const juce::Font font (juce::FontOptions (10.5f, juce::Font::bold));

    auto hasDot = [] (int i) { return i == legMid || i == legSide || i == legStereo || i == legAnalyze; };

    int x = 12;
    for (int i = 0; i <= item; ++i)
    {
        const int dotPad  = hasDot (i) ? 12 : 0;
        const int textW   = juce::GlyphArrangement::getStringWidthInt (font, legendItemLabel (i));
        const int itemW   = dotPad + textW;

        if (i == item)
            return { x, 6, itemW, 16 };

        x += itemW + 12;   // 12 px gap between items
    }

    return { x, 6, 40, 16 };
}

int EQGraphComponent::findLegendItemAt (juce::Point<int> pos) const
{
    for (int i = 0; i < numLegendItems; ++i)
        if (legendItemBounds (i).expanded (4, 4).contains (pos))
            return i;
    return -1;
}

//==============================================================================
float EQGraphComponent::paramValue (const juce::String& id) const
{
    if (auto* p = processor.apvts.getParameter (id))
        return p->getValue();
    return 0.0f;
}

void EQGraphComponent::setParamValue (const juce::String& id, float normalizedValue)
{
    if (auto* p = processor.apvts.getParameter (id))
        p->setValueNotifyingHost (normalizedValue);
}

void EQGraphComponent::showCutMenu (bool isHighPass, bool sideVariant)
{
    const juce::String prefix = isHighPass ? "hp" : "lp";
    const bool indep = paramValue (prefix + "_independent") > 0.5f;

    // Independent mode: the mid variant reads/writes prefix+"_slope" (as
    // before), the side variant reads/writes prefix+"_side_slope". Outside
    // independent mode there's no side node to click, so sideVariant is
    // then always false (still guarded here for safety).
    const bool useSideParams = indep && sideVariant;
    const juce::String slopeParam = useSideParams ? (prefix + "_side_slope") : (prefix + "_slope");

    const bool on      = paramValue (prefix + "_on") > 0.5f;
    const int  slope   = juce::roundToInt (paramValue (slopeParam) * 2.0f);
    const int  routing = juce::roundToInt (paramValue (prefix + "_mode") * 2.0f);

    juce::Component::SafePointer<EQGraphComponent> safe (this);
    auto set = [safe, prefix] (const juce::String& suffix, float v)
    {
        if (safe != nullptr)
            safe->setParamValue (prefix + suffix, v);
    };
    auto setSlope = [safe, slopeParam] (float v)
    {
        if (safe != nullptr)
            safe->setParamValue (slopeParam, v);
    };

    juce::PopupMenu menu;
    menu.setLookAndFeel (&menuLnf);
    menu.addItem (juce::String (isHighPass ? "High-pass" : "Low-pass") + " enabled",
                  true, on, [set, on] { set ("_on", on ? 0.0f : 1.0f); });

    juce::PopupMenu slopeMenu;
    const char* slopeNames[3] = { "12 dB/oct", "24 dB/oct", "48 dB/oct" };
    for (int i = 0; i < 3; ++i)
        slopeMenu.addItem (slopeNames[i], true, slope == i,
                           [setSlope, i] { setSlope ((float) i / 2.0f); });
    menu.addSubMenu (! indep ? "Slope" : (useSideParams ? "Slope (Side)" : "Slope (Mid)"), slopeMenu);

    // Routing (Stereo/Mid only/Side only) is meaningless in independent mode -
    // both channels always run then, each with its own freq/slope.
    if (! indep)
    {
        juce::PopupMenu routeMenu;
        const char* routeNames[3] = { "Stereo", "Mid only", "Side only" };
        for (int i = 0; i < 3; ++i)
            routeMenu.addItem (routeNames[i], true, routing == i,
                               [set, i] { set ("_mode", (float) i / 2.0f); });
        menu.addSubMenu ("Routing", routeMenu);
    }

    menu.addSeparator();
    menu.addItem ("Independent Mid/Side", true, indep,
                  [set, indep] { set ("_independent", indep ? 0.0f : 1.0f); });

    // Anchor the menu at the triangle's actual position instead of letting
    // PopupMenu fall back to an undefined default location.
    const int anchorNode = isHighPass ? (sideVariant ? hpSideNode : hpNode)
                                       : (sideVariant ? lpSideNode : lpNode);
    const auto localPos  = nodePosition (anchorNode);
    const auto screenPos = localPointToGlobal (localPos);
    const juce::Rectangle<int> targetArea (juce::roundToInt (screenPos.x) - 4,
                                           juce::roundToInt (screenPos.y) - 4, 8, 8);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                   .withTargetScreenArea (targetArea));
}

//==============================================================================
void EQGraphComponent::mouseDown (const juce::MouseEvent& e)
{
    // Find Resonances buttons are handled before the legend (they sit in the top right).
    const auto pi = e.position.toInt();

    if (analyzeState == AnalyzeState::proposal)
    {
        if (applyButtonBounds (getLocalBounds()).contains (pi))   { applyProposals(); repaint(); return; }
        if (discardButtonBounds (getLocalBounds()).contains (pi)) { cancelAnalyze();  repaint(); return; }
    }
    else if (analyzeState == AnalyzeState::applied)
    {
        if (undoButtonBounds (getLocalBounds()).contains (pi))    { undoApply();      repaint(); return; }

        // A click anywhere else in the plugin while the "Applied" affordance
        // is showing -> hide it immediately (the same reset the 10s timeout
        // in timerCallback() does), instead of forcing the user to wait out
        // the timeout. The click falls through and does its usual thing
        // (node, legend, etc.) exactly as normal.
        analyzeState = AnalyzeState::idle;
        proposals.clear();
        setAnalyzeMessage ({});
    }

    const int item = findLegendItemAt (pi);
    if (item >= 0)
    {
        switch (item)
        {
            case legMid:     spectra[specMid].visible    = ! spectra[specMid].visible;    break;
            case legSide:    spectra[specSide].visible   = ! spectra[specSide].visible;   break;
            case legStereo:  spectra[specStereo].visible = ! spectra[specStereo].visible; break;
            case legRes:     resEnabled = ! resEnabled;                                   break;
            case legSpeed:   speedIndex = (speedIndex + 1) % 3;                           break;
            case legFreeze:  toggleFreeze();                                              break;
            case legAnalyze:
                // Idle -> start. In every other state, the Find Resonances
                // label acts as a cancel/reset toggle so the button never
                // feels "dead" (APPLY/DISCARD/UNDO remain as separate buttons).
                if (analyzeState == AnalyzeState::idle)            startAnalyze();
                else if (analyzeState == AnalyzeState::applied)    undoApply();
                else                                               cancelAnalyze();
                break;
            default: break;
        }
        repaint();
        return;
    }

    const int node = findNodeAt (e.position);

    if (node >= 0 && node < MSEQ8AudioProcessor::numBands)
    {
        if (e.mods.isRightButtonDown())
        {
            // The panel opens directly on right-click - there used to be a
            // single-line intermediate menu ("Type & Dynamics...") in the
            // way that was just an extra click with no function, now removed.
            //
            // CallOutBox NEVER draws itself on top of its targetArea - only
            // outside it (left/right/top/bottom, whichever side has the most
            // room). If targetArea is just an 8x8 point at the click, the
            // bubble still ends up right next to the node and covers the
            // curve shape around it. By instead making targetArea as tall as
            // the entire plot area and wide enough to cover the visible
            // bell/shelf shape around the node, CallOutBox is forced out to
            // the left or right of the ENTIRE area, so you can see the curve
            // change live while adjusting the dynamics parameters (Jan's
            // request).
            const auto area = plotArea (getLocalBounds().toFloat());
            const auto pos  = nodePosition (node);
            const int halfW = 140;
            const juce::Rectangle<int> targetArea (juce::roundToInt (pos.x) - halfW,
                                                    (int) area.getY(),
                                                    halfW * 2,
                                                    (int) area.getHeight());

            auto panel = std::make_unique<BandDynPanel> (processor, node);
            panel->setSize (280, 152);   // tighter now that the knobs are NoTextBox (same size as BandColumn)
            auto& box = juce::CallOutBox::launchAsynchronously (std::move (panel), targetArea, this);
            box.setArrowSize (8.0f);
            box.setLookAndFeel (&menuLnf);   // the bubble's own chrome (background/border), not just the content
            return;
        }

        draggedNode = node;
        return;
    }

    if (node == hpNode || node == lpNode || node == hpSideNode || node == lpSideNode)
    {
        const bool isHp = (node == hpNode || node == hpSideNode);
        const bool isSideVariant = (node == hpSideNode || node == lpSideNode);

        if (e.mods.isRightButtonDown())
        {
            showCutMenu (isHp, isSideVariant);
            return;
        }
        draggedNode = node;
        return;
    }

    auto bounds = getLocalBounds().toFloat();
    if (e.mods.isRightButtonDown())
    {
        if (e.position.x < bounds.getX() + bounds.getWidth() * 0.12f)        { showCutMenu (true);  return; }
        if (e.position.x > bounds.getRight() - bounds.getWidth() * 0.12f)    { showCutMenu (false); return; }
    }

    draggedNode = -1;
}

void EQGraphComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggedNode < 0)
        return;

    const auto area = plotArea (getLocalBounds().toFloat());
    const float freq = xToFreq (juce::jlimit (area.getX(), area.getRight(), e.position.x));

    if (draggedNode == hpNode)
    {
        if (auto* p = processor.apvts.getParameter ("hp_freq"))
            p->setValueNotifyingHost (p->convertTo0to1 (freq));
    }
    else if (draggedNode == lpNode)
    {
        if (auto* p = processor.apvts.getParameter ("lp_freq"))
            p->setValueNotifyingHost (p->convertTo0to1 (freq));
    }
    else if (draggedNode == hpSideNode)
    {
        if (auto* p = processor.apvts.getParameter ("hp_side_freq"))
            p->setValueNotifyingHost (p->convertTo0to1 (freq));
    }
    else if (draggedNode == lpSideNode)
    {
        if (auto* p = processor.apvts.getParameter ("lp_side_freq"))
            p->setValueNotifyingHost (p->convertTo0to1 (freq));
    }
    else if (draggedNode < MSEQ8AudioProcessor::numBands)
    {
        const float gain = yToGain (juce::jlimit (area.getY(), area.getBottom(), e.position.y));

        if (auto* pf = processor.apvts.getParameter (MSEQ8AudioProcessor::freqID (draggedNode)))
            pf->setValueNotifyingHost (pf->convertTo0to1 (freq));
        if (auto* pg = processor.apvts.getParameter (MSEQ8AudioProcessor::gainID (draggedNode)))
            pg->setValueNotifyingHost (pg->convertTo0to1 (gain));
    }

    repaint();
}

void EQGraphComponent::mouseUp (const juce::MouseEvent&)
{
    draggedNode = -1;
}

void EQGraphComponent::mouseMove (const juce::MouseEvent& e)
{
    cursorPos = e.position;
    cursorValid = true;

    const int oldHover = hoveredNode;
    hoveredNode = findNodeAt (e.position);

    if (hoveredNode != oldHover && onBandHover != nullptr)
        onBandHover (hoveredNode >= 0 && hoveredNode < MSEQ8AudioProcessor::numBands ? hoveredNode : -1);

    repaint();
}

void EQGraphComponent::mouseExit (const juce::MouseEvent&)
{
    cursorValid = false;
    if (hoveredNode != -1)
    {
        hoveredNode = -1;
        if (onBandHover != nullptr)
            onBandHover (-1);
    }
    repaint();
}

void EQGraphComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int node = findNodeAt (e.position);
    // The mid and side variants share the same on/off (hp_on/lp_on applies
    // to the whole filter, regardless of independent mode) - double-clicking
    // either toggles both.
    if (node == hpNode || node == hpSideNode)      setParamValue ("hp_on", paramValue ("hp_on") > 0.5f ? 0.0f : 1.0f);
    else if (node == lpNode || node == lpSideNode) setParamValue ("lp_on", paramValue ("lp_on") > 0.5f ? 0.0f : 1.0f);
    repaint();
}

void EQGraphComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    auto bounds = getLocalBounds().toFloat();

    if (e.position.x < bounds.getX() + (float) yAxisZone)
    {
        displayMaxDb = juce::jlimit (6.0f, 30.0f, displayMaxDb - wheel.deltaY * 6.0f);
        repaint();
        return;
    }

    const int node = findNodeAt (e.position);
    if (node >= 0 && node < MSEQ8AudioProcessor::numBands)
    {
        if (auto* pq = processor.apvts.getParameter (MSEQ8AudioProcessor::qID (node)))
        {
            const float currentQ = pq->convertFrom0to1 (pq->getValue());
            const float nodeFreq = processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::freqID (node))->load();
            const float newQ = juce::jlimit (0.1f, MSEQ8AudioProcessor::maxQForFreq (nodeFreq), currentQ + wheel.deltaY * 2.0f);
            pq->setValueNotifyingHost (pq->convertTo0to1 (newQ));
            repaint();
        }
    }
}

//==============================================================================
void EQGraphComponent::startAnalyze()
{
    analyzeState = AnalyzeState::listening;
    analyzeStartMs = juce::Time::getMillisecondCounterHiRes();
    analyzeElapsed = 0.0;
    analyzeFrames = 0;
    analyzePeakLevel = 0.0f;
    proposals.clear();

    sumLevMid.fill (0.0);
    sumLevSide.fill (0.0);
    sumDbMid.fill (0.0);
    sumDbSide.fill (0.0);

    setAnalyzeMessage ("Listening... 8s");
}

void EQGraphComponent::cancelAnalyze()
{
    analyzeState = AnalyzeState::idle;
    proposals.clear();
    setAnalyzeMessage ({});
}

void EQGraphComponent::finishListening()
{
    buildProposals();

    if (! proposals.empty())
    {
        analyzeState = AnalyzeState::proposal;
        setAnalyzeMessage (juce::String ((int) proposals.size()) + " suggestions ready");
        return;
    }

    // No suggestions: distinguish silent material from clean material (guide section 9).
    analyzeState = AnalyzeState::idle;
    // analyzePeakLevel is a normalised 0..1 level accumulated during listening.
    const bool silent = analyzePeakLevel < 0.02f;
    setAnalyzeMessage (silent ? "NO SIGNAL" : "NO PROBLEM RESONANCES FOUND");
}

void EQGraphComponent::buildProposals()
{
    // Derives suggestions directly from the measurements taken while
    // listening (guide section 9):
    //   Q         : from the resonance's -3 dB bandwidth, clamped 2-12
    //   cut       : ~60% of how much the peak sticks up above its neighbourhood, 2-9 dB
    //   threshold : just below the resonance's own average level
    //   attack    : ~two periods of the frequency
    //   release   : the resonator's physical decay, t60 ~= 2.2*Q/f
    proposals.clear();

    const int frames = juce::jmax (1, analyzeFrames);

    struct Peak { int point; float freq; float excessDb; float levelDb; float q; bool side; int lp, rp; };
    std::vector<Peak> peaks;

    for (bool side : { false, true })
    {
        const auto& sumDb = side ? sumDbSide : sumDbMid;

        // Average dB per graph point over the listening window.
        std::array<float, numPoints> meanDb {};
        for (int p = 0; p < numPoints; ++p)
            meanDb[(size_t) p] = (float) (sumDb[(size_t) p] / (double) frames);

        // A lightly neighbour-smoothed copy used only for the -3dB bandwidth
        // measurement below (the peak detection above/below still uses the
        // raw meanDb, untouched). Without this, a single noisy point could
        // make an otherwise moderately wide resonance appear falsely narrow
        // - and thus give an artificially high Q that clamps against the
        // ceiling, regardless of the resonance's real character.
        std::array<float, numPoints> bwDb {};
        for (int p = 0; p < numPoints; ++p)
        {
            const int a = juce::jmax (0, p - 1), b = juce::jmin (numPoints - 1, p + 1);
            bwDb[(size_t) p] = (meanDb[(size_t) a] + 2.0f * meanDb[(size_t) p] + meanDb[(size_t) b]) * 0.25f;
        }

        for (int p = 3; p < numPoints - 3; ++p)
        {
            const float c = meanDb[(size_t) p];

            // Local peak?
            if (! (c > meanDb[(size_t) (p - 1)] && c >= meanDb[(size_t) (p + 1)]))
                continue;

            // ERB neighbourhood (same window width as the detector),
            // median-like via averaging excluding the peak's own core.
            const int half = juce::jmax (3, nbHalf[(size_t) p]);
            const int lo = juce::jmax (0, p - half);
            const int hi = juce::jmin (numPoints - 1, p + half);
            float nSum = 0.0f; int nCnt = 0;
            for (int n = lo; n <= hi; ++n)
            {
                if (std::abs (n - p) < 2) continue;
                nSum += meanDb[(size_t) n];
                ++nCnt;
            }
            const float neighbourDb = nCnt > 0 ? nSum / (float) nCnt : c - 10.0f;
            const float excess = c - neighbourDb;

            if (excess < 4.0f)   // same requirement as the resonance detector (lowered 7 -> 4 dB)
                continue;

            // -3 dB bandwidth (on the smoothed bwDb): walk outward until the
            // level has dropped 3 dB from the peak.
            const float halfLevel = bwDb[(size_t) p] - 3.0f;
            int lp = p, rp = p;
            while (lp > 0            && bwDb[(size_t) (lp - 1)] > halfLevel && (p - lp) < half) --lp;
            while (rp < numPoints - 1 && bwDb[(size_t) (rp + 1)] > halfLevel && (rp - p) < half) ++rp;

            const float fLo = pointFreq ((float) lp);
            const float fHi = pointFreq ((float) rp);
            const float fC  = pointFreq ((float) p);
            const float bw  = juce::jmax (1.0f, fHi - fLo);
            const float q   = juce::jlimit (2.0f, MSEQ8AudioProcessor::maxQForFreq (fC), fC / bw);   // matches the band's effective (frequency-dependent) Q ceiling

            peaks.push_back ({ p, fC, excess, c, q, side, lp, rp });
        }
    }

    // Strongest excess first.
    std::sort (peaks.begin(), peaks.end(),
              [] (const Peak& a, const Peak& b) { return a.excessDb > b.excessDb; });

    // Dedup based on actual -3dB overlap (not a fixed point distance): two
    // narrow, distinct resonances close together in frequency should be able
    // to become two separate narrow suggestions instead of the weaker one
    // being discarded or merged into a wide one - several narrow filters are
    // usually better than one wide reduction that takes out sound you want
    // to keep between the peaks.
    struct Range { int lo, hi; };
    std::vector<Range> usedRanges;
    for (auto& pk : peaks)
    {
        if ((int) proposals.size() >= 6)   // up to 6 of 8 bands, at least 2 free for manual work
            break;

        bool tooClose = false;
        for (auto& ur : usedRanges)
        {
            // A small safety margin (1 point) so bands don't end up
            // completely back to back and produce comb-filter artefacts.
            if (pk.lp <= ur.hi + 1 && pk.rp >= ur.lo - 1) { tooClose = true; break; }
        }
        if (tooClose)
            continue;

        usedRanges.push_back ({ pk.lp, pk.rp });

        Proposal pr;
        pr.point    = pk.point;
        pr.freq     = pk.freq;
        pr.side     = pk.side;
        pr.q        = pk.q;

        // Cut = ~60% of the excess, never all of it (would sound dead), 2-9 dB.
        pr.cutDb    = -juce::jlimit (2.0f, 9.0f, pk.excessDb * 0.6f);

        // Threshold just below the resonance's own level so the band only
        // acts when it's ringing. NOTE: levelDb carries the detector's
        // perceptual weighting (~+/-a couple of dB against raw dBFS) - the
        // suggestion is a starting point, fine-tune with DELTA. The margin
        // is Q-dependent: narrow/tonal resonances stand out perceptually
        // even at a small excess (masking within a critical band is weaker
        // for pure tones than for broadband energy), so they get a tighter
        // margin (more consistent attenuation) - wide resonances get a
        // looser one (less risk of shaving broadband material too
        // aggressively).
        const float threshMarginDb = juce::jlimit (1.0f, 5.0f, 6.5f / std::sqrt (juce::jmax (0.5f, pk.q)));
        pr.threshDb = juce::jlimit (-60.0f, 0.0f, pk.levelDb - threshMarginDb);

        // Attack: same physics as release below - the detector's bandpass
        // filter has a settling time proportional to Q/f (a time constant
        // tau ~= Q/(pi*f), not the full t60), so narrow/high-Q resonances
        // need a longer attack for the envelope to become a meaningful
        // measurement before the gain moves. Takes the larger of "two
        // periods" (a pure period-based floor, as before) and the Q/f term,
        // so the low-Q case behaves as it did previously.
        const float attPeriods = 2.0f * 1000.0f / pk.freq;
        const float attQTerm   = 1000.0f * pk.q / (juce::MathConstants<float>::pi * pk.freq);
        pr.attMs    = juce::jlimit (0.1f, 100.0f, juce::jmax (attPeriods, attQTerm));

        // Release from physical decay: t60 ~= 2.2*Q/f (seconds -> ms).
        pr.relMs    = juce::jlimit (20.0f, 1000.0f, 2.2f * pk.q / pk.freq * 1000.0f);

        proposals.push_back (pr);
    }
}

void EQGraphComponent::applyProposals()
{
    if (proposals.empty())
    {
        analyzeState = AnalyzeState::idle;
        return;
    }

    preApplyState = processor.apvts.copyState().createCopy();

    auto setParam = [this] (const juce::String& id, float v)
    {
        if (auto* p = processor.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    };

    // "Free band" = untouched: no dynamics, no gain, and not bypassed. Bands
    // the user has already set up are left alone (guide section 9).
    auto bandIsFree = [this] (int b)
    {
        const bool hasDyn  = processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::dynRangeID (b))->load() != 0.0f;
        const bool hasGain = std::abs (processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::gainID (b))->load()) > 0.01f;
        const bool bypassed = processor.apvts.getRawParameterValue (MSEQ8AudioProcessor::bypassID (b))->load() > 0.5f;
        return ! hasDyn && ! hasGain && ! bypassed;
    };

    int nextBand = 0;
    for (auto& pr : proposals)
    {
        while (nextBand < MSEQ8AudioProcessor::numBands && ! bandIsFree (nextBand))
            ++nextBand;

        if (nextBand >= MSEQ8AudioProcessor::numBands)
            break;

        setParam (MSEQ8AudioProcessor::freqID (nextBand), pr.freq);
        setParam (MSEQ8AudioProcessor::qID (nextBand), pr.q);
        setParam (MSEQ8AudioProcessor::gainID (nextBand), 0.0f);
        setParam (MSEQ8AudioProcessor::typeID (nextBand), (float) MSEQ8AudioProcessor::typeBell);
        setParam (MSEQ8AudioProcessor::modeID (nextBand),
                  (float) (pr.side ? MSEQ8AudioProcessor::modeSide : MSEQ8AudioProcessor::modeMid));
        setParam (MSEQ8AudioProcessor::bypassID (nextBand), 0.0f);
        setParam (MSEQ8AudioProcessor::dynThreshID (nextBand), pr.threshDb);
        setParam (MSEQ8AudioProcessor::dynRangeID (nextBand), pr.cutDb);
        setParam (MSEQ8AudioProcessor::dynAttID (nextBand), pr.attMs);
        setParam (MSEQ8AudioProcessor::dynRelID (nextBand), pr.relMs);

        ++nextBand;
    }

    analyzeState = AnalyzeState::applied;
    appliedAtMs = juce::Time::getMillisecondCounterHiRes();
    setAnalyzeMessage ("Applied - click UNDO to revert");
}

void EQGraphComponent::undoApply()
{
    if (preApplyState.isValid())
        processor.apvts.replaceState (preApplyState.createCopy());

    analyzeState = AnalyzeState::idle;
    proposals.clear();
    setAnalyzeMessage ({});
}

void EQGraphComponent::setAnalyzeMessage (const juce::String& msg)
{
    analyzeMsg = msg;
    analyzeMsgUntilMs = msg.isEmpty() ? 0.0 : juce::Time::getMillisecondCounterHiRes() + 4000.0;
}

juce::Rectangle<int> EQGraphComponent::chipBounds (int index) const
{
    return { 12 + index * 96, 26, 90, 18 };
}

void EQGraphComponent::drawProposals (juce::Graphics& g) const
{
    if (proposals.empty())
        return;

    const auto area = plotArea (getLocalBounds().toFloat());
    const float dashLengths[] = { 3.0f, 3.0f };

    for (auto& pr : proposals)
    {
        const float x = freqToX (pr.freq);
        const auto colour = (pr.side ? Theme::side : Theme::mid);

        // Faint dashed vertical reference line
        juce::Path line;
        line.startNewSubPath (x, area.getY());
        line.lineTo (x, area.getBottom());
        juce::PathStrokeType dstroke (1.0f);
        juce::Path dashed;
        dstroke.createDashedStroke (dashed, line, dashLengths, 2);
        g.setColour (colour.withAlpha (0.4f));
        g.strokePath (dashed, dstroke);

        // Ring at the 0 dB line at the suggested frequency
        const float yTop = gainToY (0.0f);
        g.setColour (colour.withAlpha (0.9f));
        g.drawEllipse (x - 6.0f, yTop - 6.0f, 12.0f, 12.0f, 2.0f);

        // Downward arrow showing the suggested cut (the depth scales the arrow's length)
        const float arrowLen = juce::jlimit (14.0f, 60.0f, std::abs (pr.cutDb) * 6.0f);
        const float yArrowTop = yTop + 8.0f;
        const float yArrowEnd = yArrowTop + arrowLen;
        juce::Path arrow;
        arrow.startNewSubPath (x, yArrowTop);
        arrow.lineTo (x, yArrowEnd);
        arrow.lineTo (x - 4.0f, yArrowEnd - 5.0f);
        arrow.startNewSubPath (x, yArrowEnd);
        arrow.lineTo (x + 4.0f, yArrowEnd - 5.0f);
        g.strokePath (arrow, juce::PathStrokeType (2.0f));
    }
}

void EQGraphComponent::drawAnalyzeChips (juce::Graphics& g) const
{
    if (! analyzeMsg.isEmpty())
    {
        // The message used to span all the way to the right edge, straight
        // over APPLY/DISCARD (proposal mode) or UNDO (applied mode) drawn in
        // drawLegend(). Limit the right edge to those buttons' left edge in
        // each respective state so they never collide.
        int rightLimit = getWidth() - 8;
        if (analyzeState == AnalyzeState::proposal)
            rightLimit = applyButtonBounds (getLocalBounds()).getX() - 10;
        else if (analyzeState == AnalyzeState::applied)
            rightLimit = undoButtonBounds (getLocalBounds()).getX() - 10;

        g.setColour (Theme::mid);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (analyzeMsg,
                    juce::Rectangle<int> (220, 0, juce::jmax (10, rightLimit - 220), 26),
                    juce::Justification::centredRight);
    }

    for (int i = 0; i < (int) proposals.size(); ++i)
    {
        auto b = chipBounds (i);
        auto& pr = proposals[(size_t) i];

        g.setColour (Theme::panelLight);
        g.fillRoundedRectangle (b.toFloat(), 4.0f);
        g.setColour ((pr.side ? Theme::side : Theme::mid).withAlpha (0.6f));
        g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 4.0f, 1.0f);

        g.setColour (Theme::text);
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        // Frequency, Q, depth, channel (guide section 9)
        g.drawText (formatFreq (pr.freq) + " Q" + juce::String (pr.q, 1) + " "
                        + juce::String (pr.cutDb, 1) + "dB " + (pr.side ? "S" : "M"),
                    b, juce::Justification::centred);
    }
}

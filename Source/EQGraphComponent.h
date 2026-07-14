#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"

#include <array>
#include <functional>
#include <vector>

//==============================================================================
/** Frequency graph:

    - Axes: 20 Hz-20 kHz (log-x) with Hz markings, dB-y with markings. Mouse
      wheel over the y-axis zone (left edge) zooms the dB scale (+/-6...+/-30,
      visual only).
    - Three spectra (post-EQ): mid (green), side (orange), and STEREO (the
      effective sum sqrt(mid^2+side^2), purple-grey, off by default). +3 dB/oct
      display tilt. Clickable legend.
    - FREEZE: freezes a snapshot of the visible spectra as reference contours.
    - Update rate FAST/MED/SLOW (clickable, saved in state).
    - Resonance detection (RES): vertical markers, labels on a fixed row near
      the top.
    - Crosshair with frequency + note + cents on a fixed row near the top of
      the window.
    - Hover on a node: readout with the band's values; onBandHover notifies
      the editor.
    - Mouse wheel over a band node adjusts Q. Double-click on an HP/LP node
      toggles on/off.
    - On window resize, FFT/spectrum/text rendering pauses and resumes ~200 ms
      after the last resize event (audio processing is never affected).
*/
class EQGraphComponent : public juce::Component,
                         private juce::Timer
{
public:
    explicit EQGraphComponent (MSEQ8AudioProcessor& proc);
    ~EQGraphComponent() override;

    /** Called when the hovered band changes: 0..7, or -1 for none. */
    std::function<void (int)> onBandHover;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown        (const juce::MouseEvent&) override;
    void mouseDrag        (const juce::MouseEvent&) override;
    void mouseUp          (const juce::MouseEvent&) override;
    void mouseMove        (const juce::MouseEvent&) override;
    void mouseExit        (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove   (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    //==========================================================================
    // Node index: 0..7 = band, 8 = HP (mid, or shared if not independent),
    // 9 = LP (mid/shared), 10 = HP side (only in independent mode), 11 = LP
    // side (only in independent mode).
    static constexpr int hpNode = MSEQ8AudioProcessor::numBands;
    static constexpr int lpNode = hpNode + 1;
    static constexpr int hpSideNode = lpNode + 1;
    static constexpr int lpSideNode = hpSideNode + 1;
    static constexpr int numNodes = lpSideNode + 1;

    enum SpectrumIndex { specMid = 0, specSide, specStereo, numSpectra };

    // Legend entries
    enum LegendItem { legMid = 0, legSide, legStereo, legRes, legSpeed, legFreeze,
                      legAnalyze, numLegendItems };

    static constexpr int numPoints = 320;

    struct SpectrumDisplay
    {
        std::array<float, numPoints> levels {};    // 0..1, SPEED-smoothed levels (attack/release, time)
        std::array<float, numPoints> targets {};   // 0..1, target from the latest FFT block
        std::array<float, numPoints> visLevels {}; // 0..1, levels + frequency smoothing (neighbour points) - drawing only
        double lastDataMs = 0.0;                   // when targets were last updated
        bool visible = true;
    };

    //==========================================================================
    void timerCallback() override;

    /** Runs FFT on the tap if a new block is available. Writes two variants
        per graph point: magsMax (max over the point's bin range - used by
        the RES/Find Resonances detector, which wants to preserve sensitivity
        to narrow peaks) and magsAvg (power/RMS average over the same range -
        used by the drawn spectrum curve, much smoother than max for wide
        points). Returns true if new data was available. */
    bool processTap (MSEQ8AudioProcessor::SpectrumTap&, juce::uint32& lastSeen,
                     std::array<float, numPoints>& magsMax,
                     std::array<float, numPoints>& magsAvg);

    /** Sets new targets from magnitudes (tilt + normalisation). */
    void setDisplayTargets (SpectrumDisplay&, const std::array<float, numPoints>& mags,
                            double nowMs);

    /** Time-based smoothing toward targets: attack interpolation upward,
        exponential decay downward. Frame-rate independent. */
    void smoothDisplay (SpectrumDisplay&, float dt, double nowMs);

    void updateResonances (float dt);
    float attackTau() const;
    float releaseTau() const;
    void toggleFreeze();

    /** Mono compatibility: flags (with hysteresis/persistence) if the side
        signal is on average clearly stronger than mid below 500 Hz, which
        risks phase cancellation on mono downmix. Reuses detDbMid/Side - no
        dedicated detector. */
    void updateMonoCompat (float dt);

    void drawGrid          (juce::Graphics&) const;
    void drawAxisLabels    (juce::Graphics&) const;
    void drawSpectrumFill  (juce::Graphics&, const SpectrumDisplay&, juce::Colour) const;
    void drawFrozen        (juce::Graphics&) const;
    void drawResonances    (juce::Graphics&) const;
    void drawLegend        (juce::Graphics&) const;
    void drawNodes         (juce::Graphics&) const;
    void drawHoverReadout  (juce::Graphics&) const;
    void drawCursorInfo    (juce::Graphics&) const;
    void drawMonoWarning   (juce::Graphics&) const;
    void drawAuditionMarkers (juce::Graphics&) const;

    void buildCurve (juce::Path& path, bool sideChannel) const;

    float freqToX (float freq) const;
    float xToFreq (float x) const;
    float gainToY (float dB) const;
    float yToGain (float y) const;

    static float pointFreq (float pointIndex);
    static juce::String noteName (float freq, bool withCents);
    static juce::String formatFreq (float freq);

    juce::Point<float> nodePosition (int node) const;
    int findNodeAt (juce::Point<float> pos) const;
    juce::String legendItemLabel (int item) const;
    juce::Rectangle<int> legendItemBounds (int item) const;
    int findLegendItemAt (juce::Point<int> pos) const;

    void showCutMenu (bool isHighPass, bool sideVariant = false);
    float paramValue (const juce::String& id) const;
    void  setParamValue (const juce::String& id, float value);

    //==========================================================================
    MSEQ8AudioProcessor& processor;

    // Dedicated LookAndFeel for the standalone PopupMenu/CallOutBox created by
    // the graph (the HP/LP menu, the dynamics panel). Must be instance-bound -
    // NOT juce::LookAndFeel::setDefaultLookAndFeel(), which is a process-global
    // pointer. With multiple plugin instances in the same host process (e.g.
    // FL Studio), each instance would overwrite the same global pointer in its
    // constructor and null it in its destructor, which could leave other
    // instances' still-open windows without a valid LookAndFeel and caused
    // severe CPU load/UI hangs as soon as more than one instance was open.
    KnobLookAndFeel menuLnf;

    int draggedNode = -1;
    int hoveredNode = -1;

    juce::Point<float> cursorPos;
    bool cursorValid = false;

    float displayMaxDb = 18.0f;

    int  speedIndex = 1;          // 0=fast, 1=med, 2=slow
    bool resEnabled = false;

    // Resize pause
    bool renderPaused = false;
    juce::uint32 lastResizeMs = 0;

    // Focus-based throttling: with multiple plugin windows open at once they
    // often share the host's UI thread. An unfocused window runs at a
    // reduced rate (see timerCallback()) instead of the full 30 Hz, except
    // during an ongoing Find Resonances listening pass, which always needs
    // to run at full rate.
    int backgroundSkipCounter = 0;

    juce::dsp::FFT fft { MSEQ8AudioProcessor::fftOrder };
    juce::dsp::WindowingFunction<float> fftWindow { MSEQ8AudioProcessor::fftSize,
                                                    juce::dsp::WindowingFunction<float>::hann };
    std::array<float, MSEQ8AudioProcessor::fftSize * 2> fftBuffer {};

    // Linear magnitudes per graph point (latest FFT block per tap)
    // Detection (max, unchanged sensitivity) vs. display (average, smoother curve)
    std::array<float, numPoints> midMagsDet {},  sideMagsDet {};
    std::array<float, numPoints> midMagsDisp {}, sideMagsDisp {}, stereoMagsDisp {};

    // Seqlock sequence number per tap + frame timing for dt-based smoothing
    juce::uint32 tapSeqMid = 0, tapSeqSide = 0;
    double lastFrameMs = 0.0;

    SpectrumDisplay spectra[numSpectra];

    // Freeze reference
    bool frozenActive = false;
    std::array<float, numPoints> frozenLevels[numSpectra] {};
    bool frozenVisible[numSpectra] {};

    //==========================================================================
    // Resonance detection: top 3 per channel (mid/side) with hysteresis.
    // Pipeline (steps 1-5):
    //   1. Dedicated RMS detector spectrum (~400 ms power average, independent of SPEED)
    //   2. ERB-scaled neighbourhood windows (critical bands)
    //   3. Frequency stability: parabolic peak-offset jitter gate against wandering peaks
    //   4. Masking gate: peaks below the neighbouring energy's masking threshold are ignored
    //   5. Harmonic discrimination: a subharmonic check penalises musical overtones
    struct Resonance { int point; float level; bool side; };

    void updateDetector (bool sideChannel, float dt);   // step 1 (reads rmsScratch)
    void detectChannelResonances (bool isSide, float dt);
    static int pointForFreq (float freq);

    std::array<float, numPoints> resScoreMid {}, resScoreSide {};
    std::vector<int> prevMidPoints, prevSidePoints;
    std::vector<Resonance> resonances;

    // Step 1: detector spectrum
    std::array<float, numPoints> rmsScratch {};                 // filled by processTap
    std::array<float, numPoints> detPowMid {},  detPowSide {};  // power EMA
    std::array<float, numPoints> detDbMid {},   detDbSide {};   // ~dBFS, no tilt
    std::array<float, numPoints> detLevMid {},  detLevSide {};  // 0..1, with tilt

    // Step 2: ERB half-windows in graph points, per point
    std::array<int, numPoints> nbHalf {};

    // Step 3: peak-offset jitter per point
    std::array<float, numPoints> jitterMid {}, jitterSide {};
    std::array<float, numPoints> lastOffMid {}, lastOffSide {};

    // Perceptual weighting (normalised dB weight per graph point, ~80 phon)
    std::array<float, numPoints> resWeight {};
    static float perceptualWeightDb (float freq);

    // Mono compatibility: persistence/hysteresis for the warning (see updateMonoCompat)
    float monoWarnTime = 0.0f;
    bool  monoWarnActive = false;

    //==========================================================================
    // Find Resonances assistant: listens for ~8 s, measures bandwidth (Q),
    // severity (cut), and level (threshold) for the most problematic
    // resonances and proposes dynamic bands. APPLY writes to free bands;
    // UNDO restores.
    enum class AnalyzeState { idle, listening, proposal, applied };

    struct Proposal { float freq, q, cutDb, threshDb, attMs, relMs; bool side; int point; };

    void startAnalyze();
    void cancelAnalyze();
    void finishListening();
    void buildProposals();
    void applyProposals();
    void undoApply();
    void setAnalyzeMessage (const juce::String&);
    juce::Rectangle<int> chipBounds (int index) const;
    void drawProposals (juce::Graphics&) const;
    void drawAnalyzeChips (juce::Graphics&) const;

    AnalyzeState analyzeState = AnalyzeState::idle;
    double analyzeStartMs = 0.0;
    static constexpr double analyzeDurMs = 8000.0;

    // The UNDO button auto-hides a while after Apply (doesn't touch the
    // applied parameters, just the affordance) - see timerCallback().
    double appliedAtMs = 0.0;
    static constexpr double undoVisibleMs = 10000.0;

    std::array<float, numPoints> flagTimeMid {}, flagTimeSide {};
    std::array<double, numPoints> sumLevMid {}, sumLevSide {};
    std::array<double, numPoints> sumDbMid {},  sumDbSide {};
    double analyzeElapsed = 0.0;
    int   analyzeFrames = 0;
    float analyzePeakLevel = 0.0f;

    std::vector<Proposal> proposals;
    juce::ValueTree preApplyState;

    juce::String analyzeMsg;
    double analyzeMsgUntilMs = 0.0;

    static constexpr float minFreq = 20.0f, maxFreq = 20000.0f;
    static constexpr float nodeRadius = 6.0f;
    static constexpr int   yAxisZone = 44;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQGraphComponent)
};

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

//==============================================================================
/** 8-band parametric EQ with Mid/Side processing + HP/LP filters.

    Signal flow per block:
      L/R -> encode: mid=(L+R)*0.5, side=(L-R)*0.5
           -> [spectrum tap: mid/side PRE]
           -> per band: peak filter on mid, side, or both (per the band's M/S mode)
           -> HP/LP (Butterworth 12/24/48 dB/oct, routing Stereo/Mid/Side)
           -> [spectrum tap: mid/side POST]
           -> decode: L=mid+side, R=mid-side
*/
class MSEQ8AudioProcessor : public juce::AudioProcessor,
                            private juce::AudioProcessorValueTreeState::Listener
{
public:
    static constexpr int numBands = 8;

    enum BandMode { modeMid = 0, modeMidSide = 1, modeSide = 2 };
    enum CutMode  { cutStereo = 0, cutMid = 1, cutSide = 2 };
    enum BandType { typeBell = 0, typeLowShelf = 1, typeHighShelf = 2, typeNotch = 3 };

    MSEQ8AudioProcessor();
    ~MSEQ8AudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                       { return true; }

    const juce::String getName() const override           { return "MSEQ 8"; }
    bool acceptsMidi() const override                     { return false; }
    bool producesMidi() const override                    { return false; }
    double getTailLengthSeconds() const override          { return 0.0; }

    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Parameter ID helpers
    static juce::String freqID   (int band)  { return "band" + juce::String (band) + "_freq"; }
    static juce::String gainID   (int band)  { return "band" + juce::String (band) + "_gain"; }
    static juce::String qID      (int band)  { return "band" + juce::String (band) + "_q"; }
    static juce::String modeID   (int band)  { return "band" + juce::String (band) + "_mode"; }
    static juce::String bypassID (int band)  { return "band" + juce::String (band) + "_bypass"; }
    static juce::String typeID   (int band)  { return "band" + juce::String (band) + "_type"; }

    // Dynamic EQ per band: threshold (dB), range (dB, 0 = static band),
    // attack and release (ms) for the envelope detector
    static juce::String dynThreshID (int band) { return "band" + juce::String (band) + "_dyn_thresh"; }
    static juce::String dynRangeID  (int band) { return "band" + juce::String (band) + "_dyn_range"; }
    static juce::String dynAttID    (int band) { return "band" + juce::String (band) + "_dyn_att"; }
    static juce::String dynRelID    (int band) { return "band" + juce::String (band) + "_dyn_rel"; }

    // HP/LP: "hp_on", "hp_freq", "hp_slope" (12/24/48), "hp_mode" (Stereo/Mid/Side); same for "lp_*"

    juce::AudioProcessorValueTreeState apvts;

    //==============================================================================
    // Meters (peak, read by the UI via a Timer)
    std::atomic<float> inLevel  { 0.0f };
    std::atomic<float> outLevel { 0.0f };
    std::atomic<float> inRms  { 0.0f };
    std::atomic<float> outRms { 0.0f };

    double getCurrentSampleRate() const noexcept { return currentSampleRate; }

    //==============================================================================
    // Spectrum taps: mid/side AFTER the EQ + HP/LP (before decode and output
    // gain), so the UI shows the result of the filtering in real time. The
    // processor fills a FIFO per tap; the UI grabs a block once ready is set
    // and runs the FFT itself.
    static constexpr int fftOrder = 12;
    static constexpr int fftSize  = 1 << fftOrder;   // 4096

    /** Ring buffer with 75% overlap: a new FFT block is published every
        (fftSize/4) samples (~43 blocks/s @ 44.1 kHz) instead of every
        fftSize samples — smoother updates without losing bass resolution.
        Publishing uses a seqlock (odd sequence number = write in progress)
        so the UI can always fetch the latest complete block. */
    struct SpectrumTap
    {
        static constexpr int hopSize = fftSize / 4;

        void push (float sample) noexcept
        {
            ring[writePos] = sample;
            writePos = (writePos + 1) & (fftSize - 1);

            if (++hopCounter >= hopSize)
            {
                hopCounter = 0;

                seq.fetch_add (1, std::memory_order_acq_rel);
                const int tail = fftSize - writePos;
                std::memcpy (snapshot,        ring + writePos, sizeof (float) * (size_t) tail);
                std::memcpy (snapshot + tail, ring,            sizeof (float) * (size_t) writePos);
                seq.fetch_add (1, std::memory_order_acq_rel);
            }
        }

        /** Copies the latest block (in time order) to dest. Returns false if
            there was nothing new, or if a write was in progress (try again
            next frame). */
        bool readLatest (float* dest, juce::uint32& lastSeen) noexcept
        {
            const auto s1 = seq.load (std::memory_order_acquire);
            if ((s1 & 1u) != 0 || s1 == lastSeen || s1 == 0)
                return false;

            std::memcpy (dest, snapshot, sizeof (float) * fftSize);

            if (seq.load (std::memory_order_acquire) != s1)
                return false;   // overwritten while reading

            lastSeen = s1;
            return true;
        }

    private:
        float ring[fftSize] = {};
        float snapshot[fftSize] = {};
        int writePos = 0, hopCounter = 0;
        std::atomic<juce::uint32> seq { 0 };
    };

    SpectrumTap tapMid, tapSide;

    // Effective band gain in dB (static + dynamic), for the graph's ghost nodes
    std::atomic<float> dynGainMid[numBands] {}, dynGainSide[numBands] {};

    // Ctrl+hover audition: set directly by the UI (EQGraphComponent::timerCallback),
    // -1 = off. No APVTS parameter - shouldn't be automated or saved in
    // presets, just a temporary "listen to this band's frequency range" mode
    // that isolates a bandpass around the band's freq/Q in the final L/R
    // signal, without affecting the actual EQ curve.
    std::atomic<int> auditionBand { -1 };

    //==============================================================================
    // Undo/redo (5 steps): whole APVTS state snapshots, the same pattern
    // the A/B/C/D slots and Find Resonances' Apply/Undo already use - not
    // fine-grained per-parameter tracking. An AudioProcessorValueTreeState::
    // Listener is registered for ALL parameters; parameterChanged() only
    // does a lock-free atomic increment (thread-safe regardless of which
    // thread calls it - the audio thread on host automation, the message
    // thread on GUI changes). The heavy work (ValueTree copying,
    // replaceState) happens in pushUndoSnapshot()/undo()/redo(), which the
    // EDITOR is responsible for only calling from the message thread - see
    // PluginEditor::timerCallback(), which debounces (~400 ms with no new
    // changes) before committing a snapshot, so one continuous knob/node
    // drag becomes ONE undo step.
    std::atomic<uint32_t> paramChangeGeneration { 0 };

    void pushUndoSnapshot();
    void undo();
    void redo();
    bool canUndo() const noexcept { return undoIndex > 0; }
    bool canRedo() const noexcept { return undoIndex >= 0 && undoIndex < (int) undoHistory.size() - 1; }

    //==============================================================================
    // A/B/C/D snapshots
    static constexpr int numSlots = 4;
    void storeCurrentSlot();
    void switchToSlot (int newSlot);
    int  getActiveSlot() const noexcept { return activeSlot; }

    //==============================================================================
    // Factory presets
    juce::StringArray getPresetNames() const;
    void applyPreset (int index);

    // User presets (XML files in the user's appdata folder)
    juce::File getUserPresetFolder() const;
    juce::Array<juce::File> getUserPresetFiles() const;
    bool saveUserPreset (const juce::String& name);
    void loadUserPreset (const juce::File& file);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void updateFilters();
    void updateCutFilters();

    // AudioProcessorValueTreeState::Listener - see paramChangeGeneration above.
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    static constexpr int maxUndoSteps = 5;
    std::vector<juce::ValueTree> undoHistory;
    int  undoIndex = -1;
    bool restoringState = false;   // true during undo()/redo() so we don't accidentally record our own restore

    /** Processes one band's filter on one channel, with dynamics if range != 0.
        The dynamics measure the level in the band's frequency range (a
        bandpass detector), compute the effective gain per sub-block, and
        update the coefficients allocation-free. */
    void processBandChannel (int band, float* data, int numSamples, bool sideChannel);

    // One mono filter per band, for mid and side respectively
    juce::dsp::IIR::Filter<float> midFilters[numBands];
    juce::dsp::IIR::Filter<float> sideFilters[numBands];

    // HP/LP: up to 4 cascaded biquad sections (48 dB/oct) per channel.
    // hpMid/lpMid and hpSide/lpSide are already separate filters per channel -
    // in "independent" mode (hp_independent/lp_independent) they're simply
    // configured with DIFFERENT freq/slope (hp_side_freq/hp_side_slope etc.)
    // instead of the same values as before, hence the separate *Sections
    // counters (different slope -> different number of cascaded sections).
    static constexpr int maxCutSections = 4;
    juce::dsp::IIR::Filter<float> hpMid[maxCutSections], hpSide[maxCutSections];
    juce::dsp::IIR::Filter<float> lpMid[maxCutSections], lpSide[maxCutSections];
    int hpSections = 1, lpSections = 1;
    int hpSideSections = 1, lpSideSections = 1;
    float lastHpFreq = -1.0f, lastLpFreq = -1.0f;
    int   lastHpSlope = -1,   lastLpSlope = -1;
    float lastHpSideFreq = -1.0f, lastLpSideFreq = -1.0f;
    int   lastHpSideSlope = -1,   lastLpSideSlope = -1;
    bool  lastHpIndependent = false, lastLpIndependent = false;

    // Cache for band coefficients
    float lastFreq[numBands] {}, lastGain[numBands] {}, lastQ[numBands] {};
    float lastRange[numBands] {};   // forces a detector refresh when a band becomes/stops being dynamic
    bool  coeffsDirty = true;

    // Parameter smoothing (~20 ms) per channel and band: Freq/Q
    // multiplicatively, Gain linearly in dB. Coefficients are rewritten per
    // sub-block while ramping.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        smFreq[2][numBands], smQ[2][numBands];
    juce::SmoothedValue<float> smGainDb[2][numBands];

    // Dynamic EQ: detectors (bandpass), envelopes, and last effective gain
    static constexpr int dynSubBlock = 32;
    juce::dsp::IIR::Filter<float> detMid[numBands], detSide[numBands];
    float envMid[numBands] {}, envSide[numBands] {};
    float lastEffGainMid[numBands] {}, lastEffGainSide[numBands] {};

    // Per-band envelope coefficients (attack/release), cached from the ms parameters
    float envAttC[numBands] {}, envRelC[numBands] {};
    float lastAttMs[numBands] {}, lastRelMs[numBands] {};

    // Raw parameter pointers
    std::atomic<float>* pFreq[numBands] {};
    std::atomic<float>* pGain[numBands] {};
    std::atomic<float>* pQ[numBands] {};
    std::atomic<float>* pMode[numBands] {};
    std::atomic<float>* pBandBypass[numBands] {};
    std::atomic<float>* pDynThresh[numBands] {};
    std::atomic<float>* pDynRange[numBands] {};
    std::atomic<float>* pDynAtt[numBands] {};
    std::atomic<float>* pDynRel[numBands] {};
    std::atomic<float>* pType[numBands] {};
    int lastType[numBands] {};
    std::atomic<float>* pGlobalBypass = nullptr;
    std::atomic<float>* pHpOn = nullptr, *pHpFreq = nullptr, *pHpSlope = nullptr, *pHpMode = nullptr;
    std::atomic<float>* pHpIndependent = nullptr, *pHpSideFreq = nullptr, *pHpSideSlope = nullptr;
    std::atomic<float>* pLpOn = nullptr, *pLpFreq = nullptr, *pLpSlope = nullptr, *pLpMode = nullptr;
    std::atomic<float>* pLpIndependent = nullptr, *pLpSideFreq = nullptr, *pLpSideSlope = nullptr;
    std::atomic<float>* pOutGain = nullptr;
    float lastOutGain = 1.0f;

    // Monitor: 0 = Stereo, 1 = Mid solo, 2 = Side solo (mono in both ears).
    // The decode coefficients are smoothed for click-free switching:
    //   L = a*mid + b*side, R = c*mid + d*side
    std::atomic<float>* pMonitor = nullptr;
    juce::SmoothedValue<float> smMidL, smSideL, smMidR, smSideR;

    // Delta listening: hear what was REMOVED (dry - wet), crossfaded ~10 ms.
    // Applied in the M/S domain before the monitor decode, so mid/side solo
    // of the delta works. Output gain is applied afterward (the listening
    // level is controlled as usual).
    std::atomic<float>* pDelta = nullptr;
    juce::SmoothedValue<float> smDelta;
    juce::AudioBuffer<float> dryMidBuffer, drySideBuffer;

    // Ctrl+hover audition (see auditionBand above): a bandpass on the final
    // L/R signal, crossfaded ~15 ms to avoid clicks on on/off.
    juce::dsp::IIR::Filter<float> auditionFilterL, auditionFilterR;
    juce::SmoothedValue<float> smAudition;
    int   lastAuditionBand = -1;
    float lastAuditionFreq = -1.0f, lastAuditionQ = -1.0f;

    juce::AudioBuffer<float> midBuffer, sideBuffer;
    double currentSampleRate = 44100.0;

    // A/B/C/D
    juce::ValueTree snapshots[numSlots];
    int activeSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MSEQ8AudioProcessor)
};

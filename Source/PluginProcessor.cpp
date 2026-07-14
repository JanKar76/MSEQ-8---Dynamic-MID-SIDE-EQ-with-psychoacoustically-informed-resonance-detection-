#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cstring>

//==============================================================================
namespace
{
    constexpr float defaultFreqs[MSEQ8AudioProcessor::numBands] =
        { 60.0f, 150.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6500.0f, 12000.0f };

    // Allocation-free coefficient updates (safe on the audio thread).
    // ArrayCoefficients returns {b0,b1,b2,a0,a1,a2}; Filter stores normalised
    // {b0,b1,b2,a1,a2} (divided by a0).
    void assignBiquad (juce::dsp::IIR::Filter<float>& filter, const std::array<float, 6>& c)
    {
        const float inv = 1.0f / c[3];
        auto* raw = filter.coefficients->getRawCoefficients();
        raw[0] = c[0] * inv;
        raw[1] = c[1] * inv;
        raw[2] = c[2] * inv;
        raw[3] = c[4] * inv;
        raw[4] = c[5] * inv;
    }

    void setPeakCoefficients (juce::dsp::IIR::Filter<float>& filter, double sampleRate,
                              float freq, float q, float gainDb)
    {
        assignBiquad (filter, juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (
            sampleRate, freq, q, juce::Decibels::decibelsToGain (gainDb)));
    }

    void setBandPassCoefficients (juce::dsp::IIR::Filter<float>& filter, double sampleRate,
                                  float freq, float q)
    {
        assignBiquad (filter, juce::dsp::IIR::ArrayCoefficients<float>::makeBandPass (
            sampleRate, freq, q));
    }

    // Writes coefficients for the band's selected filter type.
    // Shelf Q is clamped (0.3-1.2) so the transition is always civilised;
    // notch ignores gain.
    void setBandCoefficients (juce::dsp::IIR::Filter<float>& filter, double sampleRate,
                              int type, float freq, float q, float gainDb)
    {
        using AC = juce::dsp::IIR::ArrayCoefficients<float>;
        const float gain   = juce::Decibels::decibelsToGain (gainDb);
        const float shelfQ = juce::jlimit (0.3f, 1.2f, q);

        switch (type)
        {
            case MSEQ8AudioProcessor::typeLowShelf:
                assignBiquad (filter, AC::makeLowShelf  (sampleRate, freq, shelfQ, gain)); break;
            case MSEQ8AudioProcessor::typeHighShelf:
                assignBiquad (filter, AC::makeHighShelf (sampleRate, freq, shelfQ, gain)); break;
            case MSEQ8AudioProcessor::typeNotch:
                assignBiquad (filter, AC::makeNotch     (sampleRate, freq, q)); break;
            default:
                assignBiquad (filter, AC::makePeakFilter (sampleRate, freq, q, gain)); break;
        }
    }

    // Butterworth Q per biquad section for cascaded HP/LP
    // (replaces FilterDesign, which allocates): Q_k = 1/(2 cos(pi(2k+1)/2N))
    const float* butterworthQs (int slopeChoice, int& numSections)
    {
        static constexpr float q2[] = { 0.70710678f };
        static constexpr float q4[] = { 0.54119610f, 1.30656296f };
        static constexpr float q8[] = { 0.50979558f, 0.60134489f, 0.89997622f, 2.56291545f };

        switch (slopeChoice)
        {
            case 0:  numSections = 1; return q2;
            case 1:  numSections = 2; return q4;
            default: numSections = 4; return q8;
        }
    }
}

MSEQ8AudioProcessor::MSEQ8AudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    for (int i = 0; i < numBands; ++i)
    {
        pFreq[i]       = apvts.getRawParameterValue (freqID (i));
        pGain[i]       = apvts.getRawParameterValue (gainID (i));
        pQ[i]          = apvts.getRawParameterValue (qID (i));
        pMode[i]       = apvts.getRawParameterValue (modeID (i));
        pBandBypass[i] = apvts.getRawParameterValue (bypassID (i));
        pDynThresh[i]  = apvts.getRawParameterValue (dynThreshID (i));
        pDynRange[i]   = apvts.getRawParameterValue (dynRangeID (i));
        pDynAtt[i]     = apvts.getRawParameterValue (dynAttID (i));
        pDynRel[i]     = apvts.getRawParameterValue (dynRelID (i));
        pType[i]       = apvts.getRawParameterValue (typeID (i));
    }
    pGlobalBypass = apvts.getRawParameterValue ("global_bypass");

    pHpOn    = apvts.getRawParameterValue ("hp_on");
    pHpFreq  = apvts.getRawParameterValue ("hp_freq");
    pHpSlope = apvts.getRawParameterValue ("hp_slope");
    pHpMode  = apvts.getRawParameterValue ("hp_mode");
    pHpIndependent = apvts.getRawParameterValue ("hp_independent");
    pHpSideFreq    = apvts.getRawParameterValue ("hp_side_freq");
    pHpSideSlope   = apvts.getRawParameterValue ("hp_side_slope");

    pLpOn    = apvts.getRawParameterValue ("lp_on");
    pLpFreq  = apvts.getRawParameterValue ("lp_freq");
    pLpSlope = apvts.getRawParameterValue ("lp_slope");
    pLpMode  = apvts.getRawParameterValue ("lp_mode");
    pLpIndependent = apvts.getRawParameterValue ("lp_independent");
    pLpSideFreq    = apvts.getRawParameterValue ("lp_side_freq");
    pLpSideSlope   = apvts.getRawParameterValue ("lp_side_slope");

    pOutGain = apvts.getRawParameterValue ("output_gain");
    pMonitor = apvts.getRawParameterValue ("monitor");
    pDelta   = apvts.getRawParameterValue ("delta");

    // Initialise with real biquad coefficients so the filters' state size
    // (order 2) is correct even before prepare/reset.
    for (int i = 0; i < numBands; ++i)
    {
        midFilters[i].coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            44100.0, defaultFreqs[i], 1.0f, 1.0f);
        sideFilters[i].coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            44100.0, defaultFreqs[i], 1.0f, 1.0f);
        detMid[i].coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (
            44100.0, defaultFreqs[i], 1.0f);
        detSide[i].coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (
            44100.0, defaultFreqs[i], 1.0f);
    }

    for (int s = 0; s < maxCutSections; ++s)
    {
        hpMid[s].coefficients  = juce::dsp::IIR::Coefficients<float>::makeHighPass (44100.0, 20.0f);
        hpSide[s].coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (44100.0, 20.0f);
        lpMid[s].coefficients  = juce::dsp::IIR::Coefficients<float>::makeLowPass  (44100.0, 20000.0f);
        lpSide[s].coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass  (44100.0, 20000.0f);
    }

    for (int s = 0; s < numSlots; ++s)
        snapshots[s] = apvts.copyState().createCopy();

    // Undo/redo: listen to ALL parameters (not just a hand-picked list) so
    // any parameters added in the future are automatically covered too.
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.addParameterListener (rp->paramID, this);

    // Baseline - index 0 in the history is always "how the plugin looked at
    // instantiation/load", so you can undo all the way back to that.
    undoHistory.push_back (apvts.copyState().createCopy());
    undoIndex = 0;
}

MSEQ8AudioProcessor::~MSEQ8AudioProcessor()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.removeParameterListener (rp->paramID, this);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MSEQ8AudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < numBands; ++i)
    {
        const auto num = String (i + 1);

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { freqID (i), 1 }, "Band " + num + " Freq",
            NormalisableRange<float> (20.0f, 20000.0f, 0.01f, 0.25f),
            defaultFreqs[i],
            AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { gainID (i), 1 }, "Band " + num + " Gain",
            NormalisableRange<float> (-18.0f, 18.0f, 0.01f),
            0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { qID (i), 1 }, "Band " + num + " Q",
            NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.35f),
            1.0f));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { modeID (i), 1 }, "Band " + num + " M/S Mode",
            StringArray { "Mid", "Mid+Side", "Side" }, modeMidSide));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { typeID (i), 1 }, "Band " + num + " Type",
            StringArray { "Bell", "Low Shelf", "High Shelf", "Notch" }, typeBell));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { bypassID (i), 1 }, "Band " + num + " Bypass", false));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { dynThreshID (i), 1 }, "Band " + num + " Dyn Threshold",
            NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -30.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { dynRangeID (i), 1 }, "Band " + num + " Dyn Range",
            NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { dynAttID (i), 1 }, "Band " + num + " Dyn Attack",
            NormalisableRange<float> (0.1f, 100.0f, 0.1f, 0.35f), 5.0f,
            AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { dynRelID (i), 1 }, "Band " + num + " Dyn Release",
            NormalisableRange<float> (20.0f, 1000.0f, 1.0f, 0.4f), 150.0f,
            AudioParameterFloatAttributes().withLabel ("ms")));
    }

    // HP/LP
    const StringArray slopes  { "12 dB/oct", "24 dB/oct", "48 dB/oct" };
    const StringArray routing { "Stereo", "Mid", "Side" };

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "hp_on", 1 }, "HP On", false));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "hp_freq", 1 }, "HP Freq",
        NormalisableRange<float> (20.0f, 20000.0f, 0.01f, 0.25f), 30.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "hp_slope", 1 }, "HP Slope", slopes, 0));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "hp_mode", 1 }, "HP Routing", routing, cutStereo));

    // Independent mid/side HP: new, additive parameters (backward compatible
    // - old projects/presets lack them, falling back to default false/the
    // same values as hp_freq/hp_slope, i.e. identical behaviour to before
    // this feature existed).
    // Off: hp_freq/hp_slope control both channels (as before), routed via
    // hp_mode. On: hp_freq/hp_slope control MID, hp_side_freq/hp_side_slope
    // control SIDE, both always active (hp_mode is ignored - the whole point
    // is running different values simultaneously).
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "hp_independent", 1 }, "HP Independent Mid/Side", false));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "hp_side_freq", 1 }, "HP Side Freq",
        NormalisableRange<float> (20.0f, 20000.0f, 0.01f, 0.25f), 30.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "hp_side_slope", 1 }, "HP Side Slope", slopes, 0));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "lp_on", 1 }, "LP On", false));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "lp_freq", 1 }, "LP Freq",
        NormalisableRange<float> (20.0f, 20000.0f, 0.01f, 0.25f), 18000.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "lp_slope", 1 }, "LP Slope", slopes, 0));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "lp_mode", 1 }, "LP Routing", routing, cutStereo));

    // Independent mid/side LP: same reasoning as HP above.
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "lp_independent", 1 }, "LP Independent Mid/Side", false));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "lp_side_freq", 1 }, "LP Side Freq",
        NormalisableRange<float> (20.0f, 20000.0f, 0.01f, 0.25f), 18000.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "lp_side_slope", 1 }, "LP Side Slope", slopes, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "output_gain", 1 }, "Output Gain",
        NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "monitor", 1 }, "Monitor",
        StringArray { "Stereo", "Mid solo", "Side solo" }, 0));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "delta", 1 }, "Delta Listen", false));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "global_bypass", 1 }, "Bypass", false));

    return layout;
}

//==============================================================================
void MSEQ8AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 1 };

    for (int i = 0; i < numBands; ++i)
    {
        midFilters[i].prepare (spec);
        sideFilters[i].prepare (spec);
        midFilters[i].reset();
        sideFilters[i].reset();

        detMid[i].prepare (spec);   detMid[i].reset();
        detSide[i].prepare (spec);  detSide[i].reset();
        envMid[i] = envSide[i] = 0.0f;
        lastEffGainMid[i] = lastEffGainSide[i] = 1.0e9f;   // force the first update
    }

    // Per-band envelope coefficients: force recalculation from the ms parameters
    for (int i = 0; i < numBands; ++i)
        lastAttMs[i] = lastRelMs[i] = -1.0f;

    // Parameter smoothing ~20 ms, initialised at the current values
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < numBands; ++i)
        {
            smFreq[ch][i].reset (sampleRate, 0.02);
            smQ[ch][i].reset (sampleRate, 0.02);
            smGainDb[ch][i].reset (sampleRate, 0.02);
            smFreq[ch][i].setCurrentAndTargetValue (
                juce::jlimit (20.0f, (float) (sampleRate * 0.49), pFreq[i]->load()));
            smQ[ch][i].setCurrentAndTargetValue (pQ[i]->load());
            smGainDb[ch][i].setCurrentAndTargetValue (pGain[i]->load());
        }

    for (int s = 0; s < maxCutSections; ++s)
    {
        hpMid[s].prepare (spec);   hpMid[s].reset();
        hpSide[s].prepare (spec);  hpSide[s].reset();
        lpMid[s].prepare (spec);   lpMid[s].reset();
        lpSide[s].prepare (spec);  lpSide[s].reset();
    }

    midBuffer.setSize  (1, samplesPerBlock);
    sideBuffer.setSize (1, samplesPerBlock);
    dryMidBuffer.setSize  (1, samplesPerBlock);
    drySideBuffer.setSize (1, samplesPerBlock);

    // Delta crossfade ~10 ms
    smDelta.reset (sampleRate, 0.01);
    smDelta.setCurrentAndTargetValue (pDelta->load() > 0.5f ? 1.0f : 0.0f);

    // Audition crossfade ~15 ms; the filters are recreated the first time a
    // band is auditioned (lastAuditionBand = -1 forces this).
    auditionFilterL.prepare (spec);   auditionFilterL.reset();
    auditionFilterR.prepare (spec);   auditionFilterR.reset();
    smAudition.reset (sampleRate, 0.015);
    smAudition.setCurrentAndTargetValue (0.0f);
    lastAuditionBand = -1;
    lastAuditionFreq = lastAuditionQ = -1.0f;

    // Monitor crossfade ~10 ms; start directly at the current mode
    const int mon = (int) pMonitor->load();
    for (auto* sv : { &smMidL, &smSideL, &smMidR, &smSideR })
        sv->reset (sampleRate, 0.01);
    smMidL.setCurrentAndTargetValue  (mon == 2 ? 0.0f : 1.0f);
    smSideL.setCurrentAndTargetValue (mon == 1 ? 0.0f : 1.0f);
    smMidR.setCurrentAndTargetValue  (mon == 2 ? 0.0f : 1.0f);
    smSideR.setCurrentAndTargetValue (mon == 0 ? -1.0f : (mon == 1 ? 0.0f : 1.0f));

    coeffsDirty = true;
    lastHpFreq = lastLpFreq = -1.0f;
    lastHpSlope = lastLpSlope = -1;
    lastHpSideFreq = lastLpSideFreq = -1.0f;
    lastHpSideSlope = lastLpSideSlope = -1;
    lastHpIndependent = lastLpIndependent = false;
    updateFilters();
    updateCutFilters();
}

bool MSEQ8AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // M/S requires stereo in/out
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
void MSEQ8AudioProcessor::updateFilters()
{
    for (int i = 0; i < numBands; ++i)
    {
        // Envelope coefficients (attack/release) from the ms parameters
        const float attMs = pDynAtt[i]->load();
        const float relMs = pDynRel[i]->load();
        if (attMs != lastAttMs[i] || relMs != lastRelMs[i])
        {
            lastAttMs[i] = attMs;
            lastRelMs[i] = relMs;
            envAttC[i] = (float) std::exp (-1.0 / (juce::jmax (0.05f, attMs) * 0.001
                                                   * currentSampleRate));
            envRelC[i] = (float) std::exp (-1.0 / (juce::jmax (1.0f, relMs) * 0.001
                                                   * currentSampleRate));
        }

        const float f = pFreq[i]->load();
        const float g = pGain[i]->load();
        const float q = pQ[i]->load();
        const int type = (int) pType[i]->load();
        const float range = pDynRange[i]->load();
        const float freq = juce::jlimit (20.0f, (float) (currentSampleRate * 0.49), f);

        if (coeffsDirty)
        {
            // Preset/state loading: jump directly, no ramp
            lastFreq[i] = f; lastGain[i] = g; lastQ[i] = q; lastType[i] = type;
            lastRange[i] = range;

            for (int ch = 0; ch < 2; ++ch)
            {
                smFreq[ch][i].setCurrentAndTargetValue (freq);
                smQ[ch][i].setCurrentAndTargetValue (q);
                smGainDb[ch][i].setCurrentAndTargetValue (g);
            }

            setBandCoefficients (midFilters[i],  currentSampleRate, type, freq, q, g);
            setBandCoefficients (sideFilters[i], currentSampleRate, type, freq, q, g);
            setBandPassCoefficients (detMid[i],  currentSampleRate, freq, q);
            setBandPassCoefficients (detSide[i], currentSampleRate, freq, q);
            lastEffGainMid[i] = lastEffGainSide[i] = 1.0e9f;
        }
        else if (type != lastType[i])
        {
            // A type change is discrete. IMPORTANT: snap the smoother directly
            // to the current target values (setCurrentAndTargetValue) instead
            // of only writing lastFreq/lastGain/lastQ and leaving the smoother
            // untouched - otherwise a simultaneous frequency change gets
            // "swallowed" without ever being ramped (lastFreq already matches
            // f, so the normal change branch below never triggers
            // afterwards), which permanently freezes both the filter and
            // detector coefficients at the wrong frequency. This was exactly
            // what made Find Resonances bands go completely silent in the
            // dynamics engine if the band happened to have a different type
            // from before (Find Resonances always sets type + frequency in
            // the same apply call) - this should work regardless of what the
            // user did with the band beforehand.
            lastType[i] = type;
            lastFreq[i] = f; lastGain[i] = g; lastQ[i] = q;
            lastRange[i] = range;

            for (int ch = 0; ch < 2; ++ch)
            {
                smFreq[ch][i].setCurrentAndTargetValue (freq);
                smQ[ch][i].setCurrentAndTargetValue (q);
                smGainDb[ch][i].setCurrentAndTargetValue (g);
            }

            setBandCoefficients (midFilters[i],  currentSampleRate, type, freq, q, g);
            setBandCoefficients (sideFilters[i], currentSampleRate, type, freq, q, g);
            setBandPassCoefficients (detMid[i],  currentSampleRate, freq, q);
            setBandPassCoefficients (detSide[i], currentSampleRate, freq, q);
            lastEffGainMid[i] = lastEffGainSide[i] = 1.0e9f;
        }
        else
        {
            if (f != lastFreq[i] || g != lastGain[i] || q != lastQ[i])
            {
                // Normal parameter change: set the target, the sub-block path
                // ramps smoothly
                lastFreq[i] = f; lastGain[i] = g; lastQ[i] = q;

                for (int ch = 0; ch < 2; ++ch)
                {
                    smFreq[ch][i].setTargetValue (freq);
                    smQ[ch][i].setTargetValue (q);
                    smGainDb[ch][i].setTargetValue (g);
                }
            }

            if (range != lastRange[i])
            {
                // The band is becoming/stopping being dynamic. Force an
                // immediate coefficient refresh in processBandChannel() (both
                // filter and detector) by invalidating the lastEff cache -
                // otherwise the first block where `dynamic` becomes true
                // could have neither an in-progress freq/Q ramp nor an
                // already-changed effective gain (amount is still 0 on the
                // first block), so the inner update condition in
                // processBandChannel() never triggers and the detector never
                // gets synced to the band's actual frequency.
                lastRange[i] = range;
                lastEffGainMid[i] = lastEffGainSide[i] = 1.0e9f;
            }
        }
    }

    coeffsDirty = false;
}

void MSEQ8AudioProcessor::updateCutFilters()
{
    // hpMid/lpMid ALWAYS mirror hp_freq/hp_slope and lp_freq/lp_slope -
    // exactly the same behaviour as before "independent" mode existed, so
    // old projects/presets (which only know about these parameters) sound
    // identical.
    const bool hpIndep = pHpIndependent->load() > 0.5f;
    const bool lpIndep = pLpIndependent->load() > 0.5f;

    {
        const float hf = pHpFreq->load();
        const int   hs = (int) pHpSlope->load();

        if (hf != lastHpFreq || hs != lastHpSlope)
        {
            lastHpFreq = hf; lastHpSlope = hs;
            const auto freq = juce::jlimit (20.0f, (float) (currentSampleRate * 0.49), hf);

            int newSections = 0;
            const float* qs = butterworthQs (hs, newSections);
            for (int s = 0; s < newSections; ++s)
                assignBiquad (hpMid[s], juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (
                    currentSampleRate, freq, qs[s]));
            for (int s = hpSections; s < newSections; ++s)
                hpMid[s].reset();
            hpSections = newSections;
        }
    }

    // hpSide: mirrors hp_freq/hp_slope (like mid, as before) unless
    // independent, in which case it uses hp_side_freq/hp_side_slope. A
    // change of independent mode itself counts as a change so the filter is
    // immediately synced to the right source.
    {
        const float hf = hpIndep ? pHpSideFreq->load()  : pHpFreq->load();
        const int   hs = hpIndep ? (int) pHpSideSlope->load() : (int) pHpSlope->load();

        if (hf != lastHpSideFreq || hs != lastHpSideSlope || hpIndep != lastHpIndependent)
        {
            lastHpSideFreq = hf; lastHpSideSlope = hs; lastHpIndependent = hpIndep;
            const auto freq = juce::jlimit (20.0f, (float) (currentSampleRate * 0.49), hf);

            int newSections = 0;
            const float* qs = butterworthQs (hs, newSections);
            for (int s = 0; s < newSections; ++s)
                assignBiquad (hpSide[s], juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (
                    currentSampleRate, freq, qs[s]));
            for (int s = hpSideSections; s < newSections; ++s)
                hpSide[s].reset();
            hpSideSections = newSections;
        }
    }

    {
        const float lf = pLpFreq->load();
        const int   ls = (int) pLpSlope->load();

        if (lf != lastLpFreq || ls != lastLpSlope)
        {
            lastLpFreq = lf; lastLpSlope = ls;
            const auto freq = juce::jlimit (20.0f, (float) (currentSampleRate * 0.49), lf);

            int newSections = 0;
            const float* qs = butterworthQs (ls, newSections);
            for (int s = 0; s < newSections; ++s)
                assignBiquad (lpMid[s], juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (
                    currentSampleRate, freq, qs[s]));
            for (int s = lpSections; s < newSections; ++s)
                lpMid[s].reset();
            lpSections = newSections;
        }
    }

    {
        const float lf = lpIndep ? pLpSideFreq->load()  : pLpFreq->load();
        const int   ls = lpIndep ? (int) pLpSideSlope->load() : (int) pLpSlope->load();

        if (lf != lastLpSideFreq || ls != lastLpSideSlope || lpIndep != lastLpIndependent)
        {
            lastLpSideFreq = lf; lastLpSideSlope = ls; lastLpIndependent = lpIndep;
            const auto freq = juce::jlimit (20.0f, (float) (currentSampleRate * 0.49), lf);

            int newSections = 0;
            const float* qs = butterworthQs (ls, newSections);
            for (int s = 0; s < newSections; ++s)
                assignBiquad (lpSide[s], juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (
                    currentSampleRate, freq, qs[s]));
            for (int s = lpSideSections; s < newSections; ++s)
                lpSide[s].reset();
            lpSideSections = newSections;
        }
    }
}

//==============================================================================
void MSEQ8AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || numSamples == 0)
        return;

    // In-meter
    {
        const float peak = juce::jmax (buffer.getMagnitude (0, 0, numSamples),
                                       buffer.getMagnitude (1, 0, numSamples));
        const float rms  = 0.5f * (buffer.getRMSLevel (0, 0, numSamples)
                                 + buffer.getRMSLevel (1, 0, numSamples));
        inLevel.store (juce::jmax (peak, inLevel.load() * 0.85f));
        inRms.store (rms);
    }

    if (pGlobalBypass->load() > 0.5f)
    {
        outLevel.store (inLevel.load());
        outRms.store (inRms.load());

        const auto* l = buffer.getReadPointer (0);
        const auto* r = buffer.getReadPointer (1);
        for (int n = 0; n < numSamples; ++n)
        {
            tapMid.push  ((l[n] + r[n]) * 0.5f);
            tapSide.push ((l[n] - r[n]) * 0.5f);
        }
        return;
    }

    updateFilters();
    updateCutFilters();

    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    midBuffer.setSize  (1, numSamples, false, false, true);
    sideBuffer.setSize (1, numSamples, false, false, true);
    auto* mid  = midBuffer.getWritePointer (0);
    auto* side = sideBuffer.getWritePointer (0);

    // Encode L/R -> M/S + save dry copies for delta listening
    dryMidBuffer.setSize  (1, numSamples, false, false, true);
    drySideBuffer.setSize (1, numSamples, false, false, true);
    auto* dryMid  = dryMidBuffer.getWritePointer (0);
    auto* drySide = drySideBuffer.getWritePointer (0);

    for (int n = 0; n < numSamples; ++n)
    {
        mid[n]  = (left[n] + right[n]) * 0.5f;
        side[n] = (left[n] - right[n]) * 0.5f;
        dryMid[n]  = mid[n];
        drySide[n] = side[n];
    }

    // Peak bands per M/S mode (static or dynamic per band)
    for (int i = 0; i < numBands; ++i)
    {
        if (pBandBypass[i]->load() > 0.5f)
            continue;

        const int mode = (int) pMode[i]->load();

        if (mode == modeMid || mode == modeMidSide)
            processBandChannel (i, mid, numSamples, false);

        if (mode == modeSide || mode == modeMidSide)
            processBandChannel (i, side, numSamples, true);
    }

    // HP/LP with Stereo/Mid/Side routing
    // HP/LP routing: in independent mode, hp_mode/lp_mode (Stereo/Mid/Side)
    // is ignored entirely - the whole point is running BOTH channels
    // simultaneously, just with a different frequency/slope each. hpMid/
    // hpSide (and lpMid/lpSide) can now have a different number of cascaded
    // sections (hpSections vs. hpSideSections) since the slope can differ,
    // hence the separate loops.
    if (pHpOn->load() > 0.5f)
    {
        const bool indep = pHpIndependent->load() > 0.5f;
        const int  mode  = (int) pHpMode->load();

        if (indep || mode != cutSide)
            for (int s = 0; s < hpSections; ++s)
            {
                juce::dsp::AudioBlock<float> block (midBuffer);
                juce::dsp::ProcessContextReplacing<float> ctx (block);
                hpMid[s].process (ctx);
            }

        if (indep || mode != cutMid)
            for (int s = 0; s < hpSideSections; ++s)
            {
                juce::dsp::AudioBlock<float> block (sideBuffer);
                juce::dsp::ProcessContextReplacing<float> ctx (block);
                hpSide[s].process (ctx);
            }
    }

    if (pLpOn->load() > 0.5f)
    {
        const bool indep = pLpIndependent->load() > 0.5f;
        const int  mode  = (int) pLpMode->load();

        if (indep || mode != cutSide)
            for (int s = 0; s < lpSections; ++s)
            {
                juce::dsp::AudioBlock<float> block (midBuffer);
                juce::dsp::ProcessContextReplacing<float> ctx (block);
                lpMid[s].process (ctx);
            }

        if (indep || mode != cutMid)
            for (int s = 0; s < lpSideSections; ++s)
            {
                juce::dsp::AudioBlock<float> block (sideBuffer);
                juce::dsp::ProcessContextReplacing<float> ctx (block);
                lpSide[s].process (ctx);
            }
    }

    // Delta listening: crossfade toward (dry - wet) = what was removed.
    // Done in the M/S domain so monitor-solo of the delta works.
    smDelta.setTargetValue (pDelta->load() > 0.5f ? 1.0f : 0.0f);

    if (smDelta.getCurrentValue() > 0.0001f || smDelta.getTargetValue() > 0.0001f)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float mix = smDelta.getNextValue();
            mid[n]  += mix * (dryMid[n]  - 2.0f * mid[n]);
            side[n] += mix * (drySide[n] - 2.0f * side[n]);
        }
    }
    else
    {
        smDelta.skip (numSamples);
    }

    // Post-EQ taps + decode M/S -> L/R per monitor mode:
    // Stereo: L=mid+side, R=mid-side. Mid solo: L=R=mid. Side solo: L=R=side.
    {
        const int mon = (int) pMonitor->load();
        smMidL.setTargetValue  (mon == 2 ? 0.0f : 1.0f);
        smSideL.setTargetValue (mon == 1 ? 0.0f : 1.0f);
        smMidR.setTargetValue  (mon == 2 ? 0.0f : 1.0f);
        smSideR.setTargetValue (mon == 0 ? -1.0f : (mon == 1 ? 0.0f : 1.0f));

        for (int n = 0; n < numSamples; ++n)
        {
            tapMid.push (mid[n]);
            tapSide.push (side[n]);

            const float a = smMidL.getNextValue();
            const float b = smSideL.getNextValue();
            const float c = smMidR.getNextValue();
            const float d = smSideR.getNextValue();

            left[n]  = a * mid[n] + b * side[n];
            right[n] = c * mid[n] + d * side[n];
        }
    }

    // Ctrl+hover audition: isolates a band's approximate frequency range
    // (a bandpass around its freq/Q) in the final L/R signal, so the user
    // can hear just that band playing. Affects monitoring only, not the
    // actual EQ curve. No APVTS parameter - driven directly by the
    // auditionBand atomic from the UI (see PluginProcessor.h).
    {
        const int ab = auditionBand.load();
        const bool wantAudition = ab >= 0 && ab < numBands;

        if (wantAudition)
        {
            const float freq = juce::jlimit (20.0f, (float) (currentSampleRate * 0.49), pFreq[ab]->load());
            const float q    = juce::jmax (0.5f, pQ[ab]->load());

            if (ab != lastAuditionBand || freq != lastAuditionFreq || q != lastAuditionQ)
            {
                auto coeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass (currentSampleRate, freq, q);
                *auditionFilterL.coefficients = *coeffs;
                *auditionFilterR.coefficients = *coeffs;
                lastAuditionBand = ab;
                lastAuditionFreq = freq;
                lastAuditionQ = q;
            }
        }

        smAudition.setTargetValue (wantAudition ? 1.0f : 0.0f);

        if (smAudition.getCurrentValue() > 0.0001f || smAudition.getTargetValue() > 0.0001f)
        {
            for (int n = 0; n < numSamples; ++n)
            {
                const float fade = smAudition.getNextValue();
                const float fl = auditionFilterL.processSample (left[n]);
                const float fr = auditionFilterR.processSample (right[n]);
                left[n]  += fade * (fl - left[n]);
                right[n] += fade * (fr - right[n]);
            }
        }
        else
        {
            smAudition.skip (numSamples);
        }
    }

    // Output gain, ramped for click-free automation
    const float targetGain = juce::Decibels::decibelsToGain (pOutGain->load());
    buffer.applyGainRamp (0, numSamples, lastOutGain, targetGain);
    lastOutGain = targetGain;

    // Out-meter
    {
        const float peak = juce::jmax (buffer.getMagnitude (0, 0, numSamples),
                                       buffer.getMagnitude (1, 0, numSamples));
        const float rms  = 0.5f * (buffer.getRMSLevel (0, 0, numSamples)
                                 + buffer.getRMSLevel (1, 0, numSamples));
        outLevel.store (juce::jmax (peak, outLevel.load() * 0.85f));
        outRms.store (rms);
    }
}

//==============================================================================
void MSEQ8AudioProcessor::processBandChannel (int band, float* data, int numSamples,
                                              bool sideChannel)
{
    const int ch   = sideChannel ? 1 : 0;
    auto& filter   = sideChannel ? sideFilters[band]      : midFilters[band];
    auto& detector = sideChannel ? detSide[band]          : detMid[band];
    auto& env      = sideChannel ? envSide[band]          : envMid[band];
    auto& lastEff  = sideChannel ? lastEffGainSide[band]  : lastEffGainMid[band];
    auto& gainOut  = sideChannel ? dynGainSide[band]      : dynGainMid[band];

    auto& sF = smFreq[ch][band];
    auto& sQ = smQ[ch][band];
    auto& sG = smGainDb[ch][band];

    const int type = (int) pType[band]->load();
    const bool gainless = type == typeNotch;   // notch has no gain -> no dynamics

    const float range = pDynRange[band]->load();
    const bool dynamic   = ! gainless && std::abs (range) >= 0.01f;
    const bool smoothing = sF.isSmoothing() || sQ.isSmoothing() || sG.isSmoothing();

    // Static band at rest: the whole block in one sweep (coefficients are already correct)
    if (! dynamic && ! smoothing)
    {
        gainOut.store (sG.getCurrentValue());

        float* chans[1] = { data };
        juce::dsp::AudioBlock<float> block (chans, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        filter.process (ctx);
        return;
    }

    // Sub-block path: parameter ramps and/or dynamics. Coefficients update
    // allocation-free every 32 samples.
    const float thresh = pDynThresh[band]->load();
    float effDb = sG.getCurrentValue();

    for (int start = 0; start < numSamples; start += dynSubBlock)
    {
        const int len = juce::jmin (dynSubBlock, numSamples - start);

        const float f = smoothing ? sF.skip (len) : sF.getCurrentValue();
        const float q = smoothing ? sQ.skip (len) : sQ.getCurrentValue();
        const float g = smoothing ? sG.skip (len) : sG.getCurrentValue();

        if (dynamic)
        {
            const float att = envAttC[band];
            const float rel = envRelC[band];

            for (int n = start; n < start + len; ++n)
            {
                const float x = std::abs (detector.processSample (data[n]));
                env = x > env ? att * env + (1.0f - att) * x
                              : rel * env + (1.0f - rel) * x;
            }

            const float envDb  = juce::Decibels::gainToDecibels (env, -100.0f);

            // The knee width is Q-dependent (same reasoning as the threshold
            // margin in Find Resonances): narrow/high-Q resonances are heard
            // as distinct tones even at a small excess and benefit from a
            // steeper, more decisive knee; broad/low-Q resonances blend into
            // the overall mix more and fare better with a gentler, wider
            // knee so broadband material isn't clamped too hard. Applies to
            // all dynamic bands, not just the ones Find Resonances proposes.
            const float kneeDb = juce::jlimit (4.0f, 18.0f, 25.0f / std::sqrt (juce::jmax (0.5f, q)));
            const float amount = juce::jlimit (0.0f, 1.0f, (envDb - thresh) / kneeDb);
            effDb = g + range * amount;
        }
        else
        {
            effDb = g;
        }

        if (smoothing || std::abs (effDb - lastEff) > 0.05f)
        {
            setBandCoefficients (filter, currentSampleRate, type, f, q, effDb);

            // Previously "dynamic && smoothing" made the detector depend on
            // a freq/Q ramp happening to be in progress at the exact moment
            // the band became dynamic - otherwise it never got a chance to
            // sync at all (see the lastRange fix in updateFilters()). Now
            // it's enough for the band to BE dynamic - as soon as we're
            // already inside this (already fairly cheap) update block, keep
            // the detector's coefficients in sync too so it always measures
            // the band's actual current frequency/Q, not a stale one.
            if (dynamic)
                setBandPassCoefficients (detector, currentSampleRate, f, q);

            lastEff = effDb;
        }

        for (int n = start; n < start + len; ++n)
            data[n] = filter.processSample (data[n]);
    }

    filter.snapToZero();
    if (dynamic)
        detector.snapToZero();

    gainOut.store (effDb);
}

//==============================================================================
void MSEQ8AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MSEQ8AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

    coeffsDirty = true;
    lastHpFreq = lastLpFreq = -1.0f;
    lastHpSideFreq = lastLpSideFreq = -1.0f;
    lastHpIndependent = lastLpIndependent = false;
}

//==============================================================================
void MSEQ8AudioProcessor::parameterChanged (const juce::String&, float)
{
    // ONLY a lock-free atomic increment here - this callback can be called
    // from any thread (the audio thread on host automation, the message
    // thread on GUI changes). Anything that allocates or takes a lock
    // (ValueTree copying, replaceState) instead happens in the editor's
    // debounce logic (PluginEditor::timerCallback(), guaranteed to be the
    // message thread), which polls this counter and commits via
    // pushUndoSnapshot() once things have been quiet for a while.
    paramChangeGeneration.fetch_add (1, std::memory_order_relaxed);
}

void MSEQ8AudioProcessor::pushUndoSnapshot()
{
    if (restoringState)
        return;   // our own undo()/redo() shouldn't record itself

    // If we're in the middle of the history (having undone something) and
    // make a new change: truncate everything after the current position -
    // classic undo/redo behaviour, a new branch replaces the old "redo" path.
    if (undoIndex >= 0 && undoIndex < (int) undoHistory.size() - 1)
        undoHistory.resize ((size_t) undoIndex + 1);

    undoHistory.push_back (apvts.copyState().createCopy());

    if ((int) undoHistory.size() > maxUndoSteps)
        undoHistory.erase (undoHistory.begin());   // drop the oldest

    // Always the last element in the (possibly trimmed) history is "now".
    undoIndex = (int) undoHistory.size() - 1;
}

void MSEQ8AudioProcessor::undo()
{
    if (! canUndo())
        return;

    restoringState = true;
    --undoIndex;
    apvts.replaceState (undoHistory[(size_t) undoIndex].createCopy());
    coeffsDirty = true;
    lastHpFreq = lastLpFreq = -1.0f;
    lastHpSideFreq = lastLpSideFreq = -1.0f;
    lastHpIndependent = lastLpIndependent = false;
    restoringState = false;
}

void MSEQ8AudioProcessor::redo()
{
    if (! canRedo())
        return;

    restoringState = true;
    ++undoIndex;
    apvts.replaceState (undoHistory[(size_t) undoIndex].createCopy());
    coeffsDirty = true;
    lastHpFreq = lastLpFreq = -1.0f;
    lastHpSideFreq = lastLpSideFreq = -1.0f;
    lastHpIndependent = lastLpIndependent = false;
    restoringState = false;
}

//==============================================================================
void MSEQ8AudioProcessor::storeCurrentSlot()
{
    snapshots[activeSlot] = apvts.copyState().createCopy();
}

void MSEQ8AudioProcessor::switchToSlot (int newSlot)
{
    newSlot = juce::jlimit (0, numSlots - 1, newSlot);
    if (newSlot == activeSlot)
        return;

    storeCurrentSlot();

    if (snapshots[newSlot].isValid())
        apvts.replaceState (snapshots[newSlot].createCopy());

    activeSlot = newSlot;
    coeffsDirty = true;
    lastHpFreq = lastLpFreq = -1.0f;
    lastHpSideFreq = lastLpSideFreq = -1.0f;
    lastHpIndependent = lastLpIndependent = false;
}

//==============================================================================
// User presets
juce::File MSEQ8AudioProcessor::getUserPresetFolder() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("MSEQ8").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

juce::Array<juce::File> MSEQ8AudioProcessor::getUserPresetFiles() const
{
    auto files = getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.xml");
    files.sort();
    return files;
}

bool MSEQ8AudioProcessor::saveUserPreset (const juce::String& name)
{
    const auto file = getUserPresetFolder().getChildFile (
        juce::File::createLegalFileName (name) + ".xml");

    if (auto xml = apvts.copyState().createXml())
        return xml->writeTo (file);

    return false;
}

void MSEQ8AudioProcessor::loadUserPreset (const juce::File& file)
{
    if (auto xml = juce::parseXML (file))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            coeffsDirty = true;
            lastHpFreq = lastLpFreq = -1.0f;
            lastHpSideFreq = lastLpSideFreq = -1.0f;
            lastHpIndependent = lastLpIndependent = false;
        }
    }
}

//==============================================================================
juce::StringArray MSEQ8AudioProcessor::getPresetNames() const
{
    return { "Default", "Vocal Clarity", "Wide Master", "Low-End Focus" };
}

void MSEQ8AudioProcessor::applyPreset (int index)
{
    auto setParam = [this] (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    // Reset all bands + HP/LP first
    for (int i = 0; i < numBands; ++i)
    {
        setParam (freqID (i),   defaultFreqs[i]);
        setParam (gainID (i),   0.0f);
        setParam (qID (i),      1.0f);
        setParam (modeID (i),   (float) modeMidSide);
        setParam (typeID (i),   (float) typeBell);
        setParam (bypassID (i), 0.0f);
        setParam (dynThreshID (i), -30.0f);
        setParam (dynRangeID (i),  0.0f);
        setParam (dynAttID (i),    5.0f);
        setParam (dynRelID (i),    150.0f);
    }
    setParam ("hp_on", 0.0f);  setParam ("hp_freq", 30.0f);
    setParam ("hp_slope", 0.0f);  setParam ("hp_mode", (float) cutStereo);
    setParam ("hp_independent", 0.0f);
    setParam ("hp_side_freq", 30.0f);  setParam ("hp_side_slope", 0.0f);
    setParam ("lp_on", 0.0f);  setParam ("lp_freq", 18000.0f);
    setParam ("lp_slope", 0.0f);  setParam ("lp_mode", (float) cutStereo);
    setParam ("lp_independent", 0.0f);
    setParam ("lp_side_freq", 18000.0f);  setParam ("lp_side_slope", 0.0f);
    setParam ("output_gain", 0.0f);
    setParam ("delta", 0.0f);

    switch (index)
    {
        case 1: // Vocal Clarity
            setParam ("hp_on", 1.0f);  setParam ("hp_freq", 80.0f);
            setParam (gainID (1), -1.5f);  setParam (qID (1), 1.1f);
            setParam (gainID (2), -1.0f);  setParam (qID (2), 1.2f);
            setParam (gainID (3), 2.0f);
            setParam (gainID (4), 1.0f);   setParam (modeID (4), (float) modeMid);
            setParam (gainID (5), 3.5f);   setParam (qID (5), 0.9f);
            setParam (gainID (6), 2.5f);
            setParam (gainID (7), 1.5f);   setParam (modeID (7), (float) modeSide);
            break;

        case 2: // Wide Master
            setParam ("hp_on", 1.0f);  setParam ("hp_freq", 120.0f);
            setParam ("hp_mode", (float) cutSide);
            setParam (gainID (5), 1.5f);   setParam (modeID (5), (float) modeSide);
            setParam (gainID (6), 2.0f);   setParam (modeID (6), (float) modeSide);
            setParam (gainID (7), 2.5f);   setParam (modeID (7), (float) modeSide);
            break;

        case 3: // Low-End Focus
            setParam (gainID (0), 2.5f);   setParam (modeID (0), (float) modeMid);
            setParam (gainID (1), 1.5f);   setParam (modeID (1), (float) modeMid);
            setParam (gainID (2), -1.0f);
            break;

        default:
            break;
    }
}

//==============================================================================
juce::AudioProcessorEditor* MSEQ8AudioProcessor::createEditor()
{
    return new MSEQ8AudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MSEQ8AudioProcessor();
}

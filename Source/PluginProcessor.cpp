#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

//==============================================================
// Parameter layout
//==============================================================
juce::AudioProcessorValueTreeState::ParameterLayout
FruityClipAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Left finger – input gain, in dB
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "inputGain", "Input Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f));

    // SILK – 0..1
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "silkAmount", "Silk Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // OTT – 0..1 (150 Hz+ only, parallel, unity gain)
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "ottAmount", "OTT Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // SAT – 0..1
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "satAmount", "Saturation Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // MODE – 0 = clipper, 1 = limiter
    params.push_back (std::make_unique<juce::AudioParameterBool>(
        "useLimiter", "Use Limiter", false));

    // OVERSAMPLE MODE – 0:x1, 1:x2, 2:x4, 3:x8, 4:x16
    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        "oversampleMode", "Oversample Mode",
        juce::StringArray { "x1", "x2", "x4", "x8", "x16" }, 0));

    return { params.begin(), params.end() };
}

//==============================================================
// Fruity-ish soft clip curve
// threshold: 0..1, where lower = earlier / softer onset
//==============================================================
float FruityClipAudioProcessor::fruitySoftClipSample (float x, float threshold)
{
    const float sign = (x >= 0.0f ? 1.0f : -1.0f);
    const float ax   = std::abs (x);

    if (ax <= threshold)
        return x;

    if (ax >= 1.0f)
        return sign * 1.0f;

    // Normalised smooth curve between threshold and 1.0
    const float t = (ax - threshold) / (1.0f - threshold); // 0..1
    const float shaped = threshold + (1.0f - (1.0f - t) * (1.0f - t)) * (1.0f - threshold);

    return sign * shaped;
}

//==============================================================
// Subtle Neve 5060-style Silk curve
//==============================================================
float FruityClipAudioProcessor::silkCurveFull (float x)
{
    const float x2 = x * x;
    const float x3 = x2 * x;
    const float x5 = x3 * x2;

    // Gentle odd harmonics
    constexpr float a3 = 0.15f;
    constexpr float a5 = 0.02f;

    float y = x + a3 * x3 + a5 * x5;

    // Normalise so |1| in -> |1| out
    constexpr float y1   = 1.0f + a3 * 1.0f + a5 * 1.0f;
    constexpr float norm = 1.0f / y1;

    y *= norm;

    return juce::jlimit (-1.2f, 1.2f, y);
}

//==============================================================
// Constructor / Destructor
//==============================================================
FruityClipAudioProcessor::FruityClipAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMS", createParameterLayout())
{
    // Ultra fine-tuned Fruity-null gain
    postGain        = 0.99999385f;
    // Soft clip threshold (~ -6 dB at satAmount = 1)
    thresholdLinear = juce::Decibels::decibelsToGain (-6.0f);
}

FruityClipAudioProcessor::~FruityClipAudioProcessor() = default;

//==============================================================
// Oversampling config helper
//==============================================================
void FruityClipAudioProcessor::updateOversampling (int osIndex, int numChannels)
{
    // osIndex: 0=x1, 1=x2, 2=x4, 3=x8, 4=x16
    currentOversampleIndex = juce::jlimit (0, 4, osIndex);

    int numStages = 0; // factor = 2^stages
    switch (currentOversampleIndex)
    {
        case 0: numStages = 0; break; // x1
        case 1: numStages = 1; break; // x2
        case 2: numStages = 2; break; // x4
        case 3: numStages = 3; break; // x8
        case 4: numStages = 4; break; // x16
        default: numStages = 0; break;
    }

    if (numStages <= 0 || numChannels <= 0)
    {
        oversampler.reset();
        return;
    }

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        numChannels,
        numStages,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true /* maximum quality */);

    oversampler->reset();

    if (maxBlockSize > 0)
        oversampler->initProcessing ((size_t) maxBlockSize);
}

//==============================================================
// Basic AudioProcessor overrides
//==============================================================
void FruityClipAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate   = (newSampleRate > 0.0 ? newSampleRate : 44100.0);
    limiterGain  = 1.0f;
    maxBlockSize = juce::jmax (1, samplesPerBlock);

    // ~50 ms release for limiter
    const float releaseTimeSec = 0.050f;
    limiterReleaseCo = std::exp (-1.0f / (releaseTimeSec * (float) sampleRate));

    // Reset K-weight filter + LUFS state
    resetKFilterState (getTotalNumOutputChannels());
    lufsMeanSquare = 1.0e-6f;

    // Reset OTT split state
    resetOttState (getTotalNumOutputChannels());

    // One-pole lowpass factor for 150 Hz split (0–150 = low band)
    const float fc = 150.0f;
    const float sr = (float) sampleRate;
    const float alpha = std::exp (-2.0f * juce::MathConstants<float>::pi * fc / sr);
    ottAlpha = juce::jlimit (0.0f, 1.0f, alpha);

    lastOttGain = 1.0f;

    // Initial oversampling setup from parameter
    if (auto* osModeParam = parameters.getRawParameterValue ("oversampleMode"))
    {
        const int osIndex = (int) osModeParam->load();
        updateOversampling (osIndex, getTotalNumOutputChannels());
    }
    else
    {
        updateOversampling (0, getTotalNumOutputChannels());
    }

    // Reset GUI signal envelope for LUFS gating
    guiSignalEnv.store (0.0f);
}

void FruityClipAudioProcessor::releaseResources() {}

bool FruityClipAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto main = layouts.getMainOutputChannelSet();
    return main == juce::AudioChannelSet::stereo()
        || main == juce::AudioChannelSet::mono();
}

//==============================================================
// K-weight filter reset
//==============================================================
void FruityClipAudioProcessor::resetKFilterState (int numChannels)
{
    kFilterStates.clear();
    if (numChannels <= 0)
        return;

    kFilterStates.resize ((size_t) numChannels);
    for (auto& st : kFilterStates)
    {
        st.z1a = st.z2a = 0.0f;
        st.z1b = st.z2b = 0.0f;
    }
}

//==============================================================
// OTT HP split reset
//==============================================================
void FruityClipAudioProcessor::resetOttState (int numChannels)
{
    ottStates.clear();
    if (numChannels <= 0)
        return;

    ottStates.resize ((size_t) numChannels);
    for (auto& st : ottStates)
        st.low = 0.0f;
}

//==============================================================
// Limiter sample processor (0 lookahead, zero latency)
//==============================================================
float FruityClipAudioProcessor::processLimiterSample (float x)
{
    const float ax    = std::abs (x);
    const float limit = 1.0f;

    float desiredGain = 1.0f;
    if (ax > limit && ax > 0.0f)
        desiredGain = limit / ax;

    // Instant attack, exponential release
    if (desiredGain < limiterGain)
    {
        // Attack: clamp down immediately
        limiterGain = desiredGain;
    }
    else
    {
        // Release: move back towards 1.0
        limiterGain = limiterGain + (1.0f - limiterReleaseCo) * (desiredGain - limiterGain);
    }

    return x * limiterGain;
}

//==============================================================
// CORE DSP
//==============================================================
void FruityClipAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    if ((int) kFilterStates.size() < numChannels)
        resetKFilterState (numChannels);
    if ((int) ottStates.size() < numChannels)
        resetOttState (numChannels);

    auto* gainParam   = parameters.getRawParameterValue ("inputGain");
    auto* silkParam   = parameters.getRawParameterValue ("silkAmount");
    auto* ottParam    = parameters.getRawParameterValue ("ottAmount");
    auto* satParam    = parameters.getRawParameterValue ("satAmount");
    auto* modeParam   = parameters.getRawParameterValue ("useLimiter");
    auto* osModeParam = parameters.getRawParameterValue ("oversampleMode");

    const float inputGainDb   = gainParam   ? gainParam->load()   : 0.0f;
    const float silkAmountRaw = silkParam   ? silkParam->load()   : 0.0f;
    const float ottAmountRaw  = ottParam    ? ottParam->load()    : 0.0f;
    const float satAmountRaw  = satParam    ? satParam->load()    : 0.0f;
    const bool  useLimiter    = modeParam   ? (modeParam->load() >= 0.5f) : false;
    const int   osIndexParam  = osModeParam ? (int) osModeParam->load()   : 0;

    const float silkAmount = juce::jlimit (0.0f, 1.0f, silkAmountRaw);
    const float ottAmount  = juce::jlimit (0.0f, 1.0f, ottAmountRaw);
    const float satAmount  = juce::jlimit (0.0f, 1.0f, satAmountRaw);

    const float silkBlend = silkAmount * silkAmount; // keep first half subtle
    const float g         = postGain;                // Fruity-null alignment
    const float inputGain = juce::Decibels::decibelsToGain (inputGainDb);

    // Oversampling mode can be changed at runtime – keep Oversampling object in sync
    if (osIndexParam != currentOversampleIndex || (! oversampler && osIndexParam > 0))
    {
        updateOversampling (osIndexParam, numChannels);
    }

    if (oversampler && maxBlockSize < numSamples)
    {
        maxBlockSize = numSamples;
        oversampler->initProcessing ((size_t) maxBlockSize);
    }

    //==========================================================
    // PRE-CHAIN: GAIN + SILK + OTT (always at base rate)
    //==========================================================
    double sumSqOriginal = 0.0;
    double sumSqOtt      = 0.0;

    if (ottAmount <= 0.0f)
    {
        // OTT fully bypassed: apply only INPUT GAIN + SILK in place,
        // and force OTT gain to unity so knob = true bypass.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* samples = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                float y = samples[i] * inputGain;

                // SILK
                if (silkBlend > 0.0f)
                {
                    const float silkFull = silkCurveFull (y);
                    y = y + silkBlend * (silkFull - y);
                }

                samples[i] = y;
            }
        }

        lastOttGain = 1.0f;
    }
    else
    {
        // OTT active: do SILK + OTT on a temp buffer and RMS gain-match
        juce::AudioBuffer<float> preChain (numChannels, numSamples);
        preChain.makeCopyOf (buffer);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* x = preChain.getWritePointer (ch);
            auto&  ott = ottStates[(size_t) ch];

            for (int i = 0; i < numSamples; ++i)
            {
                // INPUT GAIN
                float y = x[i] * inputGain;

                // 1) SILK (pre-clip transformer-ish colour)
                if (silkBlend > 0.0f)
                {
                    const float silkFull = silkCurveFull (y);
                    y = y + silkBlend * (silkFull - y);
                }

                // Original (pre-OTT) energy
                sumSqOriginal += (double) (y * y);

                // 2) OTT high-band only (0–150 dry, >150 processed)
                // One-pole lowpass for 0–150 Hz
                ott.low = ottAlpha * ott.low + (1.0f - ottAlpha) * y;
                const float lowDry = ott.low;
                const float hiDry  = y - lowDry;

                // Simple "OTT-ish" processing on high band
                float hiProc = hiDry * (1.0f + 2.5f * ottAmount);
                hiProc = std::tanh (hiProc);

                const float hiMix = hiDry + ottAmount * (hiProc - hiDry);

                y = lowDry + hiMix;

                sumSqOtt += (double) (y * y);

                x[i] = y;
            }
        }

        const int totalSamplesOtt = juce::jmax (1, numSamples * juce::jmax (1, numChannels));

        float ottGain = 1.0f;
        if (sumSqOtt > 0.0)
        {
            float rmsOriginal = (float) std::sqrt (sumSqOriginal / (double) totalSamplesOtt + 1.0e-20);
            float rmsOtt      = (float) std::sqrt (sumSqOtt      / (double) totalSamplesOtt + 1.0e-20);

            if (rmsOtt > 0.0f)
                ottGain = rmsOriginal / rmsOtt;
        }

        // Smooth OTT gain so it doesn't chatter
        const float ottSmooth = 0.4f;
        lastOttGain = (1.0f - ottSmooth) * lastOttGain + ottSmooth * ottGain;

        // Copy OTT-processed buffer into main and apply gain-match
        buffer.makeCopyOf (preChain);
        buffer.applyGain (lastOttGain);
    }

    //==========================================================
    // DISTORTION CHAIN (SAT/CLIP or LIMITER)
    //   - In oversampled mode, this runs at higher rate
    //   - No metering here; meters are computed later at base rate
    //==========================================================
    const bool useOversampling = (oversampler != nullptr && currentOversampleIndex

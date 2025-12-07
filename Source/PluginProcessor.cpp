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

    // OTT – 0..1 (150 Hz+ only, parallel)
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "ottAmount", "LOVE",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // SAT – 0..1
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "satAmount", "DEATH",
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
    // osIndex: 0=x1, 1=x2, 2=x4, 3:x8, 4:x16
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

    // Reset SAT bass-tilt state
    resetSatState (getTotalNumOutputChannels());

    const float sr = (float) sampleRate;

    // One-pole lowpass factor for 150 Hz split (0–150 = low band)
    {
        const float fc = 150.0f;
        const float alpha = std::exp (-2.0f * juce::MathConstants<float>::pi * fc / sr);
        ottAlpha = juce::jlimit (0.0f, 1.0f, alpha);
    }

    // Envelope smoothing for high-band dynamics – slower now (~30 ms)
    // This gives more "tails" and less choking after transients.
    {
        const float envTauSec = 0.030f; // was 0.010f
        const float envA = std::exp (-1.0f / (envTauSec * sr));
        ottEnvAlpha = juce::jlimit (0.0f, 1.0f, envA);
    }

    // One-pole lowpass for SAT bass tilt (around 300 Hz at base rate)
    {
        const float fcSat = 300.0f;
        const float alphaSat = std::exp (-2.0f * juce::MathConstants<float>::pi * fcSat / sr);
        satLowAlpha = juce::jlimit (0.0f, 1.0f, alphaSat);
    }

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
    {
        st.low = 0.0f;
        st.env = 0.0f;
    }
}

//==============================================================
// SAT bass-tilt reset
//==============================================================
void FruityClipAudioProcessor::resetSatState (int numChannels)
{
    satStates.clear();
    if (numChannels <= 0)
        return;

    satStates.resize ((size_t) numChannels);
    for (auto& st : satStates)
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
    if ((int) satStates.size() < numChannels)
        resetSatState (numChannels);

    auto* gainParam   = parameters.getRawParameterValue ("inputGain");
    auto* ottParam    = parameters.getRawParameterValue ("ottAmount");
    auto* satParam    = parameters.getRawParameterValue ("satAmount");
    auto* modeParam   = parameters.getRawParameterValue ("useLimiter");
    auto* osModeParam = parameters.getRawParameterValue ("oversampleMode");

    const float inputGainDb   = gainParam   ? gainParam->load()   : 0.0f;
    const float ottAmountRaw  = ottParam    ? ottParam->load()    : 0.0f;
    const float satAmountRaw  = satParam    ? satParam->load()    : 0.0f;
    const bool  useLimiter    = modeParam   ? (modeParam->load() >= 0.5f) : false;
    const int   osIndexParam  = osModeParam ? (int) osModeParam->load()   : 0;

    const float ottAmount  = juce::jlimit (0.0f, 1.0f, ottAmountRaw);
    const float satAmount  = juce::jlimit (0.0f, 1.0f, satAmountRaw);

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
    // PRE-CHAIN: GAIN + OTT (always at base rate)
    //==========================================================

    if (ottAmount <= 0.0f)
    {
        // OTT fully bypassed: apply only INPUT GAIN in place.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* samples = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                float y = samples[i] * inputGain;
                samples[i] = y;
            }
        }

        lastOttGain = 1.0f; // 1.0 = no trim
    }
    else
    {
        // OTT active: process on temp buffer, then apply static trim to main buffer
        juce::AudioBuffer<float> preChain;
        preChain.makeCopyOf (buffer);

        // First, apply INPUT GAIN + OTT on preChain
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* x = preChain.getWritePointer (ch);
            auto&  ott = ottStates[(size_t) ch];

            for (int i = 0; i < numSamples; ++i)
            {
                // 1) INPUT GAIN
                float y = x[i] * inputGain;

                // 2) OTT high-band only (0–150 dry, >150 processed)
                //    Low band stays intact, high band gets dynamic OTT.
                ott.low = ottAlpha * ott.low + (1.0f - ottAlpha) * y;
                const float lowDry = ott.low;
                const float hiDry  = y - lowDry;

                // 3) High-band envelope for upward + downward action
                const float absHi = std::abs (hiDry);
                ott.env = ottEnvAlpha * ott.env + (1.0f - ottEnvAlpha) * absHi;
                const float env = ott.env;

                // Reference level for high band (~ -20 dB region)
                constexpr float refLevel = 0.10f; // tweakable

                // Normalised level around reference
                float lev = env / (refLevel + 1.0e-6f);

                // 4) Dynamic gain:
                //    - lev < 1  => upward (boost quiet highs / tails)
                //    - lev > 1  => downward (very gentle – don't choke the tails)
                float dynGain = 1.0f;

                if (lev < 1.0f)
                {
                    // Upward region
                    float t = 1.0f - lev;                        // 0..1
                    t = juce::jlimit (0.0f, 1.0f, t);
                    const float maxUp = 2.3f;                    // up to +6 dB
                    dynGain += t * (maxUp - 1.0f) * ottAmount;
                }
                else
                {
                    // Downward region – softened a lot so tails stay alive
                    float t = juce::jlimit (0.0f, 1.0f, lev - 1.0f);
                    const float minGain = 0.85f;                 // only ~ -1.4 dB max tame
                    dynGain -= t * (1.0f - minGain) * ottAmount;
                }

                // 5) Static tilt
                const float staticBoost = 1.0f + 1.0f * ottAmount;

                // Apply combined static + dynamic gain to high band
                float hiProc = hiDry * staticBoost * dynGain;

                // 6) Soft non-linearity to stop craziness
                hiProc = std::tanh (hiProc);

                // 7) Parallel blend (high band only)
                const float hiMix = hiDry + ottAmount * (hiProc - hiDry);

                // 8) Recombine with untouched low band
                y = lowDry + hiMix;

                x[i] = y;
            }
        }

        // --- STATIC TRIM AFTER OTT ---
        // A bit more reduction towards the end of the knob.
        // - No trim up to 50%
        // - Gently increasing trim up to about -1.0 dB at OTT = 1,
        //   with most of the reduction happening near the very top.
        float staticTrimDb = 0.0f;

        if (ottAmount > 0.5f)
        {
            // normalise OTT 0.5 -> 1.0 to 0..1
            float t = (ottAmount - 0.5f) / 0.5f;
            t = juce::jlimit (0.0f, 1.0f, t);

            // shape so most of the trim happens near the top of the range
            float shaped = std::pow (t, 1.5f); // 0..1, slower at start, faster at end

            const float maxTrimDb = -1.5f;      // was -0.8f
            staticTrimDb = maxTrimDb * shaped;
        }

        const float staticTrim = juce::Decibels::decibelsToGain (staticTrimDb);

        lastOttGain = staticTrim; // store for potential debug / GUI if needed

        buffer.makeCopyOf (preChain);
        buffer.applyGain (staticTrim);
    }

    //==========================================================
    // DISTORTION CHAIN (SAT/CLIP or LIMITER)
    //   - In oversampled mode, this runs at higher rate
    //   - No metering here; meters are computed later at base rate
    //==========================================================
    const bool useOversampling = (oversampler != nullptr && currentOversampleIndex > 0);

    if (useOversampling)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        auto osBlock = oversampler->processSamplesUp (block);
        const int osNumSamples = (int) osBlock.getNumSamples();

        if (useLimiter)
        {
            // LIMIT MODE (oversampled)
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* samples = osBlock.getChannelPointer (ch);

                for (int i = 0; i < osNumSamples; ++i)
                {
                    float y = samples[i];

                    // Limiter (0 lookahead)
                    y = processLimiterSample (y);

                    // Alignment + final hard safety in OS world
                    y *= g;

                    if (y >  1.0f) y =  1.0f;
                    if (y < -1.0f) y = -1.0f;

                    samples[i] = y;
                }
            }
        }
        else
        {
            // CLIP / SAT MODE (oversampled)
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* samples = osBlock.getChannelPointer (ch);

                for (int i = 0; i < osNumSamples; ++i)
                {
                    float y = samples[i];

                    // SATURATION (pre-clip), only active when satAmount > 0
                    if (satAmount > 0.0f)
                    {
                        auto& sat = satStates[(size_t) ch];

                        // Curved auto-trim: gentle at start, stronger at the top
                        const float tTrim     = std::pow (satAmount, 1.3f);
                        const float satTrimDb = -2.5f * tTrim; // same max, slower onset
                        const float satTrim   = juce::Decibels::decibelsToGain (satTrimDb);

                        float yPre = y * satTrim;

                        // Low-tilt bass emphasis for the drive
                        sat.low = satLowAlpha * sat.low + (1.0f - satLowAlpha) * yPre;
                        const float low = sat.low;

                        // As SAT goes up, we lean more into the lowpassed version
                        const float tiltAmount = juce::jmap (satAmount, 0.0f, 1.0f, 0.0f, 0.85f);
                        const float tilted     = yPre + tiltAmount * (low - yPre);

                        // Smooth drive growth – no "nothing, nothing, BOOM"
                        const float drive = 1.0f + 5.0f * std::pow (satAmount, 1.3f);

                        float driven = std::tanh (tilted * drive);

                        // Simple drive compensation so it doesn't jump in level insanely
                        const float driveComp = 1.0f + 0.6f * (drive - 1.0f);
                        driven /= driveComp;

                        // Dry/wet mix with gentle curve so low SAT already does something
                        const float mix = std::pow (satAmount, 1.1f);
                        y = yPre + mix * (driven - yPre);

                        float makeupDb = 0.0f;
                        if (satAmount > 0.5f)
                        {
                            float t = (satAmount - 0.5f) / 0.5f; // 0..1 from 50% to 100%
                            t = juce::jlimit (0.0f, 1.0f, t);
                            makeupDb = 0.8f * t;
                        }
                        const float makeup = juce::Decibels::decibelsToGain (makeupDb);
                        y *= makeup;
                    }

                    // Post-gain (Fruity-null alignment)
                    y *= g;

                    // Hard ceiling in OS world
                    if (y >  1.0f) y =  1.0f;
                    if (y < -1.0f) y = -1.0f;

                    samples[i] = y;
                }
            }
        }

        // Downsample back into original buffer
        oversampler->processSamplesDown (block);

        // FINAL SAFETY CEILING AT BASE RATE (catches intersample overs)
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* s = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                float y = s[i];

                if (y >  1.0f) y =  1.0f;
                if (y < -1.0f) y = -1.0f;

                s[i] = y;
            }
        }
    }
    else
    {
        // NO OVERSAMPLING – original behaviour at base rate
        if (useLimiter)
        {
            // LIMIT MODE
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* samples = buffer.getWritePointer (ch);

                for (int i = 0; i < numSamples; ++i)
                {
                    float y = samples[i];

                    // Limiter (0 lookahead)
                    y = processLimiterSample (y);

                    // Alignment + final hard safety
                    y *= g;

                    if (y >  1.0f) y =  1.0f;
                    if (y < -1.0f) y = -1.0f;

                    samples[i] = y;
                }
            }
        }
        else
        {
            // CLIP / SAT MODE
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* samples = buffer.getWritePointer (ch);

                for (int i = 0; i < numSamples; ++i)
                {
                    float y = samples[i];

                    // SATURATION (pre-clip), only active when satAmount > 0
                    if (satAmount > 0.0f)
                    {
                        auto& sat = satStates[(size_t) ch];

                        // Curved auto-trim: gentle at start, stronger at the top
                        const float tTrim     = std::pow (satAmount, 1.3f);
                        const float satTrimDb = -2.5f * tTrim; // same max, slower onset
                        const float satTrim   = juce::Decibels::decibelsToGain (satTrimDb);

                        float yPre = y * satTrim;

                        // Low-tilt bass emphasis for the drive
                        sat.low = satLowAlpha * sat.low + (1.0f - satLowAlpha) * yPre;
                        const float low = sat.low;

                        // As SAT goes up, we lean more into the lowpassed version
                        const float tiltAmount = juce::jmap (satAmount, 0.0f, 1.0f, 0.0f, 0.85f);
                        const float tilted     = yPre + tiltAmount * (low - yPre);

                        // Smooth drive growth – no "nothing, nothing, BOOM"
                        const float drive = 1.0f + 5.0f * std::pow (satAmount, 1.3f);

                        float driven = std::tanh (tilted * drive);

                        // Simple drive compensation so it doesn't jump in level insanely
                        const float driveComp = 1.0f + 0.6f * (drive - 1.0f);
                        driven /= driveComp;

                        // Dry/wet mix with gentle curve so low SAT already does something
                        const float mix = std::pow (satAmount, 1.1f);
                        y = yPre + mix * (driven - yPre);

                        float makeupDb = 0.0f;
                        if (satAmount > 0.5f)
                        {
                            float t = (satAmount - 0.5f) / 0.5f; // 0..1 from 50% to 100%
                            t = juce::jlimit (0.0f, 1.0f, t);
                            makeupDb = 0.8f * t;
                        }
                        const float makeup = juce::Decibels::decibelsToGain (makeupDb);
                        y *= makeup;
                    }

                    // Post-gain (Fruity-null alignment)
                    y *= g;

                    // Hard ceiling
                    if (y >  1.0f) y =  1.0f;
                    if (y < -1.0f) y = -1.0f;

                    samples[i] = y;
                }
            }
        }
    }

    //==========================================================
    // METERING PASS (base rate, after distortion + final ceiling)
    //   - blockMax for burn + LUFS gate
    //   - K-weighted LUFS for GUI
    //==========================================================
    // K-weight filter coeffs (48 kHz reference; close enough)
    // Stage 1 (shelving) coefficients
    constexpr float k_b0a =  1.53512485958697f;
    constexpr float k_b1a = -2.69169618940638f;
    constexpr float k_b2a =  1.19839281085285f;
    constexpr float k_a1a = -1.69065929318241f;
    constexpr float k_a2a =  0.73248077421585f;

    // Stage 2 (RLB high-pass)
    constexpr float k_b0b =  1.0f;
    constexpr float k_b1b = -2.0f;
    constexpr float k_b2b =  1.0f;
    constexpr float k_a1b = -1.99004745483398f;
    constexpr float k_a2b =  0.99007225036621f;

    float  blockMax      = 0.0f;
    double sumSquaresK   = 0.0;
    const  int totalSamplesK = juce::jmax (1, numSamples * juce::jmax (1, numChannels));

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* samples = buffer.getWritePointer (ch);
        auto&  kf      = kFilterStates[(size_t) ch];

        for (int i = 0; i < numSamples; ++i)
        {
            float y = samples[i];

            // Track peak for GUI burn + gating
            const float ay = std::abs (y);
            if (ay > blockMax)
                blockMax = ay;

            // --- K-weighted meter path ---
            float xk = y;

            // Stage 1
            float v1 = xk - k_a1a * kf.z1a - k_a2a * kf.z2a;
            float y1 = k_b0a * v1 + k_b1a * kf.z1a + k_b2a * kf.z2a;
            kf.z2a = kf.z1a;
            kf.z1a = v1;

            // Stage 2
            float v2 = y1 - k_a1b * kf.z1b - k_a2b * kf.z2b;
            float y2 = k_b0b * v2 + k_b1b * kf.z1b + k_b2b * kf.z2b;
            kf.z2b = kf.z1b;
            kf.z1b = v2;

            sumSquaresK += (double) (y2 * y2);
        }
    }

    //==========================================================
    // Update GUI burn meter (0..1) from blockMax
    //==========================================================
    float normPeak = (blockMax - 0.90f) / 0.08f;   // 0.90 -> 0, 0.98 -> 1
    normPeak = juce::jlimit (0.0f, 1.0f, normPeak);
    normPeak = std::pow (normPeak, 2.5f);          // make mid-range calmer

    const float previousBurn = guiBurn.load();
    const float smoothedBurn = 0.25f * previousBurn + 0.75f * normPeak;
    guiBurn.store (smoothedBurn);

    //==========================================================
    // Short-term LUFS (~3 s window, like "short term" meters)
    //   + signal gating envelope (for hiding the meter)
    //==========================================================
    if (sampleRate <= 0.0)
        sampleRate = 44100.0f;

    const float blockDurationSec = (float) numSamples / (float) sampleRate;

    // Exponential integrator approximating a 3 s short-term window
    const float tauShortSec = 3.0f; // ITU short-term ≈ 3 s
    float alphaMs = 0.0f;
    if (tauShortSec > 0.0f)
        alphaMs = 1.0f - std::exp (-blockDurationSec / tauShortSec);
    alphaMs = juce::jlimit (0.0f, 1.0f, alphaMs);

    float blockMs = 0.0f;
    if (totalSamplesK > 0 && sumSquaresK > 0.0)
        blockMs = (float) (sumSquaresK / (double) totalSamplesK);

    if (! std::isfinite (blockMs) || blockMs < 0.0f)
        blockMs = 0.0f;

    // Update short-term mean-square
    if (blockMs <= 0.0f)
    {
        // decay towards silence
        lufsMeanSquare *= (1.0f - alphaMs);
    }
    else
    {
        lufsMeanSquare = (1.0f - alphaMs) * lufsMeanSquare + alphaMs * blockMs;
    }

    if (lufsMeanSquare < 1.0e-12f)
        lufsMeanSquare = 1.0e-12f;

    // ITU-style: L = -0.691 + 10 * log10(z)
    float lufs = -0.691f + 10.0f * std::log10 (lufsMeanSquare);
    if (! std::isfinite (lufs))
        lufs = -60.0f;

    // --- Calibration offset to sit on top of MiniMeters short-term ---
    constexpr float lufsCalibrationOffset = 3.0f; // tweak if needed
    lufs += lufsCalibrationOffset;

    // clamp to a sane display range
    lufs = juce::jlimit (-60.0f, 6.0f, lufs);

    // --- Use the calibrated block energy for gate logic ---
    float blockLufs = -60.0f;
    if (blockMs > 0.0f)
    {
        float tmp = -0.691f + 10.0f * std::log10 (blockMs);
        if (std::isfinite (tmp))
            blockLufs = juce::jlimit (-80.0f, 6.0f, tmp + lufsCalibrationOffset);
    }

    // Treat as "has signal" if:
    //   - block short-term LUFS above ~ -60
    //   OR
    //   - raw peak above ~ -40 dBFS (0.01 linear)
    const bool hasSignalNow =
        (blockLufs > -60.0f) ||
        (blockMax > 0.01f);

    // Smooth gate envelope so LUFS label doesn't flicker
    const float prevEnv   = guiSignalEnv.load();
    const float gateAlpha = 0.25f;
    const float targetEnv = hasSignalNow ? 1.0f : 0.0f;
    const float newEnv    = (1.0f - gateAlpha) * prevEnv + gateAlpha * targetEnv;
    guiSignalEnv.store (newEnv);

    //==========================================================
    // GUI LUFS readout – slower bar so it feels like a ST meter
    //==========================================================
    const float prevLufs   = guiLufs.load();
    const float lufsAlpha  = 0.40f;  // slower now
    const float lufsSmooth = (1.0f - lufsAlpha) * prevLufs + lufsAlpha * lufs;

    guiLufs.store (lufsSmooth);
}

//==============================================================
// Editor
//==============================================================
juce::AudioProcessorEditor* FruityClipAudioProcessor::createEditor()
{
    return new FruityClipAudioProcessorEditor (*this);
}

//==============================================================
// Metadata
//==============================================================
const juce::String FruityClipAudioProcessor::getName() const      { return "GOREKLIPER"; }
bool FruityClipAudioProcessor::acceptsMidi() const                { return false; }
bool FruityClipAudioProcessor::producesMidi() const               { return false; }
bool FruityClipAudioProcessor::isMidiEffect() const               { return false; }
double FruityClipAudioProcessor::getTailLengthSeconds() const     { return 0.0; }

//==============================================================
// Programs
//==============================================================
int FruityClipAudioProcessor::getNumPrograms()                    { return 1; }
int FruityClipAudioProcessor::getCurrentProgram()                 { return 0; }
void FruityClipAudioProcessor::setCurrentProgram (int)            {}
const juce::String FruityClipAudioProcessor::getProgramName (int) { return {}; }
void FruityClipAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================
// State
//==============================================================
void FruityClipAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void FruityClipAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

//==============================================================
// Entry point
//==============================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FruityClipAudioProcessor();
}

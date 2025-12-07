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
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f, 1.0f),
        0.0f));

    // Middle finger – OTT amount (0..1)
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "ottAmount", "OTT",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f, 1.0f),
        0.0f));

    // Right finger – saturation amount (0..1)
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "satAmount", "Saturation",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f, 1.0f),
        0.0f));

    // Mode toggle – clipper vs limiter
    params.push_back (std::make_unique<juce::AudioParameterBool>(
        "useLimiter", "Limiter Mode",
        false));

    // Oversampling mode
    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        "oversampleMode", "Oversampling",
        juce::StringArray { "x1", "x2", "x4", "x8", "x16" },
        0));

    return { params.begin(), params.end() };
}

//==============================================================
// Construction / destruction
//==============================================================
FruityClipAudioProcessor::FruityClipAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // Fruity soft-clip threshold at 0.5
    thresholdLinear = 0.5f;

    // OTT high-pass split around ~150 Hz, 1-pole smoothing
    const double cutoffHz = 150.0;
    const double t        = std::exp (-2.0 * juce::MathConstants<double>::pi * cutoffHz / 44100.0);
    ottAlpha              = (float) t;

    // Prepare K-meter filter state vectors empty
    resetKFilterState (2);
    resetOttState (2);
}

FruityClipAudioProcessor::~FruityClipAudioProcessor() = default;

//==============================================================
// Basic AudioProcessor overrides
//==============================================================
void FruityClipAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate   = (newSampleRate > 0.0 ? newSampleRate : 44100.0);
    limiterGain  = 1.0f;
    maxBlockSize = juce::jmax (1, samplesPerBlock);

    // ~50 ms release for limiter
    const double releaseMs = 50.0;
    limiterReleaseCo = (float) std::exp (-1.0 / ((releaseMs / 1000.0) * sampleRate));

    // Reset LUFS averaging
    lufsMeanSquare = 1.0e-6f;

    // Recreate oversampling if needed
    auto* osModeParam = parameters.getRawParameterValue ("oversampleMode");
    const int osIndex = osModeParam ? (int) osModeParam->load() : 0;

    updateOversampling (osIndex, getTotalNumOutputChannels());
}

void FruityClipAudioProcessor::releaseResources()
{
}

// We just declare stereo, but let the host handle more if needed.
bool FruityClipAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Must have same layout on input and output
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    // Allow mono or stereo only
    auto main = layouts.getMainOutputChannelSet();
    return (main == juce::AudioChannelSet::mono()
         || main == juce::AudioChannelSet::stereo());
}

//==============================================================
// Fruity-ish soft clip – used for main clip and sat curve
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
    const float y = threshold + (1.0f - threshold) * std::tanh (t * 2.0f);

    return sign * y;
}

//==============================================================
// Limiter – 0 lookahead, simple gain computer with release
//==============================================================
float FruityClipAudioProcessor::processLimiterSample (float x)
{
    const float ax = std::abs (x);

    if (ax > 1.0f)
    {
        float needed = 1.0f / (ax + 1.0e-20f);
        if (needed < limiterGain)
            limiterGain = needed;
    }
    else
    {
        limiterGain = limiterGain + (1.0f - limiterGain) * (1.0f - limiterReleaseCo);
    }

    return x * limiterGain;
}

//==============================================================
// Oversampling config helper
//==============================================================
void FruityClipAudioProcessor::updateOversampling (int osIndex, int numChannels)
{
    osIndex = juce::jlimit (0, 4, osIndex);
    if (osIndex == currentOversampleIndex && oversampler)
        return;

    currentOversampleIndex = osIndex;

    const int numStages = osIndex; // 0->x1, 1->x2, 2->x4, 3->x8, 4->x16

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
// Reset helpers
//==============================================================
void FruityClipAudioProcessor::resetKFilterState (int numChannels)
{
    kFilterStates.clear();
    kFilterStates.resize ((size_t) juce::jmax (1, numChannels));
    for (auto& st : kFilterStates)
    {
        st.z1a = st.z2a = 0.0f;
        st.z1b = st.z2b = 0.0f;
    }
}

void FruityClipAudioProcessor::resetOttState (int numChannels)
{
    ottStates.clear();
    ottStates.resize ((size_t) juce::jmax (1, numChannels));
    for (auto& st : ottStates)
        st.low = 0.0f;
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

    // Keep oversampling in sync with parameter
    if (osIndexParam != currentOversampleIndex || (! oversampler && osIndexParam > 0))
        updateOversampling (osIndexParam, numChannels);

    if (oversampler && maxBlockSize < numSamples)
    {
        maxBlockSize = numSamples;
        oversampler->initProcessing ((size_t) maxBlockSize);
    }

    //==========================================================
    // PRE-CHAIN: INPUT GAIN + OTT (always at base rate)
    //==========================================================
    double sumSqOriginal = 0.0;
    double sumSqOtt      = 0.0;

    if (ottAmount <= 0.0f)
    {
        // OTT bypassed: just apply input gain, keep OTT gain = 1
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* s = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                float y = s[i] * inputGain;
                sumSqOriginal += (double) (y * y);
                s[i] = y;
            }
        }

        lastOttGain = 1.0f;
    }
    else
    {
        // Work in a temp buffer so we can RMS-match OTT vs original
        juce::AudioBuffer<float> tmp (numChannels, numSamples);
        tmp.makeCopyOf (buffer);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* s = tmp.getWritePointer (ch);
            auto&  ott = ottStates[(size_t) ch];

            float low = ott.low;

            for (int i = 0; i < numSamples; ++i)
            {
                // Input gain
                float x = s[i] * inputGain;

                // Original energy (pre-OTT)
                sumSqOriginal += (double) (x * x);

                // One-pole split: low band (≈0–150 Hz) + high band
                low  = ottAlpha * low + (1.0f - ottAlpha) * x;
                float hiDry = x - low;

                // Simple envelope proxy from instantaneous magnitude
                float env = std::abs (hiDry) + 1.0e-6f;

                // Upward/downward style shaping factor
                float dynGain = 1.0f;

                if (env < 1.0f)
                {
                    // Quiet highs → lift tails/room
                    float t = juce::jlimit (0.0f, 1.0f, 1.0f - env);
                    dynGain += t * 0.5f * ottAmount;  // more tails when ottAmount is high
                }
                else
                {
                    // Loud highs → control a bit
                    float t = juce::jlimit (0.0f, 1.0f, env - 1.0f);
                    const float minGain = 0.6f;
                    dynGain -= t * (1.0f - minGain) * ottAmount;
                }

                float hiProc = hiDry * dynGain;

                // Soft rounding so it’s not harsh
                hiProc = std::tanh (hiProc * (1.0f + 0.5f * ottAmount));

                // Static "air" / high boost
                const float staticHighBoost = 1.0f + 2.0f * ottAmount; // more highs as knob goes up
                hiProc *= staticHighBoost;

                // Crossfade between dry high and processed high
                float hiMix = hiDry + ottAmount * (hiProc - hiDry);

                float y = low + hiMix;

                sumSqOtt += (double) (y * y);
                s[i]      = y;
            }

            ott.low = low;
        }

        // RMS gain-match OTT vs original so knob isn’t a fake loudness boost
        const int totalSamplesOtt = juce::jmax (1, numSamples * juce::jmax (1, numChannels));

        float ottGain = 1.0f;
        if (sumSqOtt > 0.0 && sumSqOriginal > 0.0)
        {
            float rmsOriginal = (float) std::sqrt (sumSqOriginal / (double) totalSamplesOtt + 1.0e-20);
            float rmsOtt      = (float) std::sqrt (sumSqOtt      / (double) totalSamplesOtt + 1.0e-20);

            if (rmsOtt > 0.0f)
                ottGain = rmsOriginal / rmsOtt;
        }

        // Smooth so it doesn’t chatter
        const float ottSmooth = 0.35f;
        lastOttGain = (1.0f - ottSmooth) * lastOttGain + ottSmooth * ottGain;

        buffer.makeCopyOf (tmp);
        buffer.applyGain (lastOttGain);
    }

    //==========================================================
    // DISTORTION CHAIN (SAT/CLIP or LIMITER)
    //   - In oversampled mode, this runs at higher rate
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

                    y = processLimiterSample (y);
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
                        // Static duck so it doesn’t just get louder when you crank SAT.
                        const float satTrimDb = -3.0f * satAmount;           // up to about -3 dB at 100%
                        const float satTrim   = juce::Decibels::decibelsToGain (satTrimDb);
                        y *= satTrim;

                        // Drive into the curve – starts mild, gets hotter as SAT increases.
                        const float driveDb = 3.0f + 9.0f * satAmount;       // ~3 dB low, ~12 dB high
                        const float drive   = juce::Decibels::decibelsToGain (driveDb);
                        const float in      = y * drive;

                        // Rounded "burn" curve:
                        // 1) Fruity-style soft clip with moving threshold
                        const float thr = juce::jmap (satAmount, 0.75f, 0.25f);
                        float shaped = fruitySoftClipSample (in, thr);

                        // 2) Extra rounding so it feels smooth / melted, not spiky
                        shaped = 0.7f * shaped + 0.3f * std::tanh (shaped);

                        // Bass emphasis – more low / low-mid weight as SAT goes up.
                        const float bassLift = 1.0f + 0.35f * satAmount;     // up to ~ +3 dB-ish in the lows
                        float ySat = shaped * bassLift;

                        // Undo part of the drive so overall loudness stays controlled.
                        const float invDrive = 1.0f / juce::jmax (drive, 1.0e-6f);
                        ySat *= invDrive;

                        // Mix: shaped so 0–50% already feels like "something".
                        float mix = std::pow (satAmount, 0.7f);              // 0..1, biased to kick in earlier
                        mix = juce::jlimit (0.0f, 1.0f, mix);

                        // Final blend
                        y = y + mix * (ySat - y);
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

        // Back down to base rate
        oversampler->processSamplesDown (block);
    }
    else
    {
        // NO OVERSAMPLING – base rate
        if (useLimiter)
        {
            // LIMIT MODE
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* samples = buffer.getWritePointer (ch);

                for (int i = 0; i < numSamples; ++i)
                {
                    float y = samples[i];

                    y = processLimiterSample (y);
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
                        const float satTrimDb = -3.0f * satAmount;           // up to about -3 dB at 100%
                        const float satTrim   = juce::Decibels::decibelsToGain (satTrimDb);
                        y *= satTrim;

                        const float driveDb = 3.0f + 9.0f * satAmount;       // ~3 dB low, ~12 dB high
                        const float drive   = juce::Decibels::decibelsToGain (driveDb);
                        const float in      = y * drive;

                        const float thr = juce::jmap (satAmount, 0.75f, 0.25f);
                        float shaped = fruitySoftClipSample (in, thr);

                        shaped = 0.7f * shaped + 0.3f * std::tanh (shaped);

                        const float bassLift = 1.0f + 0.35f * satAmount;
                        float ySat = shaped * bassLift;

                        const float invDrive = 1.0f / juce::jmax (drive, 1.0e-6f);
                        ySat *= invDrive;

                        float mix = std::pow (satAmount, 0.7f);
                        mix = juce::jlimit (0.0f, 1.0f, mix);

                        y = y + mix * (ySat - y);
                    }

                    y *= g;

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
        auto& kf = kFilterStates[(size_t) ch];
        float* samples = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = samples[i];

            // blockMax over absolute signal
            float ax = std::abs (x);
            if (ax > blockMax)
                blockMax = ax;

            // Stage 1
            float v1 = x - k_a1a * kf.z1a - k_a2a * kf.z2a;
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
        sampleRate = 44100.0;

    const float invTotal = 1.0f / (float) totalSamplesK;
    float blockMs = (float) (sumSquaresK * invTotal);

    // Attack / release for running mean-square over ~3 seconds
    const float windowSec = 3.0f;
    const float alphaMs   = 1.0f - std::exp (-1.0f / (windowSec * (float) sampleRate));

    if (blockMs <= 0.0f)
    {
        // Let mean-square decay slowly when silent
        lufsMeanSquare *= (1.0f - 0.25f * alphaMs);
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
    //   - blockLufs > -50 dB
    //   - OR the raw block peak is above some small level
    bool hasSignal = (blockLufs > -50.0f) || (blockMax > 0.01f);

    // Smooth an envelope for gating
    const float attackGate  = 0.25f;
    const float releaseGate = 0.02f;

    float gateEnv = guiSignalEnv.load();
    const float target = hasSignal ? 1.0f : 0.0f;
    const float coeff  = hasSignal ? attackGate : releaseGate;

    gateEnv = gateEnv + coeff * (target - gateEnv);
    guiSignalEnv.store (gateEnv);

    // Finally, write LUFS to GUI atomic
    guiLufs.store (lufs);
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
    if (auto xml = parameters.state.createXml())
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

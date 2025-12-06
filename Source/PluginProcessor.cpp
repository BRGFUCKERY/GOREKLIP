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

    // SAT – 0..1
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "satAmount", "Saturation Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // SILK – 0..1
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "silkAmount", "Silk Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // MODE – 0 = clipper, 1 = limiter
    params.push_back (std::make_unique<juce::AudioParameterBool>(
        "useLimiter", "Use Limiter", false));

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
// Basic AudioProcessor overrides
//==============================================================
void FruityClipAudioProcessor::prepareToPlay (double newSampleRate, int /*samplesPerBlock*/)
{
    sampleRate = (newSampleRate > 0.0 ? newSampleRate : 44100.0);
    limiterGain = 1.0f;

    // ~50 ms release for limiter
    const float releaseTimeSec = 0.050f;
    limiterReleaseCo = std::exp (-1.0f / (releaseTimeSec * (float) sampleRate));

    // Reset K-weight filter + LUFS state
    resetKFilterState (getTotalNumOutputChannels());
    lufsMeanSquare = 1.0e-6f;
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

    auto* gainParam = parameters.getRawParameterValue ("inputGain");
    auto* satParam  = parameters.getRawParameterValue ("satAmount");
    auto* silkParam = parameters.getRawParameterValue ("silkAmount");
    auto* modeParam = parameters.getRawParameterValue ("useLimiter");

    const float inputGainDb   = gainParam  ? gainParam->load()   : 0.0f;
    const float satAmountRaw  = satParam   ? satParam->load()    : 0.0f;
    const float silkAmountRaw = silkParam  ? silkParam->load()   : 0.0f;
    const bool  useLimiter    = modeParam  ? (modeParam->load() >= 0.5f) : false;

    const float satAmount  = juce::jlimit (0.0f, 1.0f, satAmountRaw);
    const float silkAmount = juce::jlimit (0.0f, 1.0f, silkAmountRaw);

    //==========================================================
    // SAT global "auto gain"
    //==========================================================
    const float satCompDb = 0.0f; // we’re doing the tiny trim inside SAT block

    // User gain (your left finger) – used directly in LIMIT mode
    const float inputGainLimiter = juce::Decibels::decibelsToGain (inputGainDb);

    // In CLIP/SAT mode, user gain + static SAT compensation (pre-sat)
    const float inputGainClip    = juce::Decibels::decibelsToGain (inputGainDb + satCompDb);

    const float silkBlend = silkAmount * silkAmount; // keep first half subtle
    const float g         = postGain;                // Fruity-null alignment

    float blockMax = 0.0f; // For GUI burn (post processing)

    //==========================================================
    // K-weighted LUFS accumulation
    // Using ITU-R BS.1770-style K-weighting:
    //   Stage 1 (shelving) + Stage 2 (high-pass / RLB),
    // with coefficients given for 48 kHz (good approximation).
    //==========================================================
    // Stage 1 (head / shelving) coefficients (48 kHz reference)
    // From ITU-R BS.1770 tables:contentReference[oaicite:0]{index=0}
    constexpr float k_b0a =  1.53512485958697f;
    constexpr float k_b1a = -2.69169618940638f;
    constexpr float k_b2a =  1.19839281085285f;
    constexpr float k_a1a = -1.69065929318241f;
    constexpr float k_a2a =  0.73248077421585f;

    // Stage 2 (RLB / high-pass) coefficients (48 kHz reference):contentReference[oaicite:1]{index=1}
    constexpr float k_b0b =  1.0f;
    constexpr float k_b1b = -2.0f;
    constexpr float k_b2b =  1.0f;
    constexpr float k_a1b = -1.99004745483398f;
    constexpr float k_a2b =  0.99007225036621f;

    double sumSquaresK = 0.0;
    const int totalSamplesK = juce::jmax (1, numSamples * juce::jmax (1, numChannels));

    if (useLimiter)
    {
        //======================================================
        // LIMIT MODE:
        //   GAIN -> SILK -> LIMITER -> POSTGAIN -> HARD CLIP
        //======================================================
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* samples = buffer.getWritePointer (ch);
            auto&  kf      = kFilterStates[(size_t) ch];

            for (int i = 0; i < numSamples; ++i)
            {
                float y = samples[i] * inputGainLimiter;

                // 1) SILK (pre-dynamics, gentle colour)
                if (silkBlend > 0.0f)
                {
                    const float silkFull = silkCurveFull (y);
                    y = y + silkBlend * (silkFull - y);
                }

                // 2) Limiter (0 lookahead)
                y = processLimiterSample (y);

                // 3) Alignment + final hard safety
                y *= g;

                if (y >  1.0f) y =  1.0f;
                if (y < -1.0f) y = -1.0f;

                const float a = std::abs (y);
                if (a > blockMax)
                    blockMax = a;

                // --- K-weighted meter path (does NOT affect audio) ---
                float xk = y;

                // Stage 1 (shelving) – Direct Form II transposed
                float v1 = xk - k_a1a * kf.z1a - k_a2a * kf.z2a;
                float y1 = k_b0a * v1 + k_b1a * kf.z1a + k_b2a * kf.z2a;
                kf.z2a = kf.z1a;
                kf.z1a = v1;

                // Stage 2 (high-pass / RLB)
                float v2 = y1 - k_a1b * kf.z1b - k_a2b * kf.z2b;
                float y2 = k_b0b * v2 + k_b1b * kf.z1b + k_b2b * kf.z2b;
                kf.z2b = kf.z1b;
                kf.z1b = v2;

                sumSquaresK += (double) (y2 * y2);

                samples[i] = y;
            }
        }
    }
    else
    {
        //======================================================
        // CLIP / SAT MODE:
        // (Gain) -> SILK -> SAT (with tiny auto-trim) -> POSTGAIN -> HARD CLIP
        //======================================================
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* samples = buffer.getWritePointer (ch);
            auto&  kf      = kFilterStates[(size_t) ch];

            for (int i = 0; i < numSamples; ++i)
            {
                // 0) Total input gain BEFORE SILK/SAT (user + static pre-gain)
                float y = samples[i] * inputGainClip;

                // ------------------------------------------------
                // 1) SILK (pre-clip transformer-ish colour)
                // ------------------------------------------------
                if (silkBlend > 0.0f)
                {
                    const float silkFull = silkCurveFull (y);
                    y = y + silkBlend * (silkFull - y);
                }

                // ------------------------------------------------
                // 2) SATURATION – rounded, bass-thick curve
                //    Only active when satAmount > 0.0
                // ------------------------------------------------
                if (satAmount > 0.0f)
                {
                    // Tiny SAT auto-trim (max about -2.5 dB)
                    const float satTrimDb = -2.5f * satAmount;   // 0 .. -2.5 dB
                    const float satTrim   = juce::Decibels::decibelsToGain (satTrimDb);
                    y *= satTrim;

                    // Threshold moves as SAT increases
                    // sat = 0   -> thr = 0.55
                    // sat = 1   -> thr = 0.15
                    const float thr = juce::jmap (satAmount, 0.55f, 0.15f);

                    // Rounded soft clip
                    float y0 = fruitySoftClipSample (y, thr);

                    // Bass lift – still there but a bit tamer
                    const float bassLift = 1.0f + 0.15f * satAmount;
                    y0 *= bassLift;

                    // Blend: strong enough to hear, not insane loudness jump
                    y = y + (satAmount * 1.25f) * (y0 - y);
                }

                // ------------------------------------------------
                // 3) Post-gain (Fruity-null alignment)
                // ------------------------------------------------
                y *= g;

                // ------------------------------------------------
                // 4) Hard ceiling at 0 dBFS
                // ------------------------------------------------
                if (y >  1.0f) y =  1.0f;
                if (y < -1.0f) y = -1.0f;

                const float absY = std::abs (y);
                if (absY > blockMax)
                    blockMax = absY;

                // --- K-weighted meter path (does NOT affect audio) ---
                float xk = y;

                // Stage 1 (shelving)
                float v1 = xk - k_a1a * kf.z1a - k_a2a * kf.z2a;
                float y1 = k_b0a * v1 + k_b1a * kf.z1a + k_b2a * kf.z2a;
                kf.z2a = kf.z1a;
                kf.z1a = v1;

                // Stage 2 (high-pass / RLB)
                float v2 = y1 - k_a1b * kf.z1b - k_a2b * kf.z2b;
                float y2 = k_b0b * v2 + k_b1b * kf.z1b + k_b2b * kf.z2b;
                kf.z2b = kf.z1b;
                kf.z1b = v2;

                sumSquaresK += (double) (y2 * y2);

                samples[i] = y;
            }
        }
    }

    //==========================================================
    // Update GUI burn meter (0..1)
    //==========================================================
    float normPeak = (blockMax - 0.90f) / 0.08f;   // 0.90 -> 0, 0.98 -> 1
    normPeak = juce::jlimit (0.0f, 1.0f, normPeak);
    normPeak = std::pow (normPeak, 2.5f);          // make mid-range calmer

    const float previousBurn = guiBurn.load();
    const float smoothedBurn = 0.25f * previousBurn + 0.75f * normPeak;
    guiBurn.store (smoothedBurn);

    //==========================================================
    // Real-ish K-weighted “momentary LUFS” (slower, ~400 ms)
    //==========================================================
    if (sampleRate <= 0.0)
        sampleRate = 44100.0;

    const float blockDurationSec = (float) numSamples / (float) sampleRate;
    const float targetWindowSec  = 0.400f; // ITU momentary window ~400 ms:contentReference[oaicite:2]{index=2}

    float alpha = 0.0f;
    if (targetWindowSec > 0.0f)
        alpha = blockDurationSec / targetWindowSec;

    alpha = juce::jlimit (0.0f, 1.0f, alpha);

    if (totalSamplesK > 0 && sumSquaresK > 0.0)
    {
        const float blockMs = (float) (sumSquaresK / (double) totalSamplesK);

        if (std::isfinite (blockMs) && blockMs > 0.0f)
        {
            // Single-pole smoothing towards ~400 ms integration
            lufsMeanSquare = (1.0f - alpha) * lufsMeanSquare + alpha * blockMs;
        }
    }
    else
    {
        // When nothing is happening, decay towards silence
        lufsMeanSquare *= (1.0f - alpha);
        if (lufsMeanSquare < 1.0e-10f)
            lufsMeanSquare = 1.0e-10f;
    }

    float lufs = -60.0f;
    if (lufsMeanSquare > 0.0f)
    {
        // ITU-style: L = -0.691 + 10 * log10(z):contentReference[oaicite:3]{index=3}
        lufs = -0.691f + 10.0f * std::log10 (lufsMeanSquare);
        if (! std::isfinite (lufs))
            lufs = -60.0f;
    }

    // Clamp to a sensible visual range
    lufs = juce::jlimit (-60.0f, 3.0f, lufs);

    // Smooth a bit more so it doesn’t shimmer too fast
    const float prevLufs = guiLufs.load();
    const float lufsAlpha = 0.5f; // 0.5 = still pretty snappy but less jittery
    const float smoothedLufs = (1.0f - lufsAlpha) * prevLufs + lufsAlpha * lufs;

    guiLufs.store (smoothedLufs);
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
//==============

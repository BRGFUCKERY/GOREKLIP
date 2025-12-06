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

    // SATURATION – 0..1
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
// Constructor / destructor
//==============================================================
FruityClipAudioProcessor::FruityClipAudioProcessor()
    : juce::AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // Default threshold equivalent to Fruity Soft Clipper-ish
    thresholdLinear = juce::Decibels::decibelsToGain (-6.0f);
}

FruityClipAudioProcessor::~FruityClipAudioProcessor() = default;

//==============================================================
// Oversampling config helper
//==============================================================
void FruityClipAudioProcessor::updateOversampling (int osIndex, int numChannels)
{
    // osIndex: 0:x1, 1:x2, 2:x4, 3:x8, 4:x16
    currentOversampleIndex = juce::jlimit (0, 4, osIndex);

    int numStages = 0; // 2^stages
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
}

void FruityClipAudioProcessor::releaseResources()
{
}

//==============================================================
// Bus layout
//==============================================================
bool FruityClipAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getChannelSet (true,  0);
    const auto& mainOut = layouts.getChannelSet (false, 0);

    // must be the same and either mono or stereo
    if (! mainIn.isDisabled()
        && mainIn == mainOut
        && (mainOut == juce::AudioChannelSet::mono()
            || mainOut == juce::AudioChannelSet::stereo()))
    {
        return true;
    }

    return false;
}

//==============================================================
// K-weight + LUFS helpers
//==============================================================
void FruityClipAudioProcessor::resetKFilterState (int numChannels)
{
    kFilterStates.clear();
    kFilterStates.resize ((size_t) juce::jmax (1, numChannels));

    for (auto& s : kFilterStates)
    {
        s.z1a = s.z2a = 0.0f;
        s.z1b = s.z2b = 0.0f;
    }
}

void FruityClipAudioProcessor::resetOttState (int numChannels)
{
    ottStates.clear();
    ottStates.resize ((size_t) juce::jmax (1, numChannels));

    for (auto& s : ottStates)
        s.lowZ = 0.0f;
}

//==============================================================
// processBlock – main audio
//==============================================================
void FruityClipAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    if (numChannels == 0 || numSamples == 0)
        return;

    // Pull parameters
    auto* inputGainParam   = parameters.getRawParameterValue ("inputGain");
    auto* silkParam        = parameters.getRawParameterValue ("silkAmount");
    auto* ottParam         = parameters.getRawParameterValue ("ottAmount");
    auto* satParam         = parameters.getRawParameterValue ("satAmount");
    auto* useLimiterParam  = parameters.getRawParameterValue ("useLimiter");
    auto* osModeParam      = parameters.getRawParameterValue ("oversampleMode");

    const float inputGainDb  = (inputGainParam  ? inputGainParam->load()  : 0.0f);
    const float silk         = (silkParam       ? silkParam->load()       : 0.0f);
    const float ott          = (ottParam        ? ottParam->load()        : 0.0f);
    const float sat          = (satParam        ? satParam->load()        : 0.0f);
    const bool  useLimiter   = (useLimiterParam ? (useLimiterParam->load() > 0.5f) : false);
    const int   osIndex      = (osModeParam     ? (int) osModeParam->load() : 0);

    // Update oversampling if needed
    if (osIndex != currentOversampleIndex)
        updateOversampling (osIndex, numChannels);

    // Input gain (pre-clip)
    const float inputGainLinear = juce::Decibels::decibelsToGain (inputGainDb);

    // Mode blend: 0 = clipper, 1 = limiter
    modeBlend = useLimiter ? 1.0f : 0.0f;

    // Update silk and OTT amount
    silkAmount = silk;
    ottAmount  = ott;
    satAmount  = sat;

    // Apply oversampling if active
    juce::AudioBuffer<float> workBuffer;
    workBuffer.makeCopyOf (buffer);

    juce::dsp::AudioBlock<float> block (workBuffer);
    juce::dsp::AudioBlock<float> osBlock (workBuffer);

    if (oversampler != nullptr && currentOversampleIndex > 0)
    {
        osBlock = oversampler->processSamplesUp (block);
    }

    // Determine processing block (oversampled or not)
    juce::dsp::AudioBlock<float>& procBlock = (oversampler != nullptr && currentOversampleIndex > 0)
                                              ? osBlock
                                              : block;

    //==========================================================
    // Main processing per-sample
    //==========================================================
    const int procNumChannels = (int) procBlock.getNumChannels();
    const int procNumSamples  = (int) procBlock.getNumSamples();

    // Safety
    if (procNumChannels == 0 || procNumSamples == 0)
    {
        buffer.clear();
        return;
    }

    // Track block max (for burn meter)
    float blockMax = 0.0f;

    // For K-weighted LUFS
    double sumSquaresK = 0.0;
    int    totalSamplesK = 0;

    for (int ch = 0; ch < procNumChannels; ++ch)
    {
        float* channelData = procBlock.getChannelPointer (ch);

        auto& kf  = kFilterStates[(size_t) juce::jmin (ch, (int) kFilterStates.size() - 1)];
        auto& ott = ottStates[(size_t) juce::jmin (ch, (int) ottStates.size() - 1)];

        for (int n = 0; n < procNumSamples; ++n)
        {
            float x = channelData[n];

            // Input gain
            x *= inputGainLinear;

            // Simple silk tilt – high-shelf-ish and low-shelf-ish combo
            if (silkAmount > 0.0f)
            {
                const float highBoost = silkAmount * 0.75f;  // more air
                const float lowCut   = silkAmount * 0.35f;   // keep some lows

                const float high = x * (1.0f + highBoost);
                const float low  = x * (1.0f - lowCut);

                x = 0.5f * (high + low);
            }

            float forOttInput = x;

            // OTT split (150 Hz)
            if (ottAmount > 0.0001f)
            {
                // one-pole lowpass for lows
                float low = ott.lowZ + ottAlpha * (forOttInput - ott.lowZ);
                ott.lowZ = low;

                // high band is residual
                float high = forOttInput - low;

                // Over-the-top style: push highs, compress lows
                float highSat = std::tanh (high * (1.0f + ottAmount * 6.0f));
                float lowSat  = std::tanh (low  * (1.0f + ottAmount * 3.0f));

                float processed = lowSat + highSat;

                // Soft blend back toward dry
                x = forOttInput + ottAmount * (processed - forOttInput);
            }

            // Saturation stage (pre-clip color)
            if (satAmount > 0.0001f)
            {
                const float drive   = 1.0f + satAmount * 8.0f;
                const float invSoft = 1.0f / (1.0f + satAmount * 2.0f);

                float driven = std::tanh (x * drive) * invSoft;

                // Simple auto-gain-ish behavior: slightly duck input as sat goes up
                const float autoGainDb = -2.0f * satAmount; // -2 dB at max
                const float autoGain   = juce::Decibels::decibelsToGain (autoGainDb);

                x = driven * autoGain;
            }

            // Clipper/limiter hybrid
            const float clipped = fruitySoftClipSample (x, thresholdLinear);

            // Simple "limiter" style: track gain for peaks and pull down faster
            float limited = clipped;
            if (modeBlend >= 0.5f)
            {
                float mag = std::abs (limited);
                if (mag > 1.0f)
                {
                    float over = mag - 1.0f;
                    float gainDrop = 1.0f / (1.0f + over * 10.0f);
                    limiterGain = juce::jmin (limiterGain, gainDrop);
                }
                else
                {
                    limiterGain = limiterGain + limiterReleaseCo * (1.0f - limiterGain);
                }

                limited *= limiterGain;
            }

            // Blend between pure clip & limiting
            const float mix = modeBlend;
            float y = clipped * (1.0f - mix) + limited * mix;

            // Track peak for GUI burn
            blockMax = juce::jmax (blockMax, std::abs (y));

            // K-weighted path for LUFS
            {
                // Coeffs match ITU-R BS.1770-ish weighting
                constexpr float k_b0a =  1.53512485958697f;
                constexpr float k_b1a = -2.69169618940638f;
                constexpr float k_b2a =  1.19839281085285f;
                constexpr float k_a1a = -1.69065929318241f;
                constexpr float k_a2a =  0.73248077421585f;

                constexpr float k_b0b =  1.0f;
                constexpr float k_b1b = -2.0f;
                constexpr float k_b2b =  1.0f;
                constexpr float k_a1b = -1.99004745483398f;
                constexpr float k_a2b =  0.99007225036621f;

                // Stage 1 (shelving)
                float v1 = y - k_a1a * kf.z1a - k_a2a * kf.z2a;
                float y1 = k_b0a * v1 + k_b1a * kf.z1a + k_b2a * kf.z2a;
                kf.z2a = kf.z1a;
                kf.z1a = v1;

                // Stage 2 (RLB high-pass)
                float v2 = y1 - k_a1b * kf.z1b - k_a2b * kf.z2b;
                float y2 = k_b0b * v2 + k_b1b * kf.z1b + k_b2b * kf.z2b;
                kf.z2b = kf.z1b;
                kf.z1b = v2;

                sumSquaresK += (double) (y2 * y2);
                ++totalSamplesK;
            }

            channelData[n] = y;
        }
    }

    // Oversampling down
    if (oversampler != nullptr && currentOversampleIndex > 0)
    {
        oversampler->processSamplesDown (block);
        buffer.makeCopyOf (workBuffer);
    }
    else
    {
        buffer.makeCopyOf (workBuffer);
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
    // K-weighted LUFS integration (short-term style ~3 s)
    //==========================================================
    if (sampleRate <= 0.0)
        sampleRate = 44100.0;

    const float blockDurationSec = (float) numSamples / (float) sampleRate;
    const float targetWindowSec  = 3.0f; // ~3 s "short-term" loudness

    float alpha = 0.0f;
    if (targetWindowSec > 0.0f)
        alpha = blockDurationSec / targetWindowSec;
    alpha = juce::jlimit (0.0f, 1.0f, alpha);

    if (totalSamplesK > 0 && sumSquaresK > 0.0)
    {
        const float blockMs = (float) (sumSquaresK / (double) totalSamplesK);

        if (std::isfinite (blockMs) && blockMs > 0.0f)
            lufsMeanSquare = (1.0f - alpha) * lufsMeanSquare + alpha * blockMs;
    }
    else
    {
        // decay towards silence
        lufsMeanSquare *= (1.0f - alpha);
        if (lufsMeanSquare < 1.0e-10f)
            lufsMeanSquare = 1.0e-10f;
    }

    float lufs = -80.0f;
    if (lufsMeanSquare > 0.0f)
    {
        // ITU-style: L = -0.691 + 10 * log10(z)
        lufs = -0.691f + 10.0f * std::log10 (lufsMeanSquare);
        if (! std::isfinite (lufs))
            lufs = -80.0f;
    }

    // clamp working range
    lufs = juce::jlimit (-80.0f, 5.0f, lufs);

    // smoothing (extra on top of the block integration)
    const float prevLufs  = guiLufs.load();
    const float lufsAlpha = 0.3f;
    float lufsSmooth      = (1.0f - lufsAlpha) * prevLufs + lufsAlpha * lufs;

    // Calibration so we can match other meters easily (Youlean, etc.)
    constexpr float lufsCalibrationDb = 1.0f; // tweak by ear vs reference
    lufsSmooth += lufsCalibrationDb;

    lufsSmooth = juce::jlimit (-80.0f, 5.0f, lufsSmooth);
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
    if (auto xml = parameters.copyState().createXml())
    {
        copyXmlToBinary (*xml, destData);
    }
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


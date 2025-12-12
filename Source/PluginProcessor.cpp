#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
constexpr float kSilkMaxDrive      = 1.20f; // saturation drive for Silk Max reference match
constexpr float kSilkMaxAsym       = 0.020f; // asymmetry offset for even harmonics
constexpr float kSilkMaxGain       = 0.95f;  // output trim to align against hardware capture
constexpr float kSilkPreEmphasisDb = 1.0f;   // gentle HF boost into the saturator
constexpr float kSilkDeEmphasisDb  = -2.0f;  // HF soften post saturation
constexpr float kSilkLowShelfDb    = 1.0f;   // low-mid weight after HF softening
}

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
        "ottAmount", "OTT Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // SAT – 0..1
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "satAmount", "Saturation Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // MODE – 0 = clipper, 1 = limiter
    params.push_back (std::make_unique<juce::AudioParameterBool>(
        "useLimiter", "Use Limiter", false));

    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        "clipMode", "Mode",
        juce::StringArray { "Digital", "Analog" }, 0));

    // OVERSAMPLE MODE – 0:x1, 1:x2, 2:x4, 3:x8, 4:x16, 5:x32, 6:x64
    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        "oversampleMode", "Oversample Mode",
        juce::StringArray { "x1", "x2", "x4", "x8", "x16", "x32", "x64" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        "lookMode", "Look Mode",
        juce::StringArray { "COOKED", "LUFS", "STATIC" }, 0));

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
    // postGain is no longer used for default hard-clip alignment.
    // We keep it as a member in case we want special modes later.
    postGain        = 1.0f;

    // Soft clip threshold (~ -6 dB at satAmount = 1)
    // (kept for future use; currently we are in pure hard-clip mode)
    thresholdLinear = juce::Decibels::decibelsToGain (-6.0f);

    juce::PropertiesFile::Options opts;
    opts.applicationName     = "GOREKLIPER";
    opts.filenameSuffix      = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.folderName          = "GOREKLIPER";

    userSettings = std::make_unique<juce::PropertiesFile> (opts);

    if (userSettings)
    {
        // ------------------------------------------------------
        // LOOK global default
        // ------------------------------------------------------
        if (! userSettings->containsKey ("lookMode"))
            userSettings->setValue ("lookMode", 0);  // 0 = Cooked

        const int storedLook = userSettings->getIntValue ("lookMode", 0);
        setStoredLookMode (storedLook);
        setLookModeIndex  (storedLook); // pushes into parameter for new instances

        // ------------------------------------------------------
        // OFFLINE oversample global default
        //   -1 = SAME (follow LIVE)
        // ------------------------------------------------------
        if (! userSettings->containsKey ("offlineOversampleIndex"))
            userSettings->setValue ("offlineOversampleIndex", -1);

        storedOfflineOversampleIndex =
            userSettings->getIntValue ("offlineOversampleIndex", -1);

        // ------------------------------------------------------
        // LIVE oversample global default
        // ------------------------------------------------------
        // We no longer store or restore a global LIVE oversample preference.
        // New instances simply use the default value of the "oversampleMode"
        // parameter (0 = 1x), and any changes are saved per-instance by the DAW.
        // Leave storedLiveOversampleIndex at its default of 0 so any legacy
        // code that reads it still sees "1x".
        storedLiveOversampleIndex = 0;
    }

    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (parameters.getParameter ("lookMode")))
    {
        const int storedIndex = juce::jlimit (0, choiceParam->choices.size() - 1, getStoredLookMode());
        setLookModeIndex (storedIndex);
    }
}

FruityClipAudioProcessor::~FruityClipAudioProcessor() = default;

FruityClipAudioProcessor::ClipMode FruityClipAudioProcessor::getClipMode() const
{
    if (auto* p = parameters.getRawParameterValue ("clipMode"))
    {
        const int idx = juce::jlimit (0, 1, (int) p->load());
        return idx == 0 ? ClipMode::Digital : ClipMode::Analog;
    }

    return ClipMode::Digital;
}

bool FruityClipAudioProcessor::isLimiterEnabled() const
{
    if (auto* p = parameters.getRawParameterValue ("useLimiter"))
        return p->load() >= 0.5f;

    return false;
}

int FruityClipAudioProcessor::getLookModeIndex() const
{
    if (auto* p = parameters.getRawParameterValue ("lookMode"))
        return (int) p->load();

    return 0;
}

void FruityClipAudioProcessor::setLookModeIndex (int newIndex)
{
    newIndex = juce::jlimit (0, 2, newIndex);

    if (auto* p = parameters.getRawParameterValue ("lookMode"))
        p->store ((float) newIndex);

    setStoredLookMode (newIndex);
}

int FruityClipAudioProcessor::getStoredLookMode() const
{
    if (userSettings)
        return userSettings->getIntValue ("lookMode", 0);
    return 0;
}

void FruityClipAudioProcessor::setStoredLookMode (int modeIndex)
{
    if (userSettings)
    {
        userSettings->setValue ("lookMode", modeIndex);
        userSettings->saveIfNeeded();
    }
}

int FruityClipAudioProcessor::getStoredOfflineOversampleIndex() const
{
    if (userSettings)
        return juce::jlimit (-1, 6,
            userSettings->getIntValue ("offlineOversampleIndex", storedOfflineOversampleIndex));

    return juce::jlimit (-1, 6, storedOfflineOversampleIndex);
}

void FruityClipAudioProcessor::setStoredOfflineOversampleIndex (int index)
{
    index = juce::jlimit (-1, 6, index);
    storedOfflineOversampleIndex = index;

    if (userSettings)
    {
        userSettings->setValue ("offlineOversampleIndex", index);
        userSettings->saveIfNeeded();
    }
}

int FruityClipAudioProcessor::getStoredLiveOversampleIndex() const
{
    return juce::jlimit (0, 6, storedLiveOversampleIndex);
}

void FruityClipAudioProcessor::setStoredLiveOversampleIndex (int index)
{
    index = juce::jlimit (0, 6, index);
    storedLiveOversampleIndex = index;
}

//==============================================================
// Oversampling config helper
//==============================================================
void FruityClipAudioProcessor::updateOversampling (int osIndex, int numChannels)
{
    // osIndex: 0=x1, 1=x2, 2=x4, 3=x8, 4=x16, 5=x32, 6=x64
    currentOversampleIndex = juce::jlimit (0, 6, osIndex);

    int numStages = 0; // factor = 2^stages
    switch (currentOversampleIndex)
    {
        case 0: numStages = 0; break; // x1 (no oversampling)
        case 1: numStages = 1; break; // x2
        case 2: numStages = 2; break; // x4
        case 3: numStages = 3; break; // x8
        case 4: numStages = 4; break; // x16
        case 5: numStages = 5; break; // x32
        case 6: numStages = 6; break; // x64
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
    lufsAverageLufs = -60.0f;

    // Reset OTT split state
    resetOttState (getTotalNumOutputChannels());

    // Reset SAT bass-tilt state
    resetSatState (getTotalNumOutputChannels());

    resetSilkState (getTotalNumOutputChannels());

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

    // One-pole lowpass for analog tone tilt (~1 kHz split at base rate)
    {
        const float fcAnalog = 1000.0f;
        const float alphaAnalog =
            std::exp (-2.0f * juce::MathConstants<float>::pi * fcAnalog / sr);
        analogToneAlpha = juce::jlimit (0.0f, 1.0f, alphaAnalog);
    }

    // Silk Max filters (split frequencies tuned for post-clip coloration)
    {
        const float preFc = 4500.0f;
        const float deFc  = 6500.0f;
        const float lowFc = 250.0f;

        silkPreAlpha = juce::jlimit (0.0f, 1.0f,
            std::exp (-2.0f * juce::MathConstants<float>::pi * preFc / sr));
        silkDeAlpha = juce::jlimit (0.0f, 1.0f,
            std::exp (-2.0f * juce::MathConstants<float>::pi * deFc / sr));
        silkLowAlpha = juce::jlimit (0.0f, 1.0f,
            std::exp (-2.0f * juce::MathConstants<float>::pi * lowFc / sr));
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

    const int currentLookMode = getLookModeIndex();
    const int clampedLook     = juce::jlimit (0, 2, currentLookMode);

    if (clampedLook != currentLookMode)
        setLookModeIndex (clampedLook);
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

void FruityClipAudioProcessor::resetSilkState (int numChannels)
{
    silkStates.clear();

    if (numChannels <= 0)
        return;

    silkStates.resize ((size_t) numChannels);

    for (auto& st : silkStates)
    {
        st.pre = 0.0f;
        st.de  = 0.0f;
        st.low = 0.0f;
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
// Analog tone-match reset
//==============================================================
void FruityClipAudioProcessor::resetAnalogToneState (int numChannels)
{
    analogToneStates.clear();

    if (numChannels <= 0)
        return;

    analogToneStates.resize ((size_t) numChannels);
    for (auto& st : analogToneStates)
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

float FruityClipAudioProcessor::applySilkMaxColor (float x, int channel)
{
    // Silk Max is tuned against fastcartest.wav → reference_silk_100.wav.
    // The core clipper stays digital; this block adds the post-clip coloration
    // that approximates the full-red Silk hardware pass.
    if (sampleRate <= 0.0)
        return x;

    if (channel < 0 || channel >= (int) silkStates.size())
        return x;

    auto& st = silkStates[(size_t) channel];

    const float preGain     = juce::Decibels::decibelsToGain (kSilkPreEmphasisDb);
    const float deGain      = juce::Decibels::decibelsToGain (kSilkDeEmphasisDb);
    const float lowShelfG   = juce::Decibels::decibelsToGain (kSilkLowShelfDb);
    const float invSatNorm  = 1.0f / (std::tanh (kSilkMaxDrive + kSilkMaxAsym) - std::tanh (kSilkMaxAsym));

    // 1) HF pre-emphasis into the saturator (simple high split)
    st.pre = silkPreAlpha * st.pre + (1.0f - silkPreAlpha) * x;
    const float preLow  = st.pre;
    const float preHigh = x - preLow;
    const float pre     = preLow + preHigh * preGain;

    // 2) Soft asymmetric saturation for odd/even blend
    const float driven = pre * kSilkMaxDrive;
    const float sat    = (std::tanh (driven + kSilkMaxAsym) - std::tanh (kSilkMaxAsym)) * invSatNorm;

    // 3) HF de-emphasis (slightly heavier than pre boost)
    st.de = silkDeAlpha * st.de + (1.0f - silkDeAlpha) * sat;
    const float deLow  = st.de;
    const float deHigh = sat - deLow;
    const float de     = deLow + deHigh * deGain;

    // 4) Low-mid weight via single-pole shelf approximation
    st.low = silkLowAlpha * st.low + (1.0f - silkLowAlpha) * de;
    const float low      = st.low;
    const float high     = de - low;
    const float weighted = low * lowShelfG + high;

    // 5) Calibration trim to align loudness vs. analog capture
    return weighted * kSilkMaxGain;
}

float FruityClipAudioProcessor::applyAnalogToneMatch (float x, int channel)
{
    // Safety: bail out if we don't have a valid sample rate or state
    if (sampleRate <= 0.0)
        return x;

    if (channel < 0 || channel >= (int) analogToneStates.size())
        return x;

    auto& st = analogToneStates[(size_t) channel];

    // -----------------------------------------------------------------
    // 1) Split into "low" and "high" around ~1 kHz
    //
    // analogToneAlpha is configured in prepareToPlay() to give us
    // a one-pole lowpass at ~1 kHz. That becomes our "low" band and
    // (x - low) becomes the complementary "high" band.
    // -----------------------------------------------------------------
    st.low = analogToneAlpha * st.low + (1.0f - analogToneAlpha) * x;

    const float low  = st.low;
    const float high = x - low;

    // -----------------------------------------------------------------
    // 2) Read SILK (ottAmount) and shape it
    //
    // rawSilk comes from the LOVE/SILK knob (0..1). We reuse the same
    // shaped control curve as the other silk code so the ear feels
    // consistent: most of the "movement" is towards the top of the knob.
    // -----------------------------------------------------------------
    auto* ottParam = parameters.getRawParameterValue ("ottAmount");
    const float rawSilk = ottParam ? juce::jlimit (0.0f, 1.0f, ottParam->load()) : 0.0f;
    const float s       = std::pow (rawSilk, 0.8f); // shaped SILK control

    // -----------------------------------------------------------------
    // 3) 5060-style tone tilt from white-noise measurements
    //
    // The hardware captures show:
    //   • At SILK 0  : lows / low-mids slightly up, top end significantly down.
    //   • At SILK 100: still darker than Ableton on top, but less extreme
    //                  and with a touch more low/mid weight.
    //
    // We map SILK 0..1 onto two target tilt states:
    //
    //   SILK 0:
    //       low band  ≈ +0.3 dB
    //       high band ≈ -4.5 dB
    //
    //   SILK 1:
    //       low band  ≈ +0.5 dB
    //       high band ≈ -2.8 dB
    //
    // Then we interpolate in dB space based on the shaped SILK amount "s".
    // -----------------------------------------------------------------
    const float lowDbAt0  =  0.3f;  // dB at SILK 0
    const float highDbAt0 = -4.5f;  // dB at SILK 0

    const float lowDbAt1  =  0.5f;  // dB at SILK 100
    const float highDbAt1 = -2.8f;  // dB at SILK 100

    const float lowDb  = juce::jmap (s, 0.0f, 1.0f, lowDbAt0,  lowDbAt1);
    const float highDb = juce::jmap (s, 0.0f, 1.0f, highDbAt0, highDbAt1);

    const float lowGain  = juce::Decibels::decibelsToGain (lowDb);
    const float highGain = juce::Decibels::decibelsToGain (highDb);

    // -----------------------------------------------------------------
    // 4) Apply tilt and clamp
    // -----------------------------------------------------------------
    float y = lowGain * low + highGain * high;

    // Safety clamp – we should never normally hit this,
    // but it keeps the stage well-behaved in edge cases.
    return juce::jlimit (-4.0f, 4.0f, y);
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
    if ((int) silkStates.size() < numChannels)
        resetSilkState (numChannels);
    if ((int) analogToneStates.size() < numChannels)
        resetAnalogToneState (numChannels);

    const bool isOffline = isNonRealtime();

    auto* gainParam   = parameters.getRawParameterValue ("inputGain");
    auto* ottParam    = parameters.getRawParameterValue ("ottAmount");
    auto* satParam    = parameters.getRawParameterValue ("satAmount");
    auto* modeParam   = parameters.getRawParameterValue ("useLimiter");
    auto* clipModeParam = parameters.getRawParameterValue ("clipMode");

    const float inputGainDb   = gainParam   ? gainParam->load()   : 0.0f;
    const float ottAmountRaw  = ottParam    ? ottParam->load()    : 0.0f;
    const float satAmountRaw  = satParam    ? satParam->load()    : 0.0f;
    const bool  useLimiter    = modeParam   ? (modeParam->load() >= 0.5f) : false;
    const int   clipModeIndex = clipModeParam ? juce::jlimit (0, 1, (int) clipModeParam->load()) : 0;
    const ClipMode clipMode   = clipModeIndex == 0 ? ClipMode::Digital : ClipMode::Analog;

    const float ottAmount  = juce::jlimit (0.0f, 1.0f, ottAmountRaw);
    const float satAmount  = juce::jlimit (0.0f, 1.0f, satAmountRaw);

    // Global scalars for this block
    // inputGain comes from the finger (in dB).
    const float inputGain = juce::Decibels::decibelsToGain (inputGainDb);

    // Coarse alignment (kept as 1.0f for now)
    constexpr float fruityCal = 1.0f;

    // Fine alignment scalar to tune RMS/null vs Fruity.
    // Start at 1.0f. Later you can try values like 0.99998f, 1.00002f, etc.
    constexpr float fruityFineCal = 0.99997f;

    // This is the actual drive into OTT/SAT/clipper for default mode.
    const float inputDrive = inputGain * fruityCal * fruityFineCal;

    // LIVE oversample index from parameter (0..6)
    int liveOsIndex = 0;
    if (auto* osModeParam = parameters.getRawParameterValue ("oversampleMode"))
        liveOsIndex = juce::jlimit (0, 6, (int) osModeParam->load());

    // Start from LIVE value
    int osIndex = liveOsIndex;

    // Offline override: -1 = SAME (follow live), 0..6 = explicit offline choice
    if (isOffline)
    {
        const int offlineIdx = getStoredOfflineOversampleIndex(); // -1..6
        if (offlineIdx >= 0)
            osIndex = offlineIdx;
    }

    // Make sure final index is in range 0..6 for updateOversampling
    osIndex = juce::jlimit (0, 6, osIndex);

    const bool bypassNow = gainBypass.load();
    if (bypassNow)
    {
        // BYPASS mode: apply only input gain (for loudness-matched A/B).
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* samples = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                samples[i] *= inputDrive;
            }
        }

        // IMPORTANT: we DO NOT return here anymore.
        // We still want to run the K-weighted meter and LUFS logic below,
        // so the LUFS label continues to move while bypassed.
    }
    else
    {
        // Oversampling mode can be changed at runtime – keep Oversampling object in sync
        if (osIndex != currentOversampleIndex || (! oversampler))
        {
            updateOversampling (osIndex, numChannels);
        }

        if (oversampler && maxBlockSize < numSamples)
        {
            maxBlockSize = numSamples;
            oversampler->initProcessing ((size_t) maxBlockSize);
        }

        //==========================================================
        // PRE-CHAIN: GAIN + LOVE/SILK (always at base rate)
        //==========================================================

        if (clipMode == ClipMode::Analog)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* samples = buffer.getWritePointer (ch);

                for (int i = 0; i < numSamples; ++i)
                {
                    float y = samples[i] * inputDrive;
                    samples[i] = y;
                }
            }

            lastOttGain = 1.0f;
        }
        else if (ottAmount <= 0.0f)
        {
            // OTT fully bypassed: apply only INPUT DRIVE in place.
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* samples = buffer.getWritePointer (ch);

                for (int i = 0; i < numSamples; ++i)
                {
                    float y = samples[i] * inputDrive;
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
                    // 1) INPUT DRIVE
                    float y = x[i] * inputDrive;

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
                        const float maxUp = 1.7f;                    // up to +6 dB
                        dynGain += t * (maxUp - 1.0f) * ottAmount;
                    }
                    else
                    {
                        // Downward region – softened a lot so tails stay alive
                        float t = juce::jlimit (0.0f, 1.0f, lev - 1.0f);
                        const float minGain = 0.90f;                 // only ~ -1.4 dB max tame
                        dynGain -= t * (1.0f - minGain) * ottAmount;
                    }

                    // 5) Static tilt
                    const float staticBoost = 1.0f + 0.7f * ottAmount;

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

                const float maxTrimDb = -1.0f;      // was -0.8f
                staticTrimDb = maxTrimDb * shaped;
            }

            const float staticTrim = juce::Decibels::decibelsToGain (staticTrimDb);

            lastOttGain = staticTrim; // store for potential debug / GUI if needed

            buffer.makeCopyOf (preChain);
            buffer.applyGain (staticTrim);
        }

        //==========================================================
        // BASE-RATE SATURATION (always before oversampling)
        //==========================================================
        const bool limiterOn = useLimiter;
        const bool useSilkMax = (! limiterOn && clipMode == ClipMode::Analog);

        if (! limiterOn && satAmount > 0.0f)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* samples = buffer.getWritePointer (ch);
                auto&  sat     = satStates[(size_t) ch];

                for (int i = 0; i < numSamples; ++i)
                {
                    float samplePre = samples[i];

                    // --- STATIC INPUT TRIM ---
                    // At SAT = 0  -> 0 dB
                    // At SAT = 1  -> ~-0.5 dB
                    const float inputTrimDb = juce::jmap (satAmount, 0.0f, 1.0f, 0.0f, -0.5f);
                    const float inputTrim   = juce::Decibels::decibelsToGain (inputTrimDb);

                    samplePre *= inputTrim;

                    // --- BASS TILT ---
                    sat.low = satLowAlpha * sat.low + (1.0f - satLowAlpha) * samplePre;
                    const float low = sat.low;

                    const float tiltAmount = juce::jmap (satAmount, 0.0f, 1.0f, 0.0f, 0.85f);
                    const float tilted     = samplePre + tiltAmount * (low - samplePre);

                    // --- DRIVE ---
                    const float drive = 1.0f + 5.0f * std::pow (satAmount, 1.3f);

                    float driven = std::tanh (tilted * drive);

                    // --- STATIC NORMALISATION (UNITY) ---
                    const float norm = 1.0f / std::tanh (drive);
                    driven *= norm;

                    // --- DRY/WET ---
                    const float mix = std::pow (satAmount, 1.0f);
                    float sample    = samplePre + mix * (driven - samplePre);

                    samples[i] = sample;
                }
            }
        }

        //==========================================================
        // DISTORTION CHAIN (CLIP or LIMITER)
        //   - In oversampled mode, this runs at higher rate
        //   - No metering here; meters are computed later at base rate
        //==========================================================
        const bool useOversampling = (oversampler != nullptr && currentOversampleIndex > 0);

        if (useOversampling)
        {
            juce::dsp::AudioBlock<float> block (buffer);

            auto osBlock      = oversampler->processSamplesUp (block);
            const int osNumSamples  = (int) osBlock.getNumSamples();
            const int osNumChannels = (int) osBlock.getNumChannels(); // should match numChannels

            // Single-pass oversampled distortion: limiter OR pure hard clip
            for (int ch = 0; ch < osNumChannels; ++ch)
            {
                float* samples = osBlock.getChannelPointer (ch);

                for (int i = 0; i < osNumSamples; ++i)
                {
                    float sample = samples[i];

                    if (limiterOn)
                    {
                        sample = processLimiterSample (sample);
                    }
                    else
                    {
                        // Pure hard clip in oversampled domain (shared digital core)
                        if (sample >  1.0f) sample =  1.0f;
                        if (sample < -1.0f) sample = -1.0f;
                    }

                    samples[i] = sample;
                }
            }

            // Downsample once for the whole block.
            oversampler->processSamplesDown (block);
        }
        else
        {
            //======================================================
            // NO OVERSAMPLING – process at base rate only
            //======================================================
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* samples = buffer.getWritePointer (ch);

                for (int i = 0; i < numSamples; ++i)
                {
                    float sample = samples[i];

                    // SAT already applied previously if needed.
                    if (limiterOn)
                        sample = processLimiterSample (sample);
                    else
                    {
                        // Pure hard clip at base rate
                        if (sample >  1.0f) sample =  1.0f;
                        if (sample < -1.0f) sample = -1.0f;
                    }

                    samples[i] = sample;
                }
            }
        }

        //======================================================
        // SILK MAX COLORATION (base rate only, post-clip)
        //======================================================
        if (useSilkMax)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* s = buffer.getWritePointer (ch);

                for (int i = 0; i < numSamples; ++i)
                    s[i] = applySilkMaxColor (s[i], ch);
            }
        }

        // FINAL SAFETY CEILING AT BASE RATE (catches any residual intersample overs)
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

        {
            const int numChannels = buffer.getNumChannels();
            const int numSamples  = buffer.getNumSamples();

            // We quantize to 24-bit domain: ±2^23 discrete steps.
            constexpr float quantSteps = 8388608.0f;       // 2^23
            constexpr float ditherAmp  = 1.0f / quantSteps; // ~ -138 dBFS

            // Simple random generator (LCG) per block.
            static uint32_t ditherState = 0x12345678u;
            auto randFloat = [&]() noexcept
            {
                ditherState = ditherState * 1664525u + 1013904223u;
                return (ditherState & 0x00FFFFFFu) / 16777216.0f;
            };

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* samples = buffer.getWritePointer (ch);

                for (int i = 0; i < numSamples; ++i)
                {
                    float s = samples[i];

                    // inaudible Fruity-style TPDF dither
                    const float r1 = randFloat();
                    const float r2 = randFloat();
                    const float tpdf = (r1 - r2) * ditherAmp;
                    s += tpdf;

                    // 24-bit style quantization
                    const float q = std::round (s * quantSteps) / quantSteps;

                    samples[i] = q;
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
    const float burnForGui = bypassNow ? 0.0f : smoothedBurn;
    guiBurn.store (burnForGui);

    //==========================================================
    // Short-term LUFS (~1 s window for snappier meter)
    //   + signal gating envelope (for hiding the meter)
    //==========================================================
    if (sampleRate <= 0.0)
        sampleRate = 44100.0f;

    const float blockDurationSec = (float) numSamples / (float) sampleRate;

    // Exponential integrator approximating about a 1 s short-term window
    const float tauShortSec = 1.0f;
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

    const float tauSeconds   = 2.0f;
    const float blockSeconds = (float) numSamples / (float) sampleRate;

    const float alphaAvg = juce::jlimit (0.0f, 1.0f,
                                         blockSeconds / (tauSeconds + blockSeconds));

    if (hasSignalNow)
        lufsAverageLufs = (1.0f - alphaAvg) * lufsAverageLufs + alphaAvg * blockLufs;

    float avgForBurn = lufsAverageLufs;

    float norm = 0.0f;
    if (avgForBurn <= -12.0f)
        norm = 0.0f;
    else if (avgForBurn >= -1.0f)
        norm = 1.0f;
    else
        norm = (avgForBurn + 12.0f) / 11.0f;

    const int numSteps = 11;
    int stepIndex = (int) std::floor (norm * (float) numSteps + 1.0e-6f);
    stepIndex = juce::jlimit (0, numSteps, stepIndex);

    const float steppedBurn = (float) stepIndex / (float) numSteps;
    const float targetBurnLufs = steppedBurn;

    // Smooth gate envelope so LUFS label doesn't flicker
    const float prevEnv   = guiSignalEnv.load();
    const float gateAlpha = 0.25f;
    const float targetEnv = hasSignalNow ? 1.0f : 0.0f;
    const float newEnv    = (1.0f - gateAlpha) * prevEnv + gateAlpha * targetEnv;
    guiSignalEnv.store (newEnv);

    const float burnEnv = newEnv;
    const float lufsBurnForGui = bypassNow ? 0.0f : (targetBurnLufs * burnEnv);
    guiBurnLufs.store (lufsBurnForGui);

    //==========================================================
    // GUI LUFS readout – DIRECT calibrated short-term value
    //==========================================================
    // At this point:
    //   - 'lufs' is already the calibrated short-term LUFS
    //     (includes the +3 dB offset so we sit on top of MiniMeters).
    //   - gating behaviour is handled by guiSignalEnv / getGuiHasSignal().
    //
    // We no longer add extra “mastering ballistics” on the NUMBER itself.
    // That way, our LUFS readout tracks Youlean/MiniMeters closely,
    // while the LOOK/BURN animation can stay lazy / vibey.
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

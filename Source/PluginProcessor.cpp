#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <vector>

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
}

void FruityClipAudioProcessor::releaseResources() {}

bool FruityClipAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto main = layouts.getMainOutputChannelSet();
    return main == juce::AudioChannelSet::stereo()
        || main == juce::AudioChannelSet::mono();
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
    // SAT global "auto gain" – keep 0 dB baseline here.
    // We'll do a tiny trim per-sample in clip mode.
    //==========================================================
    const float satCompDb = 0.0f;

    // User gain (your left finger) – used directly in LIMIT mode
    const float inputGainLimiter = juce::Decibels::decibelsToGain (inputGainDb);

    // In CLIP/SAT mode, user gain + static SAT compensation (pre-sat)
    const float inputGainClip    = juce::Decibels::decibelsToGain (inputGainDb + satCompDb);

    const float silkBlend = silkAmount * silkAmount; // keep first half subtle
    const float g         = postGain;                // Fruity-null alignment

    float blockMax = 0.0f; // For GUI burn (post processing)

    if (useLimiter)
    {
        //======================================================
        // LIMIT MODE:
        //   GAIN -> SILK -> LIMITER -> POSTGAIN -> HARD CLIP
        //   SAT is ignored, and we do NOT apply any satCompDb.
        //======================================================
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* samples = buffer.getWritePointer (ch);

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

                samples[i] = y;
            }
        }
    }
    else
    {
        //======================================================
        // NEW CLIP / SAT MODE:
        // (Gain + tiny SAT trim) -> SILK -> SAT -> POSTGAIN -> HARD CLIP
        //======================================================
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* samples = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                // 0) Total input gain BEFORE SAT (user + static pre-gain)
                float y = samples[i] * inputGainClip;

                // ------------------------------------------------
                // 1) Tiny SAT auto-trim (max about -2 dB)
                // ------------------------------------------------
                const float satTrimDb = -2.0f * satAmount;   // 0 .. -2 dB
                const float satTrim   = juce::Decibels::decibelsToGain (satTrimDb);
                y *= satTrim;

                // ------------------------------------------------
                // 2) SILK (pre-clip transformer-ish colour)
                // ------------------------------------------------
                if (silkBlend > 0.0f)
                {
                    const float silkFull = silkCurveFull (y);
                    y = y + silkBlend * (silkFull - y);
                }

                // ------------------------------------------------
                // 3) SATURATION – rounded, bass-thick TikTok curve
                // ------------------------------------------------
                if (satAmount > 0.0f)
                {
                    // Threshold moves as SAT increases
                    // Later onset at low SAT, earlier at high SAT
                    const float thr = juce::jmap (satAmount, 0.92f, thresholdLinear);

                    // Rounded soft clip
                    float y0 = fruitySoftClipSample (y, thr);

                    // Bass lift – makes lows feel bigger without mud
                    const float bassLift = 1.0f + 0.20f * satAmount;
                    y0 *= bassLift;

                    // Blend so early SAT is subtle, end is aggressive
                    y = y + (satAmount * 0.88f) * (y0 - y);
                }

                // ------------------------------------------------
                // 4) Post-gain (Fruity-null alignment)
                // ------------------------------------------------
                y *= g;

                // ------------------------------------------------
                // 5) Hard ceiling at 0 dBFS
                // ------------------------------------------------
                if (y >  1.0f) y =  1.0f;
                if (y < -1.0f) y = -1.0f;

                const float absY = std::abs (y);
                if (absY > blockMax)
                    blockMax = absY;

                samples[i] = y;
            }
        }
    }

    //==========================================================
    // Update GUI burn meter (0..1), smoothed a bit
    // Faster in both attack & decay so the slam feels responsive
    //==========================================================
    const float targetBurn = juce::jlimit (0.0f, 1.0f, (blockMax - 0.5f) / 0.5f);
    const float previous   = guiBurn.load();
    const float smoothed   = 0.55f * previous + 0.45f * targetBurn;

    guiBurn.store (smoothed);
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

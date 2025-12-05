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
static float fruitySoftClipSample (float x, float threshold)
{
    const float ax = std::abs (x);
    const float sign = (x >= 0.0f ? 1.0f : -1.0f);

    if (ax <= threshold)
        return x;

    // Normalise to [0..1] above threshold
    const float t = (ax - threshold) / (1.0f - threshold); // 0..1

    const float shaped = threshold + (1.0f - (1.0f - t) * (1.0f - t)) * (1.0f - threshold);

    return sign * shaped;
}

//==============================================================
// Silk curve – gentle transformer-ish colour
//==============================================================
static float silkCurveFull (float x)
{
    const float x2 = x * x;
    const float x3 = x2 * x;
    const float x5 = x3 * x2;

    // Gentle odd harmonics
    constexpr float a3 = 0.15f;
    constexpr float a5 = 0.02f;

    float y = x + a3 * x3 + a5 * x5;

    // Soft limiter on the very top to keep it sane
    y = std::tanh (y * 1.3f);
    return y;
}

//==============================================================
// Constructor / Destructor
//==============================================================
FruityClipAudioProcessor::FruityClipAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
#endif
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
#endif
                      ),
#endif
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

FruityClipAudioProcessor::~FruityClipAudioProcessor() {}

//==============================================================
// Metadata
//==============================================================
const juce::String FruityClipAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool FruityClipAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool FruityClipAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool FruityClipAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double FruityClipAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

//==============================================================
// Programs (we don't really use them)
//==============================================================
int FruityClipAudioProcessor::getNumPrograms()
{
    return 1;
}

int FruityClipAudioProcessor::getCurrentProgram()
{
    return 0;
}

void FruityClipAudioProcessor::setCurrentProgram (int)
{
}

const juce::String FruityClipAudioProcessor::getProgramName (int)
{
    return {};
}

void FruityClipAudioProcessor::changeProgramName (int, const juce::String&)
{
}

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
// Prepare / Release
//==============================================================
void FruityClipAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

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
        limiterGain = desiredGain;
    }
    else
    {
        limiterGain = limiterGain + (1.0f - limiterReleaseCo) * (desiredGain - limiterGain);
    }

    return x * limiterGain;
}

//==============================================================
// ProcessBlock
//==============================================================
void FruityClipAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    if (numChannels == 0 || numSamples == 0)
        return;

    // Peak for GUI "burn"
    float blockMax = 0.0f;

    // Grab parameters
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

    // User gain (left finger) – **never touched by SAT**
    const float inputGain = juce::Decibels::decibelsToGain (inputGainDb);

    //==========================================================
    // STATIC SAT "UNITY" TRIM (post-SAT)
    // "for every bit of SAT up, push this much down"
    //==========================================================
    // At SAT = 1.0, pull down by about 4 dB (tweak to taste).
    constexpr float maxSatTrimDb = -2.0f;
    const float satCurve         = satAmount; // linear; change to satAmount*satAmount if you want
    const float satTrimDb        = maxSatTrimDb * satCurve;
    const float satTrimGain      = juce::Decibels::decibelsToGain (satTrimDb);

    const float silkBlend = silkAmount * silkAmount; // keep first half subtle
    const float g         = postGain;                // Fruity-null alignment

    float localBlockMax = 0.0f; // For GUI burn (post processing)

    if (useLimiter)
    {
        //======================================================
        // LIMITER MODE
        // - Ignores SAT (SATAmount still stored but not used)
        // - Uses 0-lookahead hard limiting with soft-ish behaviour
        //======================================================
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* samples = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                float y = samples[i] * inputGain;

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

                const float absY = std::abs (y);
                if (absY > localBlockMax)
                    localBlockMax = absY;

                samples[i] = y;
            }
        }
    }
    else
    {
        //======================================================
        // CLIPPER MODE
        //======================================================
        const float thresholdAtMaxSat = thresholdLinear;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* samples = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                float y = samples[i] * inputGain;

                // 1) SILK (pre-clip colour)
                if (silkBlend > 0.0f)
                {
                    const float silkFull = silkCurveFull (y);
                    y = y + silkBlend * (silkFull - y);
                }

                // 2) SATURATION (Fruity-ish soft clip)
                if (satAmount > 0.0f)
                {
                    // Threshold moves as SAT increases
                    const float currentThreshold = juce::jmap (satAmount, 1.0f, thresholdAtMaxSat);
                    y = fruitySoftClipSample (y, currentThreshold);
                }

                // 3) Fixed post-SAT trim so it doesn't just get stupid loud
                y *= satTrimGain;

                // 4) Post-gain (Fruity-null alignment)
                y *= g;

                // 5) Hard ceiling at 0 dBFS
                if (y >  1.0f) y =  1.0f;
                if (y < -1.0f) y = -1.0f;

                const float absY = std::abs (y);
                if (absY > localBlockMax)
                    localBlockMax = absY;

                samples[i] = y;
            }
        }
    }

    //==========================================================
    // Update GUI burn meter (0..1)
    //==========================================================
    blockMax = localBlockMax;

    const float current = guiBurn.load();
    const float target  = juce::jlimit (0.0f, 1.0f, blockMax);

    // Simple smoothing
    const float smoothing = 0.15f;
    float newValue = current + smoothing * (target - current);

    guiBurn.store (newValue);
}

//==============================================================
// Editor
//==============================================================
juce::AudioProcessorEditor* FruityClipAudioProcessor::createEditor()
{
    return new FruityClipAudioProcessorEditor (*this);
}

//==============================================================
// Entry point
//==============================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FruityClipAudioProcessor();
}

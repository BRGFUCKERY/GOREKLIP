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

    // Input gain in dB (-12 .. +12), default 0
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "inputGain", "Input Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f));

    // Saturation amount (0..1)
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "satAmount", "Saturation Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.0f));

    // Silk amount (0..1)
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "silkAmount", "Silk Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.0f));

    // Mode: false = clip, true = limiter
    params.push_back (std::make_unique<juce::AudioParameterBool>(
        "useLimiter", "Use Limiter", false));

    return { params.begin(), params.end() };
}

//==============================================================
// Subtle Neve-ish Silk curve (odd harmonics, gentle)
//==============================================================
float FruityClipAudioProcessor::silkCurveFull (float x)
{
    const float x2 = x * x;
    const float x3 = x2 * x;
    const float x5 = x3 * x2;

    // Slightly reduced from previous version to avoid overdoing lows
    constexpr float a3 = 0.10f;
    constexpr float a5 = 0.01f;

    float y = x + a3 * x3 + a5 * x5;

    // Normalise so |1| in -> |1| out
    constexpr float y1   = 1.0f + a3 * 1.0f + a5 * 1.0f;
    constexpr float norm = 1.0f / y1;

    y *= norm;

    return juce::jlimit (-1.2f, 1.2f, y);
}

//==============================================================
// Bass-friendly tanh saturator (rounded, no fuzz, no peak loss)
//==============================================================
float FruityClipAudioProcessor::bassBoostSaturate (float x, float satAmount)
{
    if (satAmount <= 0.0001f)
        return x;

    // Map 0..1 to 1x..8x drive
    const float driveMin = 1.0f;
    const float driveMax = 8.0f;
    const float drive    = juce::jmap (satAmount, driveMin, driveMax);

    const float d = drive * x;

    float y = std::tanh (d);

    // Normalise so that input 1.0 -> output 1.0 (avoid peak loss)
    const float norm = std::tanh (drive);
    if (norm > 0.0f)
        y /= norm;

    return y;
}

//==============================================================
// Limiter sample processing (0 lookahead, instant attack, smooth release)
//==============================================================
float FruityClipAudioProcessor::processLimiterSample (float x)
{
    const float limit = 0.999f;
    const float absX  = std::abs (x);

    float targetGain = 1.0f;

    if (absX > limit && absX > 1.0e-9f)
        targetGain = limit / absX;

    // Instant attack, smooth release
    if (targetGain < limiterGain)
    {
        // catch peaks immediately
        limiterGain = targetGain;
    }
    else
    {
        // release back towards 1.0
        limiterGain = limiterGain + (targetGain - limiterGain) * (1.0f - limiterReleaseCo);
    }

    return x * limiterGain;
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
    // Fruity-null-ish gain from your previous tuning
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
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    limiterGain = 1.0f;

    // Release time ~ 60 ms
    const float releaseTimeSeconds = 0.06f;
    limiterReleaseCo = std::exp (-1.0f / (releaseTimeSeconds * (float) sampleRate));
}

void FruityClipAudioProcessor::releaseResources() {}

bool FruityClipAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto main = layouts.getMainOutputChannelSet();
    return main == juce::AudioChannelSet::stereo()
        || main == juce::AudioChannelSet::mono();
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

    auto* gainParam   = parameters.getRawParameterValue ("inputGain");
    auto* satParam    = parameters.getRawParameterValue ("satAmount");
    auto* silkParam   = parameters.getRawParameterValue ("silkAmount");
    auto* modeParam   = parameters.getRawParameterValue ("useLimiter");

    const float inputGainDb  = gainParam  ? gainParam->load()  : 0.0f;
    const float satAmountRaw = satParam   ? satParam->load()   : 0.0f;
    const float silkAmountRaw= silkParam  ? silkParam->load()  : 0.0f;
    const bool  useLimiter   = modeParam  ? (modeParam->load() >= 0.5f) : false;

    const float inputGain  = juce::Decibels::decibelsToGain (inputGainDb);
    const float satAmount  = juce::jlimit (0.0f, 1.0f, satAmountRaw);
    const float silkAmount = juce::jlimit (0.0f, 1.0f, silkAmountRaw);

    const float silkBlend  = silkAmount * silkAmount; // subtle first half
    const float g          = postGain;

    if (useLimiter)
    {
        //======================================================
        // LIMIT MODE: GAIN -> SILK -> LIMITER
        //======================================================
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* samples = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                float y = samples[i] * inputGain;

                // 1) SILK (pre-dynamics, gentle)
                if (silkBlend > 0.0f)
                {
                    const float silkFull = silkCurveFull (y);
                    y = y + silkBlend * (silkFull - y);
                }

                // 2) Limiter
                y = processLimiterSample (y);

                // 3) Optional overall alignment + final hard safety
                y *= g;

                if (y >  1.0f) y =  1.0f;
                if (y < -1.0f) y = -1.0f;

                samples[i] = y;
            }
        }
    }
    else
    {
        //======================================================
        // CLIP MODE: GAIN -> SILK -> SAT -> HARD CLIP
        //======================================================
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* samples = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                float y = samples[i] * inputGain;

                // 1) SILK (pre-clip transformer-ish color)
                if (silkBlend > 0.0f)
                {
                    const float silkFull = silkCurveFull (y);
                    y = y + silkBlend * (silkFull - y);
                }

                // 2) SATURATION (rounded tanh, TikTok bass style)
                if (satAmount > 0.0f)
                {
                    y = bassBoostSaturate (y, satAmount);
                }

                // 3) Post-gain (Fruity-null alignment)
                y *= g;

                // 4) Hard ceiling at 0 dBFS
                if (y >  1.0f) y =  1.0f;
                if (y < -1.0f) y = -1.0f;

                samples[i] = y;
            }
        }
    }
}

//==============================================================
// Editor
//==============================================================
juce::AudioProcessorEditor* FruityClipAudioProcessor::createEditor()
{
    return new FruityClipAudioProcessorEditor (*this);
}

bool FruityClipAudioProcessor::hasEditor() const { return true; }

//==============================================================
// Metadata
//==============================================================
const juce::String FruityClipAudioProcessor::getName() const      { return "GOREKLIPER"; }
bool FruityClipAudioProcessor::acceptsMidi() const                { return false; }
bool FruityClipAudioProcessor::producesMidi() const               { return false; }
bool FruityClipAudioProcessor::isMidiEffect() const               { return false; }
double FruityClipAudioProcessor::getTailLengthSeconds() const     { return 0.0; }

//==============================================================
// Programs (we don't really use them)
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

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

    // MODE – 0 = clip, 1 = limit
    params.push_back (std::make_unique<juce::AudioParameterBool>(
        "mode", "Mode", false));

    return { params.begin(), params.end() };
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
    inputGainParam = parameters.getRawParameterValue ("inputGain");
    satParam       = parameters.getRawParameterValue ("satAmount");
    silkParam      = parameters.getRawParameterValue ("silkAmount");
    modeParam      = parameters.getRawParameterValue ("mode");
}

FruityClipAudioProcessor::~FruityClipAudioProcessor() {}

//==============================================================
// Basic AudioProcessor info
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

int FruityClipAudioProcessor::getNumPrograms()
{
    return 1;
}

int FruityClipAudioProcessor::getCurrentProgram()
{
    return 0;
}

void FruityClipAudioProcessor::setCurrentProgram (int) {}

const juce::String FruityClipAudioProcessor::getProgramName (int)
{
    return {};
}

void FruityClipAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================
// Prepare / Release
//==============================================================
void FruityClipAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    currentSampleRate = sampleRate;
    inverseSampleRate = (sampleRate > 0.0 ? 1.0 / sampleRate : 0.0);

    rmsAccumulatorL = 0.0f;
    rmsAccumulatorR = 0.0f;
    rmsSampleCount  = 0;

    guiBurnLevel.store (0.0f);
}

void FruityClipAudioProcessor::releaseResources() {}

//==============================================================
// Channel config
//==============================================================
bool FruityClipAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
   #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
   #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
   #endif
}

//==============================================================
// Soft-clip helper – "Fruity-ish"
//==============================================================
static float fruitySoftClipSample (float x, float thresholdLinear)
{
    const float t = thresholdLinear;

    if (std::fabs (x) <= t)
        return x;

    const float sign = (x >= 0.0f ? 1.0f : -1.0f);
    const float mag  = std::fabs (x);

    // Map [t..1] --> [t..1] with a soft curve
    const float over    = juce::jlimit (0.0f, 1.0f, (mag - t) / (1.0f - t));
    const float curved  = t + (1.0f - t) * (1.0f - std::exp (-4.0f * over));
    const float clipped = juce::jlimit (t, 1.0f, curved);

    return sign * clipped;
}

//==============================================================
// Silk curve helper
//==============================================================
static float silkCurveFull (float x)
{
    const float drive = 1.5f;
    const float y     = std::tanh (x * drive);
    return y;
}

//==============================================================
// Process block
//==============================================================
void FruityClipAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    if (numChannels == 0 || numSamples == 0)
        return;

    const float inputGainDb = inputGainParam->load();
    const float satAmount   = satParam->load();
    const float silkAmount  = silkParam->load();
    const bool  isLimitMode = (*modeParam > 0.5f);

    const float silkAmountRaw = silkAmount;
    const float silkAmountSmoothed = juce::jlimit (0.0f, 1.0f, silkAmountRaw);

    // User gain (left finger) – **never touched by SAT**
    const float inputGain = juce::Decibels::decibelsToGain (inputGainDb);

    //==========================================================
    // STATIC SAT "UNITY" TRIM (post-SAT)
    // "for every bit of SAT up, push this much down"
    //==========================================================
    // At SAT = 1.0, pull down by about 2 dB (tweak to taste).
    constexpr float maxSatTrimDb = -2.0f;

    // Keep it essentially linear so it feels natural as you turn SAT.
    const float satCurve    = satAmount;
    const float satTrimDb   = maxSatTrimDb * satCurve;
    const float satTrimGain = juce::Decibels::decibelsToGain (satTrimDb);

    const float silkBlend = silkAmount * silkAmount; // keep first half subtle

    const float thresholdLinear = 0.707f;

    // Post-gain scaling for Fruity-null behaviour
    const float g = 1.0f;

    float peakAbs = 0.0f;

    if (! isLimitMode)
    {
        //======================================================
        // CLIP MODE
        //======================================================
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* samples = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                float y = samples[i] * inputGain;

                // 1) SILK (pre-clip transformer-ish colour)
                if (silkBlend > 0.0f)
                {
                    const float silkFull = silkCurveFull (y);
                    y = y + silkBlend * (silkFull - y);
                }

                // 2) SATURATION (Fruity-ish soft clip)
                if (satAmount > 0.0f)
                {
                    const float currentThreshold = juce::jmap (satAmount, 1.0f, thresholdLinear);
                    y = fruitySoftClipSample (y, currentThreshold);
                }

                // 3) Fixed post-SAT trim so it doesn't just get stupid loud
                y *= satTrimGain;

                // 4) Post-gain (Fruity-null alignment)
                y *= g;

                // 5) Hard ceiling at 0 dBFS
                if (y >  1.0f) y =  1.0f;
                if (y < -1.0f) y = -1.0f;

                samples[i] = y;

                const float absVal = std::fabs (y);
                if (absVal > peakAbs)
                    peakAbs = absVal;

                const float absL = (ch == 0) ? absVal : 0.0f;
                const float absR = (ch == 1) ? absVal : 0.0f;

                rmsAccumulatorL += absL * absL;
                rmsAccumulatorR += absR * absR;
            }
        }
    }
    else
    {
        //======================================================
        // LIMIT MODE – brickwall, 0 lookahead, no SAT
        //======================================================
        const float limitThreshold = 1.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* samples = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                float y = samples[i] * inputGain;

                if (silkBlend > 0.0f)
                {
                    const float silkFull = silkCurveFull (y);
                    y = y + silkBlend * (silkFull - y);
                }

                if (y >  limitThreshold) y =  limitThreshold;
                if (y < -limitThreshold) y = -limitThreshold;

                y *= g;

                samples[i] = y;

                const float absVal = std::fabs (y);
                if (absVal > peakAbs)
                    peakAbs = absVal;

                const float absL = (ch == 0) ? absVal : 0.0f;
                const float absR = (ch == 1) ? absVal : 0.0f;

                rmsAccumulatorL += absL * absL;
                rmsAccumulatorR += absR * absR;
            }
        }
    }

    rmsSampleCount += numSamples;

    const float decayRatePerSecond = 6.0f;
    const float decay = std::exp (-decayRatePerSecond * (float) numSamples * (float) inverseSampleRate);

    const float peakForGui = peakAbs;
    float currentBurn      = guiBurnLevel.load();
    float targetBurn       = juce::jlimit (0.0f, 1.0f, peakForGui);

    const float smoothing = 0.15f;
    currentBurn = currentBurn + smoothing * (targetBurn - currentBurn);
    currentBurn *= decay;

    guiBurnLevel.store (currentBurn);
}

//==============================================================
// Editor
//==============================================================
bool FruityClipAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* FruityClipAudioProcessor::createEditor()
{
    return new FruityClipAudioProcessorEditor (*this);
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
// Entry point
//==============================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FruityClipAudioProcessor();
}

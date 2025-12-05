#include "PluginProcessor.h"
#include "PluginEditor.h"

// ==========================
// Parameter layout
// ==========================
juce::AudioProcessorValueTreeState::ParameterLayout
FruityClipAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "satAmount", "Saturation Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "silkAmount", "Silk Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    return { params.begin(), params.end() };
}

// ==========================
// Fruity Soft Clip Curve
// ==========================
static float fruitySoftClipSample (float x, float threshold)
{
    float sign = (x >= 0.0f ? 1.0f : -1.0f);
    float ax   = std::abs (x);

    if (ax <= threshold)
        return x;

    if (ax >= 1.0f)
        return sign * 1.0f;

    float t = (ax - threshold) / (1.0f - threshold); // 0..1

    float shaped = threshold + (1.0f - (1.0f - t) * (1.0f - t)) * (1.0f - threshold);

    return sign * shaped;
}

// ==========================
// Subtle Neve 5060-Style Silk Curve
// ==========================
static float silkCurveFull (float x)
{
    const float x2 = x * x;
    const float x3 = x2 * x;
    const float x5 = x3 * x2;

    // Gentle harmonics
    constexpr float a3 = 0.15f; 
    constexpr float a5 = 0.02f;

    float y = x + a3 * x3 + a5 * x5;

    // Normalize so 1.0 in = 1.0 out
    constexpr float y1   = 1.0f + a3 * 1.0f + a5 * 1.0f;
    constexpr float norm = 1.0f / y1;

    y *= norm;

    return juce::jlimit (-1.2f, 1.2f, y);
}

// ==========================
// Constructor
// ==========================
FruityClipAudioProcessor::FruityClipAudioProcessor()
    : parameters (*this, nullptr, "PARAMS", createParameterLayout())
{
    // Ultra fine-tuned Fruity null gain
    postGain        = 0.99999385f;

    // Soft clip threshold (Fruity behavior)
    thresholdLinear = juce::Decibels::decibelsToGain (-6.0f);
}

FruityClipAudioProcessor::~FruityClipAudioProcessor() {}

// ==========================
// Prepare / Release
// ==========================
void FruityClipAudioProcessor::prepareToPlay (double, int) {}
void FruityClipAudioProcessor::releaseResources() {}

// ==========================
// Buses layout
// ==========================
bool FruityClipAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

// ==========================
// CORE DSP
// ==========================
void FruityClipAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    const float g = postGain;

    auto* satParam  = parameters.getRawParameterValue ("satAmount");
    auto* silkParam = parameters.getRawParameterValue ("silkAmount");

    const float satAmount  = satParam  ? satParam->load()  : 0.0f;
    const float silkAmount = silkParam ? silkParam->load() : 0.0f;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* samples = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float y = samples[i];

            // =============================
            // 1) SILK (subtle transformer color)
            // =============================
            if (silkAmount > 0.0f)
            {
                const float silkFull = silkCurveFull (y);

                // square for subtle early movement
                const float amt = silkAmount * silkAmount;

                y = y + amt * (silkFull - y);
            }

            // =============================
            // 2) SATURATION (Fruity-style)
            // =============================
            if (satAmount > 0.0f)
            {
                const float clipped = fruitySoftClipSample (y, thresholdLinear);
                y = y + satAmount * (clipped - y);
            }

            // =============================
            // 3) Post-gain (Fruity null alignment)
            // =============================
            y *= g;

            // =============================
            // 4) Hard limit (0 dBFS)
            // =============================
            if (y >  1.0f) y =  1.0f;
            if (y < -1.0f) y = -1.0f;

            samples[i] = y;
        }
    }
}

// ==========================
// Editor
// ==========================
juce::AudioProcessorEditor* FruityClipAudioProcessor::createEditor()
{
    return new FruityClipAudioProcessorEditor (*this);
}

bool FruityClipAudioProcessor::hasEditor() const { return true; }

// ==========================
// State
// ==========================
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

// ==========================
// Entry point
// ==========================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FruityClipAudioProcessor();
}

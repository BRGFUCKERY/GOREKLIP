#include "PluginProcessor.h"
#include "PluginEditor.h"

// ==========================
// Parameter layout
// ==========================
juce::AudioProcessorValueTreeState::ParameterLayout
FruityClipAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Saturation amount: 0 = Fruity null, 1 = full curve
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "satAmount", "Saturation Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // Silk amount: 0 = no silk, 1 = full "5060 Red" curve (placeholder for now)
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "silkAmount", "Silk Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    return { params.begin(), params.end() };
}

// ==========================
// Soft clip curve (our "nice" curve)
// ==========================
static float fruitySoftClipSample (float x, float threshold)
{
    float sign = x >= 0.0f ? 1.0f : -1.0f;
    float ax   = std::abs (x);

    if (ax <= threshold)
        return x;

    if (ax >= 1.0f)
        return sign * 1.0f;

    float t = (ax - threshold) / (1.0f - threshold); // 0..1

    // smooth knee: f(0)=threshold, f(1)=1.0
    float shaped = threshold + (1.0f - (1.0f - t) * (1.0f - t)) * (1.0f - threshold);

    return sign * shaped;
}

// ==========================
// Silk Red "full" curve (placeholder for 5060 Red)
// ==========================
// This is the shape at Silk = 100%. The knob will blend between dry and this.
// Later we can tweak this function to match the actual 5060 manual curve.
static float silkCurveFull (float x)
{
    // Gentle asymmetric saturator:
    // more even harmonics (2nd) + some 3rd.
    const float a = 0.4f;  // even harmonic strength
    const float b = 0.2f;  // odd harmonic strength

    float y = x + a * (x * x) + b * (x * x * x);

    // Keep under control pre-clip
    return juce::jlimit (-1.0f, 1.0f, y);
}

// ==========================
// Constructor
// ==========================
FruityClipAudioProcessor::FruityClipAudioProcessor()
    : parameters (*this, nullptr, "PARAMS", createParameterLayout())
{
    // This is the factor that matched Fruity in your noise + song tests
    postGain        = 0.99997096f; // ~ -0.00026 dB

    // Threshold for the saturation curve (~ -6 dB)
    thresholdLinear = juce::Decibels::decibelsToGain (-6.0f);
}

// ==========================
// PROCESS BLOCK
// ==========================
void FruityClipAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    const float g = postGain;

    // Params
    auto* satParam  = parameters.getRawParameterValue ("satAmount");
    auto* silkParam = parameters.getRawParameterValue ("silkAmount");

    float satAmount  = satParam  ? satParam->load()  : 0.0f; // 0..1
    float silkAmount = silkParam ? silkParam->load() : 0.0f; // 0..1

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* samples = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = samples[i];
            float y = x;

            // 1) PRE-CLIP SILK: dry / wet between original and full Silk curve
            if (silkAmount > 0.0f)
            {
                float drySilk  = y;
                float silkFull = silkCurveFull (y);
                y = juce::jmap (silkAmount, drySilk, silkFull);
            }

            // 2) SATURATION CURVE: blend between linear and clipped
            if (satAmount > 0.0f)
            {
                float drySat   = y;
                float clipped  = fruitySoftClipSample (y, thresholdLinear);
                y = juce::jmap (satAmount, drySat, clipped);
            }

            // 3) FRUITY POST GAIN (null base)
            y *= g;

            // 4) FINAL HARD CEILING at ±1.0 (0 dBFS)
            if (y >  1.0f) y =  1.0f;
            if (y < -1.0f) y = -1.0f;

            samples[i] = y;
        }
    }
}

// ==========================
// EDITOR
// ==========================
juce::AudioProcessorEditor* FruityClipAudioProcessor::createEditor()
{
    // For now: generic UI with Saturation Amount + Silk Amount sliders
    return new juce::GenericAudioProcessorEditor (*this);
}

// ==========================
// State save / load
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
// JUCE entry point
// ==========================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FruityClipAudioProcessor();
}

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

    // Silk amount: 0 = no silk, 1 = full "5060 Red" curve
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "silkAmount", "Silk Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    return { params.begin(), params.end() };
}

// ==========================
// Soft clip curve (your "nice" curve)
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
// Silk Red "full" curve (5060-style saturation)
// ==========================
// This is the shape at Silk = 100%. The knob will MORPH into this curve,
// not dry/wet blend audio.
static float silkCurveFull (float x)
{
    // Odd harmonics only (transformer-ish): x + a3 x^3 + a5 x^5
    const float x2 = x * x;
    const float x3 = x2 * x;
    const float x5 = x3 * x2;

    constexpr float a3 = 0.8f;
    constexpr float a5 = 0.25f;

    float y = x + a3 * x3 + a5 * x5;

    // Normalise so that |x| = 1 -> |y| = 1
    constexpr float y1   = 1.0f + a3 * 1.0f + a5 * 1.0f;
    constexpr float norm = 1.0f / y1;

    y *= norm;

    // Keep things reasonable pre-clip
    return juce::jlimit (-1.5f, 1.5f, y);
}

// ==========================
// Constructor
// ==========================
FruityClipAudioProcessor::FruityClipAudioProcessor()
    : parameters (*this, nullptr, "PARAMS", createParameterLayout())
{
    // This is the factor that matched Fruity in your noise + song tests
    postGain        = 0.99997096f; // ~ -0.00026 dB

    // Threshold for the saturation curve (~ -6 dB at satAmount = 1)
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

    const float satAmountRaw  = satParam  ? satParam->load()  : 0.0f; // 0..1
    const float silkAmount    = silkParam ? silkParam->load() : 0.0f; // 0..1

    // Clamp just in case
    const float satAmount = juce::jlimit (0.0f, 1.0f, satAmountRaw);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* samples = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = samples[i];
            float y = x;

            // ============================================================
            // 1) PRE-CLIP SILK: function morph, NOT dry/wet blend
            //    silkAmount = 0 -> y
            //    silkAmount = 1 -> silkCurveFull(y)
            // ============================================================
            if (silkAmount > 0.0f)
            {
                const float silkFull = silkCurveFull (y);
                y = y + silkAmount * (silkFull - y);
            }

            // ============================================================
            // 2) SATURATION: curve intensity via threshold, NOT dry/wet
            //
            //    satAmount = 0 -> threshold = 1.0 (no soft-clip until 0 dBFS)
            //    satAmount = 1 -> threshold = thresholdLinear (~ -6 dB)
            //    So the knee moves earlier as you turn the knob,
            //    but the curve itself is the same fruitySoftClipSample shape.
            // ============================================================
            if (satAmount > 0.0f)
            {
                // Map satAmount 0..1 to threshold 1.0..thresholdLinear
                const float currentThreshold = juce::jmap (satAmount, 1.0f, thresholdLinear);
                y = fruitySoftClipSample (y, currentThreshold);
            }

            // ============================================================
            // 3) FRUITY POST GAIN (null base)
            // ============================================================
            y *= g;

            // ============================================================
            // 4) FINAL HARD CEILING at ±1.0 (0 dBFS)
            // ============================================================
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

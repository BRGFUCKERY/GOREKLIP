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
// Silk Red "full" curve (subtle, 5060-style)
// ==========================
// 100% Silk curve. Mostly gentle 3rd harmonic, tiny 5th, normalised so
// input 1.0 -> output 1.0. Designed to be transformer-ish, not a fuzzbox.
static float silkCurveFull (float x)
{
    const float x2 = x * x;
    const float x3 = x2 * x;
    const float x5 = x3 * x2;

    // Subtle Neve-ish color: gentle 3rd, tiny 5th
    constexpr float a3 = 0.15f;  // main 3rd harmonic
    constexpr float a5 = 0.02f;  // tiny 5th

    float y = x + a3 * x3 + a5 * x5;

    // Normalise so |x| = 1 -> |y| = 1
    constexpr float y1   = 1.0f + a3 * 1.0f + a5 * 1.0f;
    constexpr float norm = 1.0f / y1;

    y *= norm;

    // Tiny safety clamp pre-clip
    return juce::jlimit (-1.2f, 1.2f, y);
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
    const float silkAmountRaw = silkParam ? silkParam->load() : 0.0f; // 0..1

    const float satAmount  = juce::jlimit (0.0f, 1.0f, satAmountRaw);
    const float silkAmount = juce::jlimit (0.0f, 1.0f, silkAmountRaw);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* samples = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = samples[i];
            float y = x;

            // ============================================================
            // 1) PRE-CLIP SILK: subtle function morph, NOT dry/wet blend
            //
            //    silkAmount = 0   -> y
            //    silkAmount = 1   -> silkCurveFull(y)
            //    First half of the knob is VERY subtle (amount^2).
            // ============================================================
            if (silkAmount > 0.0f)
            {
                const float silkFull = silkCurveFull (y);

                // square the amount so 0–50% is very gentle
                const float amt = silkAmount * silkAmount; // 0..1

                y = y + amt * (silkFull - y);
            }

            // ============================================================
            // 2) SATURATION: curve intensity via threshold, NOT dry/wet
            //
            //    satAmount = 0 -> threshold = 1.0 (no soft-clip until 0 dBFS)
            //    satAmount = 1 -> threshold = thresholdLinear (~ -6 dB)
            //    Same curve shape, knee moves earlier as you turn the knob.
            // ============================================================
            if (satAmount > 0.0f)
            {
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
    // You can replace this with your custom editor when ready
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

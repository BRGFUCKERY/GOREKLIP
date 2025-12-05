#include "PluginProcessor.h"
#include "PluginEditor.h"

// ==========================
// Soft clip function (Fruity-style-ish)
// ==========================
static float fruitySoftClipSample (float x, float threshold)
{
    float sign = x >= 0.0f ? 1.0f : -1.0f;
    float ax   = std::abs (x);

    // Completely linear below threshold
    if (ax <= threshold)
        return x;

    // Above 0 dBFS: hard cap
    if (ax >= 1.0f)
        return sign * 1.0f;

    // Soft knee between threshold and 0 dBFS
    float t = (ax - threshold) / (1.0f - threshold); // 0..1

    // Smooth curve: starts soft, bends into 1.0
    float shaped = threshold + (1.0f - (1.0f - t) * (1.0f - t)) * (1.0f - threshold);

    return sign * shaped;
}

// ==========================
// Constructor — default behavior
// ==========================
FruityClipAudioProcessor::FruityClipAudioProcessor()
{
    // Keep a fairly standard soft-clipping threshold
    constexpr float threshDb = -6.0f;

    thresholdLinear = juce::Decibels::decibelsToGain (threshDb); // ~0.5
    postGain        = 1.0f;  // no extra loudness for now
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

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* samples = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = samples[i];

            // Soft clip
            float y = fruitySoftClipSample (x, thresholdLinear);

            // (Optional) apply postGain, currently 1.0 so no loudness change
            y *= postGain;

            // Make absolutely sure we NEVER overshoot
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
    // For now, simple generic UI; we’ll put your pretty background back later
    return new juce::GenericAudioProcessorEditor (*this);
}

// ==========================
// JUCE entry point
// ==========================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FruityClipAudioProcessor();
}

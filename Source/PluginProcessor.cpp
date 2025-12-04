#include "PluginProcessor.h"
#include "PluginEditor.h"

// ==========================
// Soft clip function (Fruity-style)
// ==========================
static float fruitySoftClipSample (float x, float threshold)
{
    float sign = x >= 0.0f ? 1.0f : -1.0f;
    float ax   = std::abs(x);

    if (ax <= threshold)
        return x;

    if (ax >= 1.0f)
        return sign * 1.0f;

    float t = (ax - threshold) / (1.0f - threshold); // 0..1

    float shaped = threshold + (1.0f - (1.0f - t) * (1.0f - t)) * (1.0f - threshold);

    return sign * shaped;
}

// ==========================
// Constructor — Default Fruity Soft Clipper Behavior
// ==========================
FruityClipAudioProcessor::FruityClipAudioProcessor()
{
    constexpr float threshDb = -6.0f; // screenshot guess
    constexpr float postDb   =  3.0f; // screenshot guess

    thresholdLinear = juce::Decibels::decibelsToGain (threshDb);
    postGain        = juce::Decibels::decibelsToGain (postDb);
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
        float* samples = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = samples[i];

            float clipped = fruitySoftClipSample(x, thresholdLinear);

            clipped *= postGain;

            samples[i] = clipped;
        }
    }
}

// ==========================
// Editor
// ==========================
juce::AudioProcessorEditor* FruityClipAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

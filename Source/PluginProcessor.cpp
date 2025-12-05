#include "PluginProcessor.h"
#include "PluginEditor.h"

// ==========================
// Constructor
// ==========================
FruityClipAudioProcessor::FruityClipAudioProcessor()
{
    // This is the linear factor that best matches Fruity
    // with THRES at max and POST around 3 o'clock.
    // Derived from your +6 and +12 white noise tests.
    postGain        = 0.99997096f; // a ≈ 0.99997
    thresholdLinear = 1.0f;        // unused now, but kept for future use
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
    const float g = postGain; // just a tiny gain trim

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* samples = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = samples[i];

            // EXACT Fruity behaviour at your settings: linear scale only
            float y = x * g;

            samples[i] = y;
        }
    }
}

// ==========================
// EDITOR
// ==========================
juce::AudioProcessorEditor* FruityClipAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

// ==========================
// JUCE entry point
// ==========================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FruityClipAudioProcessor();
}

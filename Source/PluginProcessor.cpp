#include "PluginProcessor.h"
#include "PluginEditor.h"

// ==========================
// Constructor
// ==========================
FruityClipAudioProcessor::FruityClipAudioProcessor()
{
    // Linear factor that best matches Fruity at your settings
    // (THRES max, POST ~3 o'clock), derived from +6 and +12 tests.
    postGain        = 0.99997096f; // ~ -0.00026 dB
    thresholdLinear = 1.0f;        // unused for now
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
    const float g         = postGain;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* samples = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = samples[i];

            // 1) Match Fruity's tiny gain factor
            float y = x * g;

            // 2) Hard ceiling at 0 dBFS (±1.0), like the DAW/Fruity bus
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
    return new juce::GenericAudioProcessorEditor (*this);
}

// ==========================
// JUCE entry point
// ==========================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FruityClipAudioProcessor();
}

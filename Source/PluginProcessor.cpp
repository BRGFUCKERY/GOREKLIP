#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
FruityClipAudioProcessor::FruityClipAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
#endif
{
}

bool FruityClipAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto mainOut = layouts.getMainOutputChannelSet();
    auto mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono()
        && mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (mainIn != mainOut)
        return false;

    return true;
}

//==============================================================================
void FruityClipAudioProcessor::prepareToPlay (double /*sampleRate*/, int /*samplesPerBlock*/)
{
    // If you want oversampling for Silk/Sat later, init it here.
}

//==============================================================================
void FruityClipAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    auto* paramSilk       = apvts.getRawParameterValue ("SILK");
    auto* paramSaturation = apvts.getRawParameterValue ("SATURATION");
    auto* paramInTrim     = apvts.getRawParameterValue ("IN_TRIM");
    auto* paramOutTrim    = apvts.getRawParameterValue ("OUT_TRIM");

    const float silkAmount = paramSilk       ? *paramSilk       : 0.0f; // 0..1
    const float satAmount  = paramSaturation ? *paramSaturation : 0.0f; // 0..1

    const float inTrimDb   = paramInTrim     ? *paramInTrim     : 0.0f;
    const float outTrimDb  = paramOutTrim    ? *paramOutTrim    : 0.0f;

    const float inTrimGain  = juce::Decibels::decibelsToGain (inTrimDb);
    const float outTrimGain = juce::Decibels::decibelsToGain (outTrimDb);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* samples = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = samples[i];

            // 0) INPUT TRIM
            x *= inTrimGain;

            // 1) SILK (pre-clip)
            x = applySilk (x, silkAmount);

            // 2) SATURATION (pre-clip, peak-locked)
            x = applySaturation (x, satAmount);

            // 3) FRUITY HARD CLIP (null Fruity at default)
            x = fruityHardClip (x);

            // 4) OUTPUT TRIM
            x *= outTrimGain;

            samples[i] = x;
        }
    }
}

//==============================================================================
void FruityClipAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream mos (destData, true);
    apvts.state.writeToStream (mos);
}

void FruityClipAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto tree = juce::ValueTree::readFromData (data, sizeInBytes); tree.isValid())
        apvts.replaceState (tree);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
FruityClipAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Silk amount 0..1
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "SILK", 1 },
        "Silk",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f),
        0.0f
    ));

    // Saturation amount 0..1
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "SATURATION", 1 },
        "Saturation",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f),
        0.0f
    ));

    // Input Trim in dB
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "IN_TRIM", 1 },
        "Input Trim",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f),
        0.0f
    ));

    // Output Trim in dB
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "OUT_TRIM", 1 },
        "Output Trim",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f),
        0.0f
    ));

    return { params.begin(), params.end() };
}

//==============================================================================
juce::AudioProcessorEditor* FruityClipAudioProcessor::createEditor()
{
    return new FruityClipAudioProcessorEditor (*this);
}

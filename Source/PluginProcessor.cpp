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

    // SILK – 0..1
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "silkAmount", "Silk Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // OTT – 0..1 (150 Hz+ only, parallel, unity gain)
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "ottAmount", "OTT Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // SAT – 0..1
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "satAmount", "Saturation Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // MODE – 0 = clipper, 1 = limiter
    params.push_back (std::make_unique<juce::AudioParameterBool>(
        "useLimiter", "Use Limiter", false));

    // OVERSAMPLE MODE – 0:x1, 1:x2, 2:x4, 3:x8, 4:x16
    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        "oversampleMode", "Oversample Mode",
        juce::StringArray

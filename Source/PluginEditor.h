#pragma once

#include "JuceHeader.h"
#include "PluginProcessor.h"

//==============================================================================

class FruityClipAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    FruityClipAudioProcessorEditor (FruityClipAudioProcessor&);
    ~FruityClipAudioProcessorEditor() override;

    void paint   (juce::Graphics&) override;
    void resized() override;

private:
    FruityClipAudioProcessor& processorRef;

    // --- Controls ---
    juce::Slider silkSlider;
    juce::Slider satSlider;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> silkAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> satAttachment;

    // --- Images ---
    juce::Image backgroundImage;   // from bg.png
    juce::Image fingerImage;       // from finger.png
    juce::Image logoImage;         // from gorekliper_logo.png

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruityClipAudioProcessorEditor)
};

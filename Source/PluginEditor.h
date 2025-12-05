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
    juce::Image backgroundImage;
    juce::Image logoImage;
    juce::Image silkIconImage;
    juce::Image satIconImage;

    // Cached bounds for SILK / SAT icons
    juce::Rectangle<int> silkIconBounds;
    juce::Rectangle<int> satIconBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruityClipAudioProcessorEditor)
};

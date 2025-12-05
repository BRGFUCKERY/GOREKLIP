#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================

class GOREKLIPERAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    GOREKLIPERAudioProcessorEditor (GOREKLIPERAudioProcessor&);
    ~GOREKLIPERAudioProcessorEditor() override;

    void paint   (juce::Graphics&) override;
    void resized() override;

private:
    GOREKLIPERAudioProcessor& processorRef;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GOREKLIPERAudioProcessorEditor)
};

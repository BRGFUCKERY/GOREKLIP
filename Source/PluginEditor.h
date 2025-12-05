#pragma once

#include "JuceHeader.h"
#include "PluginProcessor.h"

//==============================================================
//  Custom LookAndFeel for the finger knob
//==============================================================
class MiddleFingerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MiddleFingerLookAndFeel() = default;

    void setKnobImage (const juce::Image& img) { knobImage = img; }

    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider& slider) override;

private:
    juce::Image knobImage;
};

//==============================================================
//  Main Editor
//==============================================================
class FruityClipAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    FruityClipAudioProcessorEditor (FruityClipAudioProcessor&);
    ~FruityClipAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    FruityClipAudioProcessor& processor;

    // Background & logo
    juce::Image bgImage;
    juce::Image logoImage;
    const float bgScale = 0.35f;

    // LookAndFeel + knobs
    MiddleFingerLookAndFeel fingerLnf;

    juce::Slider silkSlider; 
    juce::Slider satSlider;

    juce::Label  silkLabel;
    juce::Label  satLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> satAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> silkAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruityClipAudioProcessorEditor)
};

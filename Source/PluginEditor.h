#pragma once

#include "JuceHeader.h"
#include "PluginProcessor.h"

//==============================================================
//  Custom LookAndFeel for the finger knobs
//==============================================================
class MiddleFingerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MiddleFingerLookAndFeel() = default;

    void setKnobImage (const juce::Image& img)
    {
        knobImage = img;
    }

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
//  Main editor
//==============================================================
class FruityClipAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit FruityClipAudioProcessorEditor (FruityClipAudioProcessor&);
    ~FruityClipAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    FruityClipAudioProcessor& processor;

    // Background and slam images
    juce::Image backgroundImage;
    juce::Image slamImage;

    // Logo: original + white version
    juce::Image logoImage;
    juce::Image logoWhiteImage;

    // Middle-finger knobs
    MiddleFingerLookAndFeel middleFingerLnf;

    juce::Slider gainSlider;
    juce::Slider silkSlider;
    juce::Slider ottSlider;
    juce::Slider satSlider;
    juce::Slider modeSlider;

    juce::Label gainLabel;
    juce::Label silkLabel;
    juce::Label ottLabel;
    juce::Label satLabel;
    juce::Label modeLabel;

    // LUFS label
    juce::Label lufsLabel;

    // Oversample dropdown
    juce::ComboBox oversampleBox;

    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   silkAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   ottAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   satAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversampleAttachment;

    //==========================================================
    // LOOK / VISUAL MODES
    //==========================================================
    enum class LookMode
    {
        LufsMeter = 0,   // background burn from LUFS
        FuckedMeter,     // background burn from peak "slam" (original)
        Static,          // static clean background
        StaticCooked     // static fully burnt background
    };

    LookMode        lookMode      = LookMode::LufsMeter; // default
    juce::TextButton lookMenuButton;                     // top-left menu button

    // GUI burn value (cached from processor)
    float lastBurn = 0.0f;

    // Timer for GUI updates
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruityClipAudioProcessorEditor)
};


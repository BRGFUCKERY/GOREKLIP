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

    // Let the LNF know which sliders are which
    void setControlledSliders (juce::Slider* gain,
                               juce::Slider* mode,
                               juce::Slider* sat)
    {
        gainSlider = gain;
        modeSlider = mode;
        satSlider  = sat;
    }

    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider& slider) override;

private:
    juce::Image knobImage;

    // Pointers to specific sliders (not owned)
    juce::Slider* gainSlider = nullptr; // left finger (GAIN)
    juce::Slider* modeSlider = nullptr; // right finger (CLIPPER/LIMITER)
    juce::Slider* satSlider  = nullptr; // SAT knob
};

//==============================================================
//  Main Editor
//==============================================================
class FruityClipAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                        private juce::Timer
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
    juce::Image slamImage;       // "slammed" background
    juce::Image logoImage;
    juce::Image logoWhiteImage;  // precomputed white version of logo (same alpha)
    const float bgScale = 0.35f; // scale for bg.png

    // LookAndFeel + knobs
    MiddleFingerLookAndFeel fingerLnf;

    // 5 knobs: GAIN, SILK, OTT, SAT, MODE
    juce::Slider gainSlider;
    juce::Slider silkSlider;
    juce::Slider ottSlider;
    juce::Slider satSlider;
    juce::Slider modeSlider;

    juce::Label  gainLabel;
    juce::Label  silkLabel;
    juce::Label  ottLabel;
    juce::Label  satLabel;
    juce::Label  modeLabel;

    // LUFS text above CLIPPER/LIMITER finger
    juce::Label  lufsLabel;

    // Oversample mode (x1/x2/x4/x8/x16) – tiny top-right dropdown
    juce::ComboBox oversampleBox;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   silkAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   ottAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   satAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversampleAttachment;

    // GUI burn value (cached from processor)
    float lastBurn = 0.0f;

    // Timer for GUI updates
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruityClipAudioProcessorEditor)
};

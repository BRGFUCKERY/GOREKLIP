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

    void setGainAndModeSliders (juce::Slider* gain, juce::Slider* mode)
    {
        gainSlider = gain;
        modeSlider = mode;
    }

    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider& slider) override;

private:
    juce::Image  knobImage;
    juce::Slider* gainSlider = nullptr;
    juce::Slider* modeSlider = nullptr;
};

//==============================================================
//  Editor
//==============================================================
class FruityClipAudioProcessorEditor
    : public juce::AudioProcessorEditor,
      private juce::Timer
{
public:
    FruityClipAudioProcessorEditor (FruityClipAudioProcessor&);
    ~FruityClipAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class LookMode
    {
        LufsMeter,
        FuckedMeter,
        StaticFlat,
        StaticCooked
    };

    FruityClipAudioProcessor& processor;

    // Background & logo
    juce::Image bgImage;
    juce::Image slamImage;       // "slammed" background
    juce::Image logoImage;
    juce::Image logoWhiteImage;  // precomputed white version of logo (same alpha)
    const float bgScale = 0.35f; // scale for bg.png so plugin stays wide

    // LookAndFeel + knobs
    MiddleFingerLookAndFeel fingerLnf;

    // 4 knobs: GAIN, OTT, SAT, MODE (no more SILK)
    juce::Slider gainSlider;
    juce::Slider ottSlider;
    juce::Slider satSlider;
    juce::Slider modeSlider;

    juce::Label  gainLabel;
    juce::Label  ottLabel;
    juce::Label  satLabel;
    juce::Label  modeLabel;

    // LUFS text above CLIPPER/LIMITER finger
    juce::Label  lufsLabel;

    // Oversample mode (x1/x2/x4/x8/x16) – tiny top-right dropdown
    juce::ComboBox oversampleBox;

    // LOOK menu (top-left): LUFS METER / FUCKED / STATIC / STATIC COOKED / ABOUT
    juce::TextButton lookMenuButton;

    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   ottAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   satAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversampleAttachment;

    // GUI state
    float    lastBurn = 0.0f;             // 0..1 for background burn
    LookMode lookMode = LookMode::LufsMeter;

    // Helpers
    void timerCallback() override;
    static float mapLufsToBurn (float lufs);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruityClipAudioProcessorEditor)
};

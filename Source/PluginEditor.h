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

class FineControlSlider : public juce::Slider
{
public:
    FineControlSlider() = default;

    void setBaseSensitivity (int newSensitivity)
    {
        baseSensitivity = juce::jmax (1, newSensitivity);
        updateSensitivity();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        isFineMode = e.mods.isShiftDown();
        updateSensitivity();
        juce::Slider::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const bool fineNow = e.mods.isShiftDown();
        if (fineNow != isFineMode)
        {
            isFineMode = fineNow;
            updateSensitivity();
        }

        juce::Slider::mouseDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        isFineMode = false;
        updateSensitivity();
        juce::Slider::mouseUp (e);
    }

private:
    void updateSensitivity()
    {
        const int sensitivity = isFineMode ? juce::roundToInt ((float) baseSensitivity * 0.25f)
                                            : baseSensitivity;
        juce::Slider::setMouseDragSensitivity (juce::jmax (1, sensitivity));
    }

    int  baseSensitivity = 250;
    bool isFineMode      = false;
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

    // 4 knobs: GAIN, OTT, SAT, MODE
    FineControlSlider gainSlider;
    FineControlSlider ottSlider;
    FineControlSlider satSlider;
    FineControlSlider modeSlider;

    juce::Label  gainLabel;
    juce::Label  ottLabel;
    juce::Label  satLabel;
    juce::Label  modeLabel;

    // LUFS text above CLIPPER/LIMITER finger
    juce::Label  lufsLabel;

    // Oversample mode (x1/x2/x4/x8/x16) – tiny top-right dropdown
    juce::ComboBox oversampleBox;
    juce::ComboBox lookBox;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   ottAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   satAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversampleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lookAttachment;

    // GUI burn value (cached from processor)
    float lastBurn = 0.0f;

    // Local GUI state for gain-bypass toggle
    bool isGainBypass = false;

    void mouseUp (const juce::MouseEvent& e) override;

    // Timer for GUI updates
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruityClipAudioProcessorEditor)
};

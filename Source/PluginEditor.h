#pragma once

#include "JuceHeader.h"
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"

//==============================================================
//  Timer helper for finger animation
//==============================================================
class AnimationTimer : public juce::Timer
{
public:
    std::function<void()> onTimer;

    void timerCallback() override
    {
        if (onTimer)
            onTimer();
    }
};

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

    std::function<void()> onClick;

    void setDragSensitivities (int normal, int fine)
    {
        normalSensitivity = (float) normal;
        fineSensitivity   = (float) fine;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        juce::Slider::mouseDown (e);
        lastDragPos = e.position;
        wasDragged = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto delta   = e.position - lastDragPos;
        const float motion = delta.x - delta.y;

        wasDragged = true;

        lastDragPos = e.position;

        const float sensitivity = e.mods.isShiftDown() ? fineSensitivity : normalSensitivity;
        const auto  range       = getRange();

        if (sensitivity > 0.0f)
        {
            const double deltaValue = (motion / sensitivity) * range.getLength();
            const double newValue   = juce::jlimit (range.getStart(), range.getEnd(), getValue() + deltaValue);

            setValue (newValue, juce::sendNotificationSync);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::Slider::mouseUp (e);

        if (! wasDragged && onClick)
            onClick();
    }

private:
    juce::Point<float> lastDragPos;
    float normalSensitivity = 250.0f;
    float fineSensitivity   = 1000.0f;
    bool wasDragged = false;
};

class DownwardComboBoxLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawComboBox (juce::Graphics& g,
                       int width, int height,
                       bool isButtonDown,
                       int buttonX, int buttonY,
                       int buttonW, int buttonH,
                       juce::ComboBox& box) override;
};

enum class LookMode
{
    Cooked = 0,
    Lufs,
    Static
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

    void showSettingsMenu();          // opens the left SETTINGS menu
    void setLookMode (LookMode mode); // updates processor + UI
    LookMode getLookMode() const;     // reads current mode from processor
    void openKlipBible();             // opens the Bible/help resource

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
    CustomLookAndFeel customLookAndFeel;
    DownwardComboBoxLookAndFeel comboLnf;

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

    // GUI burn value (cached from processor)
    float lastBurn = 0.0f;

    LookMode currentLookMode { LookMode::Cooked };

    // Local GUI state for gain-bypass toggle
    bool isGainBypass = false;

    float targetFingerAngle = 0.0f;
    float currentFingerAngle = 0.0f;
    float fingerAnimSpeed = 0.15f; // seconds
    AnimationTimer animationTimer;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    // Timer for GUI updates
    void timerCallback() override;

    void startFingerAnimation (bool limiterMode);

    void showBypassInfoPopup();

    friend class MiddleFingerLookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruityClipAudioProcessorEditor)
};

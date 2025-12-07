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
//  Custom ComboBox for LOOK: arrow-only closed state + custom popup
//==============================================================
class LookComboBox : public juce::ComboBox
{
public:
    explicit LookComboBox (FruityClipAudioProcessor& p)
        : processor (p)
    {
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // Solid black background, white border
        g.setColour (juce::Colours::black);
        g.fillRect (bounds);

        g.setColour (juce::Colours::white);
        g.drawRect (bounds);

        // Draw a simple down-arrow in the centre (no text)
        const float arrowWidth  = bounds.getWidth() * 0.30f;
        const float arrowHeight = bounds.getHeight() * 0.30f;
        const float cx          = bounds.getCentreX();
        const float cy          = bounds.getCentreY();

        juce::Path arrow;
        arrow.addTriangle (
            cx - arrowWidth * 0.5f, cy - arrowHeight * 0.25f,
            cx + arrowWidth * 0.5f, cy - arrowHeight * 0.25f,
            cx,                     cy + arrowHeight * 0.5f);

        g.fillPath (arrow);
    }

    void showPopup() override
    {
        juce::PopupMenu menu;

        // 1) BYPASS (does not actually bypass, only teaches how)
        menu.addItem (1, "BYPASS");

        menu.addSeparator();

        // 2) LOOK modes – reflect the current parameter value for checkmarks
        const int lookMode = processor.getLookMode(); // 0=COOKED, 1=LUFS, 2=STATIC
        menu.addItem (2, "LOOK : COOKED", true, lookMode == 0);
        menu.addItem (3, "LOOK : LUFS",   true, lookMode == 1);
        menu.addItem (4, "LOOK : STATIC", true, lookMode == 2);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [this] (int result)
                            {
                                if (result == 1)
                                {
                                    // BYPASS help only – no parameter change
                                    juce::AlertWindow::showMessageBoxAsync (
                                        juce::AlertWindow::NoIcon,
                                        "Bypass tip",
                                        "Tap the GAIN logo to bypass the clipper circuit while keeping your input gain active.\n\n"
                                        "This lets you A/B level-matched audio with and without processing.",
                                        "OK",
                                        this);
                                }
                                else if (result >= 2 && result <= 4)
                                {
                                    // Map 2,3,4 -> 0,1,2 indices for the underlying ComboBox
                                    const int modeIndex = result - 2; // 0..2

                                    // This drives the existing ComboBoxAttachment for "lookMode"
                                    setSelectedItemIndex (modeIndex, juce::sendNotification);
                                }
                            });
    }

private:
    FruityClipAudioProcessor& processor;
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
    LookComboBox   lookBox;

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

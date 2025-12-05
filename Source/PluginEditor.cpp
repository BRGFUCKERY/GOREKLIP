#include "BinaryData.h"
#include "PluginEditor.h"

#include <cmath>

//==============================================================
// Custom Middle-Finger Knob LookAndFeel
//==============================================================
void MiddleFingerLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                                int x, int y, int width, int height,
                                                float sliderPosProportional,
                                                float rotaryStartAngle,
                                                float rotaryEndAngle,
                                                juce::Slider& slider)
{
    juce::ignoreUnused (rotaryStartAngle, rotaryEndAngle);

    if (! knobImage.isValid())
        return;

    juce::Graphics::ScopedSaveState save (g);

    auto bounds   = juce::Rectangle<float> ((float) x, (float) y,
                                            (float) width, (float) height);
    auto knobArea = bounds.reduced (width * 0.05f, height * 0.05f);

    const float imgW  = (float) knobImage.getWidth();
    const float imgH  = (float) knobImage.getHeight();
    const float scale = std::min (knobArea.getWidth()  / imgW,
                                  knobArea.getHeight() / imgH);

    juce::Rectangle<float> imgRect (0.0f, 0.0f, imgW * scale, imgH * scale);
    imgRect.setCentre (knobArea.getCentre());

    // Default angle mapping: ~7 o'clock to ~5 o'clock
    const float minAngle = juce::degreesToRadians (-135.0f);
    const float maxAngle = juce::degreesToRadians ( 135.0f);

    float angle = 0.0f;

    if (modeSlider != nullptr && &slider == modeSlider)
    {
        // MODE FINGER: two positions only
        const bool useLimiter = (slider.getValue() >= 0.5f);

        // 12 o'clock (up) = CLIPPER, 6 o'clock (down) = LIMITER
        const float angleDegrees = useLimiter ? 180.0f : 0.0f;
        angle = juce::degreesToRadians (angleDegrees);
    }
    else
    {
        // All other knobs (GAIN, SILK, SAT) – just map 0..1 to angle.
        // No SAT-based visual compensation so the gain finger never looks "stuck".
        angle = minAngle + (maxAngle - minAngle) * sliderPosProportional;
    }

    juce::AffineTransform t;
    t = t.rotated (angle, imgRect.getCentreX(), imgRect.getCentreY());
    g.addTransform (t);

    g.drawImage (knobImage,
                 imgRect.getX(), imgRect.getY(),
                 imgRect.getWidth(), imgRect.getHeight(),
                 0, 0, knobImage.getWidth(), knobImage.getHeight());
}

//==============================================================
// Editor
//==============================================================
FruityClipAudioProcessorEditor::FruityClipAudioProcessorEditor (FruityClipAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    // Load background
    bgImage = juce::ImageCache::getFromMemory (
        BinaryData::bg_png,
        BinaryData::bg_pngSize);

    // Load GOREKLIPER logo
    logoImage = juce::ImageCache::getFromMemory (
        BinaryData::gorekliper_logo_png,
        BinaryData::gorekliper_logo_pngSize);

    // Load finger knob image
    juce::Image fingerImage = juce::ImageCache::getFromMemory (
        BinaryData::finger_png,
        BinaryData::finger_pngSize);

    fingerLnf.setKnobImage (fingerImage);

    if (bgImage.isValid())
        setSize ((int)(bgImage.getWidth() * bgScale),
                 (int)(bgImage.getHeight() * bgScale));
    else
        setSize (600, 400);

    // ----------------------
    // SLIDERS
    // ----------------------
    auto setupKnob01 = [] (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (0.0, 1.0, 0.0001);
        s.setMouseDragSensitivity (250);
    };

    // GAIN uses dB range
    gainSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gainSlider.setMouseDragSensitivity (250);
    gainSlider.setRange (-12.0, 12.0, 0.01);

    setupKnob01 (silkSlider);
    setupKnob01 (satSlider);
    setupKnob01 (modeSlider); // MODE finger – param is bool, but we use 0..1 range

    gainSlider.setLookAndFeel (&fingerLnf);
    silkSlider.setLookAndFeel (&fingerLnf);
    satSlider.setLookAndFeel  (&fingerLnf);
    modeSlider.setLookAndFeel (&fingerLnf);

    addAndMakeVisible (gainSlider);
    addAndMakeVisible (silkSlider);
    addAndMakeVisible (satSlider);
    addAndMakeVisible (modeSlider);

    // ----------------------
    // LABELS
    // ----------------------
    auto setupLabel = [] (juce::Label& lbl, const juce::String& text)
    {
        lbl.setText (text, juce::dontSendNotification);
        lbl.setJustificationType (juce::Justification::centred);
        lbl.setColour (juce::Label::textColourId, juce::Colours::white);
        lbl.setFont (juce::Font (16.0f, juce::Font::bold));
    };

    setupLabel (gainLabel, "GAIN");
    setupLabel (silkLabel, "SILK");
    setupLabel (satLabel,  "SAT");
    setupLabel (modeLabel, "CLIPPER"); // will switch to LIMITER when mode is on

    addAndMakeVisible (gainLabel);
    addAndMakeVisible (silkLabel);
    addAndMakeVisible (satLabel);
    addAndMakeVisible (modeLabel);

    // ----------------------
    // PARAMETER ATTACHMENTS
    // ----------------------
    auto& apvts = processor.getParametersState();

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts,

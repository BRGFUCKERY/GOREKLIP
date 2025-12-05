#include "PluginEditor.h"

//==============================================================
// MiddleFingerLookAndFeel
//==============================================================
void MiddleFingerLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                                int x, int y, int width, int height,
                                                float sliderPosProportional,
                                                float /*rotaryStartAngle*/,
                                                float /*rotaryEndAngle*/,
                                                juce::Slider& slider)
{
    if (! knobImage.isValid())
    {
        LookAndFeel_V4::drawRotarySlider (g, x, y, width, height,
                                          sliderPosProportional,
                                          0.0f, 0.0f,
                                          slider);
        return;
    }

    juce::Graphics::ScopedSaveState save (g);

    auto bounds = juce::Rectangle<float> ((float)x, (float)y,
                                          (float)width, (float)height);

    auto knobArea = bounds.reduced (width * 0.05f, height * 0.05f);

    const float imgW = (float) knobImage.getWidth();
    const float imgH = (float) knobImage.getHeight();
    const float scale = std::min (knobArea.getWidth()  / imgW,
                                  knobArea.getHeight() / imgH);

    juce::Rectangle<float> imgRect (0.0f, 0.0f, imgW * scale, imgH * scale);
    imgRect.setCentre (knobArea.getCentre());

    // Left -> right arc (-45° to +45°)
    const float minAngle = juce::degreesToRadians (-45.0f);
    const float maxAngle = juce::degreesToRadians ( 45.0f);
    const float angle    = minAngle + (maxAngle - minAngle) * sliderPosProportional;

    juce::AffineTransform t;
    t = t.rotated (angle, imgRect.getCentreX(), imgRect.getCentreY());

    g.addTransform (t);
    g.drawImage (knobImage,
                 imgRect.getX(), imgRect.getY(),
                 imgRect.getWidth(), imgRect.getHeight(),
                 0, 0, knobImage.getWidth(), knobImage.getHeight());
}

//==============================================================
// Editor constructor
//==============================================================
FruityClipAudioProcessorEditor::FruityClipAudioProcessorEditor (FruityClipAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    // NOTE: make sure these BinaryData names match what JUCE generates.
    // bg.png  -> BinaryData::bg_png
    // finger  -> BinaryData::finger_png
    // logo    -> BinaryData::gorekliper_logo_png (or similar)
    //
    // Adjust these identifiers if your actual names differ.

    bgImage = juce::ImageCache::getFromMemory (
        BinaryData::bg_png,
        BinaryData::bg_pngSize);

    logoImage = juce::ImageCache::getFromMemory (
        BinaryData::gorekliper_logo_png,
        BinaryData::gorekliper_logo_pngSize);

    juce::Image fingerImage = juce::ImageCache::getFromMemory (
        BinaryData::finger_png,
        BinaryData::finger_pngSize);

    fingerLnf.setKnobImage (fingerImage);

    // Set editor size from background at 0.35 scale
    if (bgImage.isValid())
    {
        const int w = (int) (bgImage.getWidth()  * bgScale);
        const int h = (int) (bgImage.getHeight() * bgScale);
        setSize (w, h);
    }
    else
    {
        setSize (600, 400);
    }

    // Knob setup
    silkSlider.setLookAndFeel (&fingerLnf);
    satSlider.setLookAndFeel  (&fingerLnf);

    auto setupKnob = [] (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (0.0, 1.0, 0.0001);
        s.setMouseDragSensitivity (250);
    };

    setupKnob (silkSlider);
    setupKnob (satSlider);

    addAndMakeVisible (silkSlider);
    addAndMakeVisible (satSlider);

    // Labels
    silkLabel.setText ("SILK", juce::dontSendNotification);
    satLabel.setText  ("SAT",  juce::dontSendNotification);

    auto setupLabel = [] (juce::Label& lbl)
    {
        lbl.setJustificationType (juce::Justification::centred);
        lbl.setEditable (false, false, false);
        lbl.setColour (juce::Label::textColourId, juce::Colours::white);
        lbl.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        lbl.setFont (juce::Font (14.0f, juce::Font::bold));
    };

    setupLabel (silkLabel);
    setupLabel (satLabel);

    addAndMakeVisible (silkLabel);
    addAndMakeVisible (satLabel);

    // Attach to parameters
    auto& apvts = processor.getParametersState();
    satAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "satAmount",  satSlider);
    silkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "silkAmount", silkSlider);
}

FruityClipAudioProcessorEditor::~FruityClipAudioProcessorEditor()
{
    silkSlider.setLookAndFeel (nullptr);
    satSlider.setLookAndFeel  (nullptr);
}

//==============================================================
// Painting
//==============================================================
void FruityClipAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    const int w = getWidth();
    const int h = getHeight();

    if (bgImage.isValid())
    {
        g.drawImageWithin (bgImage,
                           0, 0, w, h,
                           juce::RectanglePlacement::stretchToFit);
    }

    // Draw GOREKLIPER logo near top center
    if (logoImage.isValid())
    {
        const int logoMaxW = (int) (w * 0.7f);
        const int logoMaxH = (int) (h * 0.3f);

        const float imgW = (float) logoImage.getWidth();
        const float imgH = (float) logoImage.getHeight();

        const float scale = std::min ((float) logoMaxW / imgW,
                                      (float) logoMaxH / imgH);

        const int drawW = (int) (imgW * scale);
        const int drawH = (int) (imgH * scale);

        const int x = (w - drawW) / 2;
        const int y = (int) (h * 0.05f); // small margin from top

        g.setOpacity (1.0f);
        g.drawImage (logoImage,
                     x, y, drawW, drawH,
                     0, 0, logoImage.getWidth(), logoImage.getHeight());
    }
}

//==============================================================
// Layout
//==============================================================
void FruityClipAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // Leave room at top for logo (~35% height)
    auto logoArea = bounds.removeFromTop ((int) (getHeight() * 0.35f));
    juce::ignoreUnused (logoArea);

    // Base knob size from editor, then shrink ~80%
    const int baseKnobSize = juce::jmin (getWidth() / 3, getHeight() / 3);
    const int knobSize     = juce::jmax (40, (int) (baseKnobSize * 0.2f));

    const int margin = 10;

    auto bottomArea = bounds.removeFromBottom (knobSize + 2 * margin + 20);

    auto leftArea  = bottomArea.removeFromLeft (getWidth() / 2);
    auto rightArea = bottomArea;

    auto silkKnobArea = leftArea.withSizeKeepingCentre (knobSize, knobSize);
    auto satKnobArea  = rightArea.withSizeKeepingCentre (knobSize, knobSize);

    silkSlider.setBounds (silkKnobArea);
    satSlider.setBounds  (satKnobArea);

    const int labelHeight = 18;

    silkLabel.setBounds (silkKnobArea.getX(),
                         silkKnobArea.getBottom() + 2,
                         silkKnobArea.getWidth(),
                         labelHeight);

    satLabel.setBounds  (satKnobArea.getX(),
                         satKnobArea.getBottom() + 2,
                         satKnobArea.getWidth(),
                         labelHeight);
}

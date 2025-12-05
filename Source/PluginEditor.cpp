#include "BinaryData.h"
#include "PluginEditor.h"

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
    juce::ignoreUnused (rotaryStartAngle, rotaryEndAngle, slider);

    if (! knobImage.isValid())
        return;

    juce::Graphics::ScopedSaveState save (g);

    auto bounds   = juce::Rectangle<float> ((float)x, (float)y, (float)width, (float)height);
    auto knobArea = bounds.reduced (width * 0.05f, height * 0.05f);

    const float imgW = (float) knobImage.getWidth();
    const float imgH = (float) knobImage.getHeight();
    const float scale = std::min (knobArea.getWidth()  / imgW,
                                  knobArea.getHeight() / imgH);

    juce::Rectangle<float> imgRect (0.0f, 0.0f, imgW * scale, imgH * scale);
    imgRect.setCentre (knobArea.getCentre());

    // WIDE ROTATION RANGE (~7 o'clock to ~5 o'clock)
    const float minAngle = juce::degreesToRadians (-135.0f);
    const float maxAngle = juce::degreesToRadians ( 135.0f);
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
// EDITOR CONSTRUCTOR
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
    auto setupKnob = [] (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (0.0, 1.0, 0.0001);
        s.setMouseDragSensitivity (250);
    };

    setupKnob (silkSlider);
    setupKnob (satSlider);

    silkSlider.setLookAndFeel (&fingerLnf);
    satSlider.setLookAndFeel  (&fingerLnf);

    addAndMakeVisible (silkSlider);
    addAndMakeVisible (satSlider);

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

    setupLabel (silkLabel, "SILK");
    setupLabel (satLabel,  "SAT");

    addAndMakeVisible (silkLabel);
    addAndMakeVisible (satLabel);

    // ----------------------
    // PARAMETER ATTACHMENTS
    // ----------------------
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
// PAINT
//==============================================================
void FruityClipAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();

    if (bgImage.isValid())
        g.drawImageWithin (bgImage, 0, 0, w, h, juce::RectanglePlacement::stretchToFit);
    else
        g.fillAll (juce::Colours::black);

    // BIG LOGO - glued to the top edge
    if (logoImage.isValid())
    {
        const float targetW = w * 0.90f;                  // very wide
        const float scale   = targetW / logoImage.getWidth();

        const int drawW = (int)(logoImage.getWidth()  * scale);
        const int drawH = (int)(logoImage.getHeight() * scale);

        const int x = (w - drawW) / 2;
        const int y = 0;                                  // flush with top

        g.drawImage (logoImage,
                     x, y, drawW, drawH,
                     0, 0, logoImage.getWidth(), logoImage.getHeight());
    }
}

//==============================================================
// LAYOUT
//==============================================================
void FruityClipAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    const int w = getWidth();
    const int h = getHeight();

    // Reserve just a small band for the logo visually (~20%),
    // leaving a big negative-space middle.
    const int logoSpace = (int)(h * 0.20f);
    bounds.removeFromTop (logoSpace);

    // C-layout: knobs smaller & way lower near the bottom.
    const int knobSize = juce::jmax (60, (int)(h * 0.18f)); // smaller, subtle
    const int spacing  = (int)(w * 0.06f);

    const int totalW   = knobSize * 2 + spacing;
    const int startX   = (w - totalW) / 2;

    // Push knobs near the bottom of the whole editor (brutal negative space)
    const int bottomMargin = (int)(h * 0.04f);
    const int knobY        = h - knobSize - bottomMargin;

    silkSlider.setBounds (startX, knobY, knobSize, knobSize);
    satSlider .setBounds (startX + knobSize + spacing, knobY, knobSize, knobSize);

    const int labelH = 20;

    silkLabel.setBounds (silkSlider.getX(),
                         silkSlider.getBottom() + 2,
                         silkSlider.getWidth(),
                         labelH);

    satLabel.setBounds (satSlider.getX(),
                        satSlider.getBottom() + 2,
                        satSlider.getWidth(),
                        labelH);
}

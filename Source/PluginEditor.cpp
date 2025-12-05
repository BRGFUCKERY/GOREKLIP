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

    // GAIN uses dB range, set below
    gainSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gainSlider.setMouseDragSensitivity (250);

    setupKnob (silkSlider);
    setupKnob (satSlider);

    gainSlider.setLookAndFeel (&fingerLnf);
    silkSlider.setLookAndFeel (&fingerLnf);
    satSlider.setLookAndFeel  (&fingerLnf);

    addAndMakeVisible (gainSlider);
    addAndMakeVisible (silkSlider);
    addAndMakeVisible (satSlider);

    // ----------------------
    // LIMIT BUTTON
    // ----------------------
    limitButton.setButtonText ("LIMIT");
    limitButton.setClickingTogglesState (true);
    addAndMakeVisible (limitButton);

    // When limiter is on, SAT is disabled visually
    limitButton.onStateChange = [this]
    {
        const bool useLimiter = limitButton.getToggleState();
        satSlider.setEnabled (! useLimiter);
    };

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

    addAndMakeVisible (gainLabel);
    addAndMakeVisible (silkLabel);
    addAndMakeVisible (satLabel);

    // ----------------------
    // PARAMETER ATTACHMENTS
    // ----------------------
    auto& apvts = processor.getParametersState();

    // GAIN in dB (-12..+12)
    gainSlider.setRange (-12.0, 12.0, 0.01);
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "inputGain", gainSlider);

    satAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "satAmount",  satSlider);

    silkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "silkAmount", silkSlider);

    limitAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                        apvts, "useLimiter", limitButton);

    // Ensure SAT enabled/disabled state matches initial limiter param
    satSlider.setEnabled (! limitButton.getToggleState());
}

FruityClipAudioProcessorEditor::~FruityClipAudioProcessorEditor()
{
    gainSlider.setLookAndFeel (nullptr);
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

    // LOGO - crop invisible padding so the visible part touches the top
    if (logoImage.isValid())
    {
        const float targetW = w * 0.80f;
        const float scale   = targetW / logoImage.getWidth();

        const int drawW = (int)(logoImage.getWidth()  * scale);
        const int drawH = (int)(logoImage.getHeight() * scale);

        const int x = (w - drawW) / 2;
        const int y = 0; // absolutely top

        // --- CROP TOP 20% OF SOURCE LOGO ---
        const int cropY      = (int)(logoImage.getHeight() * 0.20f);   // remove top 20%
        const int cropHeight = logoImage.getHeight() - cropY;          // keep lower 80%

        g.drawImage (logoImage,
                     x, y, drawW, drawH,     // destination
                     0, cropY,               // source x, y (start 20% down)
                     logoImage.getWidth(),
                     cropHeight);            // source height
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

    // Reserve a thin band for the logo visually (~18% height)
    const int logoSpace = (int)(h * 0.18f);
    bounds.removeFromTop (logoSpace);

    // Knobs
    const int knobSize = juce::jmax (50, (int)(h * 0.144f));

    const int spacing  = (int)(w * 0.07f);
    const int totalW   = knobSize * 3 + spacing * 2;
    const int startX   = (w - totalW) / 2;

    const int bottomMargin = (int)(h * 0.05f);
    const int knobY        = h - knobSize - bottomMargin;

    // GAIN, SILK, SAT knobs
    gainSlider.setBounds (startX,
                          knobY,
                          knobSize, knobSize);

    silkSlider.setBounds (startX + knobSize + spacing,
                          knobY,
                          knobSize, knobSize);

    satSlider .setBounds (startX + (knobSize + spacing) * 2,
                          knobY,
                          knobSize, knobSize);

    const int labelH = 20;

    gainLabel.setBounds (gainSlider.getX(),
                         gainSlider.getBottom() + 2,
                         gainSlider.getWidth(),
                         labelH);

    silkLabel.setBounds (silkSlider.getX(),
                         silkSlider.getBottom() + 2,
                         silkSlider.getWidth(),
                         labelH);

    satLabel.setBounds (satSlider.getX(),
                        satSlider.getBottom() + 2,
                        satSlider.getWidth(),
                        labelH);

    // LIMIT button on the right side above SAT
    const int buttonW = 70;
    const int buttonH = 24;
    const int buttonX = satSlider.getX() + (satSlider.getWidth() - buttonW) / 2;
    const int buttonY = satSlider.getY() - buttonH - 6;

    limitButton.setBounds (buttonX, buttonY, buttonW, buttonH);
}

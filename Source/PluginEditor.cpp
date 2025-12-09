#include "BinaryData.h"
#include "PluginEditor.h"
#include "CustomLookAndFeel.h"

#include <cmath>

class KlipBibleComponent : public juce::Component
{
public:
    explicit KlipBibleComponent (juce::String textToShow) : text (std::move (textToShow)) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
        g.setColour (juce::Colours::white);
        g.setFont (16.0f);
        g.drawFittedText (text, getLocalBounds().reduced(20), juce::Justification::topLeft, 20);
    }

private:
    juce::String text;
};

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
    juce::ignoreUnused (slider, rotaryStartAngle, rotaryEndAngle);

    if (! knobImage.isValid())
        return;

    const float angle = juce::jmap (sliderPosProportional,
                                    0.0f, 1.0f,
                                    juce::degreesToRadians (220.0f),
                                    juce::degreesToRadians ( -40.0f));

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
    const float minAngle = juce::degreesToRadians (220.0f);
    const float maxAngle = juce::degreesToRadians (-40.0f);
    const float mappedAngle = minAngle + (maxAngle - minAngle) * sliderPosProportional;

    juce::AffineTransform t;
    t = t.rotated (mappedAngle, imgRect.getCentreX(), imgRect.getCentreY());
    g.addTransform (t);

    g.drawImage (knobImage,
                 imgRect.getX(), imgRect.getY(),
                 imgRect.getWidth(), imgRect.getHeight(),
                 0, 0, knobImage.getWidth(), knobImage.getHeight());
}

//==============================================================
// DownwardComboBoxLookAndFeel – transparent, arrow-only look
// (now with subtle hollow arrows, menus are handled via colours)
//==============================================================
void DownwardComboBoxLookAndFeel::drawComboBox (juce::Graphics& g,
                                                int width, int height,
                                                bool isButtonDown,
                                                int buttonX, int buttonY,
                                                int buttonW, int buttonH,
                                                juce::ComboBox& box)
{
    juce::ignoreUnused (isButtonDown, buttonX, buttonY, buttonW, buttonH, box);

    auto bounds = juce::Rectangle<float> (0.0f, 0.0f,
                                          (float) width, (float) height);

    // Fully transparent body - we only draw the tiny arrow overlay.
    g.setColour (juce::Colours::transparentBlack);
    g.fillRect (bounds);

    // Subtle hollow arrow on the right, same for all dropdowns
    const float arrowSize    = (float) height * 0.35f;
    const float arrowCenterX = bounds.getRight() - arrowSize * 0.9f;
    const float arrowCenterY = bounds.getCentreY();

    juce::Path arrow;
    arrow.startNewSubPath (arrowCenterX - arrowSize * 0.55f,
                           arrowCenterY - arrowSize * 0.25f);
    arrow.lineTo          (arrowCenterX,
                           arrowCenterY + arrowSize * 0.55f);
    arrow.lineTo          (arrowCenterX + arrowSize * 0.55f,
                           arrowCenterY - arrowSize * 0.25f);

    g.setColour (juce::Colours::white);
    g.strokePath (arrow,
                  juce::PathStrokeType ((float) height * 0.09f,
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));
}

//==============================================================
// Editor
//==============================================================
FruityClipAudioProcessorEditor::FrutyClipAudioProcessorEditor (FruityClipAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    auto& lookSelector         = lookBox;
    auto& oversamplingSelector = oversampleBox;
    auto& menuSelector         = lookBox;

    // Make sure all dropdown menus (SETTINGS, KLIPBIBLE, oversample)
    // use a flat black background with white text.
    comboLnf.setColour (juce::PopupMenu::backgroundColourId,            juce::Colours::black);
    comboLnf.setColour (juce::PopupMenu::textColourId,                  juce::Colours::white);
    comboLnf.setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colours::darkgrey);
    comboLnf.setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);

    setLookAndFeel (&customLookAndFeel);

    lookSelector.setColour (juce::ComboBox::backgroundColourId, juce::Colours::black);
    lookSelector.setColour (juce::ComboBox::textColourId,       juce::Colours::white);

    oversamplingSelector.setColour (juce::ComboBox::backgroundColourId, juce::Colours::black);
    oversamplingSelector.setColour (juce::ComboBox::textColourId,       juce::Colours::white);

    menuSelector.setColour (juce::ComboBox::backgroundColourId, juce::Colours::black);
    menuSelector.setColour (juce::ComboBox::textColourId,       juce::Colours::white);

    // Load background
    bgImage = juce::ImageCache::getFromMemory (BinaryData::bg_png,  BinaryData::bg_pngSize);
    slamImage = juce::ImageCache::getFromMemory (BinaryData::slam_jpg, BinaryData::slam_jpgSize);

    // Load GOREKLIPER logo
    logoImage = juce::ImageCache::getFromMemory (BinaryData::gorekliper_logo_png,
                                                 BinaryData::gorekliper_logo_pngSize);

    // Precompute a white version of the logo (same alpha)
    if (logoImage.isValid())
    {
        logoWhiteImage = logoImage.createCopy();

        juce::Image::BitmapData data (logoWhiteImage, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < data.height; ++y)
        {
            for (int x = 0; x < data.width; ++x)
            {
                auto c = data.getPixelColour (x, y);
                auto a = c.getAlpha();
                if (a > 0)
                {
                    data.setPixelColour (x, y,
                                         juce::Colour::fromRGBA (255, 255, 255, a));
                }
            }
        }
    }

    // Load finger knob image
    juce::Image fingerImage = juce::ImageCache::getFromMemory (BinaryData::finger_png,
                                                               BinaryData::finger_pngSize);
    fingerLnf.setKnobImage (fingerImage);

    if (bgImage.isValid())
        setSize ((int) (bgImage.getWidth() * bgScale),
                 (int) (bgImage.getHeight() * bgScale));
    else
        setSize (600, 400);

    // ----------------------
    // SLIDERS
    // ----------------------
    auto setupKnob01 = [] (FineControlSlider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (0.0, 1.0, 0.0001);
        s.setMouseDragSensitivity (250);
        s.setDragSensitivities (250, 800);
    };

    // GAIN uses dB range
    gainSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gainSlider.setRange (-24.0, 24.0, 0.01);
    gainSlider.setMouseDragSensitivity (250);
    gainSlider.setDragSensitivities (250, 800);

    // OTT / LOVE, SAT / DEATH, and MODE use 0..1
    setupKnob01 (ottSlider);
    setupKnob01 (satSlider);
    setupKnob01 (modeSlider);

    gainSlider.setLookAndFeel (&fingerLnf);
    ottSlider.setLookAndFeel  (&fingerLnf);
    satSlider.setLookAndFeel  (&fingerLnf);
    modeSlider.setLookAndFeel (&fingerLnf);

    addAndMakeVisible (gainSlider);
    addAndMakeVisible (ottSlider);
    addAndMakeVisible (satSlider);
    addAndMakeVisible (modeSlider);

    // ----------------------
    // GAIN LABEL (bypass clickable)
    // ----------------------
    gainLabel.setText ("GAIN", juce::dontSendNotification);
    gainLabel.setJustificationType (juce::Justification::centred);
    gainLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    gainLabel.setInterceptsMouseClicks (true, false);
    addAndMakeVisible (gainLabel);

    // ----------------------
    // LOOK / SETTINGS (top-left dropdown)
    // ----------------------
    lookBox.setName ("lookBox");
    lookBox.setTextWhenNothingSelected ("SETTINGS");
    lookBox.setJustificationType (juce::Justification::centred);
    lookBox.setColour (juce::ComboBox::textColourId,        juce::Colours::transparentWhite);
    lookBox.setColour (juce::ComboBox::outlineColourId,     juce::Colours::transparentBlack);
    lookBox.setColour (juce::ComboBox::backgroundColourId,  juce::Colours::transparentBlack);
    lookBox.setColour (juce::ComboBox::buttonColourId,      juce::Colours::transparentBlack);
    lookBox.setColour (juce::ComboBox::arrowColourId,       juce::Colours::white);

    // Combo itself doesn’t eat the click – editor handles it
    lookBox.setInterceptsMouseClicks (false, false);
    lookBox.setLookAndFeel (&comboLnf);
    addAndMakeVisible (lookBox);

    // ----------------------
    // OVERSAMPLE DROPDOWN (top-right, tiny, white "x1" etc.)
    // ----------------------
    oversampleBox.setName ("oversampleBox");
    oversampleBox.addItem ("x1",  1);
    oversampleBox.addItem ("x2",  2);
    oversampleBox.addItem ("x4",  3);
    oversampleBox.addItem ("x8",  4);
    oversampleBox.addItem ("x16", 5);
    oversampleBox.setSelectedId (1, juce::dontSendNotification);
    oversampleBox.setTextWhenNothingSelected ("x1");

    oversampleBox.setJustificationType (juce::Justification::centred);
    oversampleBox.setColour (juce::ComboBox::textColourId,        juce::Colours::white);
    oversampleBox.setColour (juce::ComboBox::outlineColourId,     juce::Colours::transparentBlack);
    oversampleBox.setColour (juce::ComboBox::backgroundColourId,  juce::Colours::transparentBlack);
    oversampleBox.setColour (juce::ComboBox::buttonColourId,      juce::Colours::transparentBlack);
    oversampleBox.setColour (juce::ComboBox::arrowColourId,       juce::Colours::white);
    oversampleBox.setLookAndFeel (&comboLnf);
    addAndMakeVisible (oversampleBox);

    // ----------------------
    // PARAMETER ATTACHMENTS
    // ----------------------
    auto& apvts = processor.getParametersState();

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "inputGain", gainSlider);
    ottAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "ottAmount", ottSlider);
    satAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "satAmount", satSlider);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "modeBlend", modeSlider);

    // ----------------------
    // TIMER
    // ----------------------
    startTimerHz (30);
}

FruityClipAudioProcessorEditor::~FruityClipAudioProcessorEditor()
{
    setLookAndFeel (nullptr);

    gainSlider.setLookAndFeel (nullptr);
    ottSlider.setLookAndFeel  (nullptr);
    satSlider.setLookAndFeel  (nullptr);
    modeSlider.setLookAndFeel (nullptr);

    lookBox.setLookAndFeel     (nullptr);
    oversampleBox.setLookAndFeel (nullptr);
}

//==============================================================
// Layout
//==============================================================
void FruityClipAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    const int w = bounds.getWidth();
    const int h = bounds.getHeight();

    const int topMargin = 10;
    const int barH      = 24;

    // Left: SETTINGS arrow-only box
    const int lookSize = barH;
    const int lookX    = topMargin;
    const int lookY    = topMargin;
    lookBox.setBounds (lookX, lookY, lookSize, barH);

    // Right: oversample text + arrow, same top margin, pinned right
    const int osW = juce::jmax (60, w / 10);
    const int osH = barH;
    const int osX = w - osW - topMargin;
    const int osY = topMargin;
    oversampleBox.setBounds (osX, osY, osW, osH);

    // 4 knobs in a row (GAIN, LOVE, DEATH, CLIPPER)
    const int knobSize = juce::jmin (w / 7, h / 3);
    const int spacing  = knobSize / 2;

    const int totalWidth = 4 * knobSize + 3 * spacing;
    const int startX     = (w - totalWidth) / 2;
    const int centerY    = h - knobSize - 40;

    gainSlider.setBounds (startX,                         centerY, knobSize, knobSize);
    ottSlider.setBounds  (startX + (knobSize + spacing),  centerY, knobSize, knobSize);
    satSlider.setBounds  (startX + 2 * (knobSize + spacing), centerY, knobSize, knobSize);
    modeSlider.setBounds (startX + 3 * (knobSize + spacing), centerY, knobSize, knobSize);

    gainLabel.setBounds (gainSlider.getX(), gainSlider.getBottom() + 4,
                         knobSize, 20);
}

//==============================================================
// Painting
//==============================================================
void FruityClipAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    if (bgImage.isValid())
    {
        auto area = getLocalBounds().toFloat();
        auto imageArea = juce::Rectangle<float> (0.0f, 0.0f,
                                                 (float) bgImage.getWidth(),
                                                 (float) bgImage.getHeight());

        imageArea = imageArea.withSizeKeepingCentre (area.getWidth()  / bgScale,
                                                     area.getHeight() / bgScale);

        g.drawImage (bgImage, imageArea);
    }

    if (slamImage.isValid())
    {
        auto area = getLocalBounds().toFloat();
        auto slamArea = juce::Rectangle<float> (0.0f, 0.0f,
                                                (float) slamImage.getWidth(),
                                                (float) slamImage.getHeight());

        slamArea = slamArea.withSizeKeepingCentre (area.getWidth()  / slamScale,
                                                   area.getHeight() / slamScale);

        g.setOpacity (0.0f);
        g.drawImage (slamImage, slamArea);
        g.setOpacity (1.0f);
    }

    if (logoWhiteImage.isValid())
    {
        const int size = 120;
        const int margin = 10;

        auto logoArea = juce::Rectangle<int> (margin,
                                              getHeight() - size - margin,
                                              size, size);

        g.drawImageWithin (logoWhiteImage,
                           logoArea.getX(), logoArea.getY(),
                           logoArea.getWidth(), logoArea.getHeight(),
                           juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yBottom,
                           true);
    }
}

//==============================================================
// Settings menu + KlipBible popup
//==============================================================
LookMode FruityClipAudioProcessorEditor::getLookMode() const
{
    return (LookMode) processor.getLookMode();
}

void FruityClipAudioProcessorEditor::setLookMode (LookMode mode)
{
    processor.setLookMode ((int) mode);
}

void FruityClipAudioProcessorEditor::openKlipBible()
{
    juce::String klipText;
    klipText << "KLIPBIBLE\n\n";
    klipText << "Here you explain bypass, gain logo, all the nerd stuff etc.\n";

    auto* popup = new KlipBibleComponent (kl

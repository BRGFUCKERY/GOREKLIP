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
    {
        // Fallback: default JUCE knob if image missing
        juce::LookAndFeel_V4::drawRotarySlider (g, x, y, width, height,
                                                sliderPosProportional,
                                                rotaryStartAngle,
                                                rotaryEndAngle,
                                                slider);
        return;
    }

    juce::Rectangle<float> bounds ((float) x, (float) y,
                                   (float) width, (float) height);

    // Slight padding so the knob doesn't touch the edges
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

    if (&slider == gainSlider)
    {
        // GAIN finger: map dB range -12..+12 to minAngle..maxAngle
        const auto range = slider.getRange();
        const float minDb = (float) range.getStart();
        const float maxDb = (float) range.getEnd();
        const float valDb = (float) slider.getValue();

        float norm = (valDb - minDb) / (maxDb - minDb);
        norm = juce::jlimit (0.0f, 1.0f, norm);
        angle = minAngle + (maxAngle - minAngle) * norm;
    }
    else if (&slider == modeSlider)
    {
        // MODE finger: 0 = CLIPPER, 1 = LIMITER (two positions only)
        const bool useLimiter = (slider.getValue() >= 0.5f);
        angle = useLimiter ? juce::degreesToRadians (180.0f)
                           : juce::degreesToRadians (  0.0f);
    }
    else if (&slider == satSlider)
    {
        // SAT finger: 0..1, but slightly emphasize upper range
        float norm = (float) slider.getValue();
        norm = std::pow (juce::jlimit (0.0f, 1.0f, norm), 1.1f);
        angle = minAngle + (maxAngle - minAngle) * norm;
    }
    else
    {
        // Other sliders (if any)
        angle = minAngle + (maxAngle - minAngle) * sliderPosProportional;
    }

    // Draw rotated knob image
    g.saveState();

    g.addTransform (juce::AffineTransform::rotation (angle,
                                                     imgRect.getCentreX(),
                                                     imgRect.getCentreY()));

    g.drawImage (knobImage,
                 (int) imgRect.getX(),
                 (int) imgRect.getY(),
                 (int) imgRect.getWidth(),
                 (int) imgRect.getHeight(),
                 0, 0,
                 knobImage.getWidth(),
                 knobImage.getHeight());

    g.restoreState();
}

//==============================================================
// FineControlSlider – shift for fine control
//==============================================================
void FineControlSlider::mouseDown (const juce::MouseEvent& e)
{
    lastDragPos = e.position;
    juce::Slider::mouseDown (e);
}

void FineControlSlider::mouseDrag (const juce::MouseEvent& e)
{
    auto delta = e.position - lastDragPos;
    lastDragPos = e.position;

    const float sensitivity = e.mods.isShiftDown() ? fineSensitivity : normalSensitivity;
    const auto  range       = getRange();

    if (sensitivity > 0.0f)
    {
        const double motion     = delta.x + (-delta.y);
        const double deltaValue = (motion / sensitivity) * range.getLength();
        const double newValue   = juce::jlimit (range.getStart(), range.getEnd(), getValue() + deltaValue);

        setValue (newValue, juce::sendNotificationSync);
    }
}

void FineControlSlider::mouseUp (const juce::MouseEvent& e)
{
    juce::Slider::mouseUp (e);
}

//==============================================================
// Editor
//==============================================================
FruityClipAudioProcessorEditor::FruityClipAudioProcessorEditor (FruityClipAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p)
{
    setSize (700, 400);

    //==========================================================
    // Images
    //==========================================================
    bgImage = juce::ImageCache::getFromMemory (BinaryData::bg_png,
                                               BinaryData::bg_pngSize);

    slamImage = juce::ImageCache::getFromMemory (BinaryData::slam_png,
                                                 BinaryData::slam_pngSize);

    logoImage = juce::ImageCache::getFromMemory (BinaryData::gorekliper_logo_png,
                                                 BinaryData::gorekliper_logo_pngSize);

    // Precompute white logo for "pinned" look, keep alpha from original
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

    //==========================================================
    // Finger knob image + LNF
    //==========================================================
    juce::Image fingerImage = juce::ImageCache::getFromMemory (BinaryData::finger_png,
                                                               BinaryData::finger_pngSize);
    fingerLnf.setKnobImage (fingerImage);

    //==========================================================
    // SLIDERS
    //==========================================================
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
    gainSlider.setMouseDragSensitivity (250);
    gainSlider.setDragSensitivities (250, 800);
    gainSlider.setRange (-12.0, 12.0, 0.01);

    setupKnob01 (ottSlider);
    setupKnob01 (satSlider);
    setupKnob01 (modeSlider); // MODE finger – param is bool, but we use 0..1

    gainSlider.setLookAndFeel (&fingerLnf);
    ottSlider .setLookAndFeel (&fingerLnf);
    satSlider .setLookAndFeel (&fingerLnf);
    modeSlider.setLookAndFeel (&fingerLnf);

    addAndMakeVisible (gainSlider);
    addAndMakeVisible (ottSlider);
    addAndMakeVisible (satSlider);
    addAndMakeVisible (modeSlider);

    //==========================================================
    // Labels
    //==========================================================
    auto setupLabel = [] (juce::Label& lbl, const juce::String& text)
    {
        lbl.setText (text, juce::dontSendNotification);
        lbl.setJustificationType (juce::Justification::centred);
        lbl.setColour (juce::Label::textColourId, juce::Colours::white);

        juce::FontOptions opts (16.0f);
        opts = opts.withStyle ("Bold");
        lbl.setFont (juce::Font (opts));
    };

    setupLabel (gainLabel, "GAIN");
    setupLabel (ottLabel,  "LOVE");
    setupLabel (satLabel,  "DEATH");
    setupLabel (modeLabel, "CLIPPER"); // switches to LIMITER runtime

    addAndMakeVisible (gainLabel);
    addAndMakeVisible (ottLabel);
    addAndMakeVisible (satLabel);
    addAndMakeVisible (modeLabel);

    // Make GAIN label clickable to toggle bypass-after-gain
    gainLabel.setInterceptsMouseClicks (true, false);
    gainLabel.addMouseListener (this, false);

    // LUFS label – white bold font, slightly smaller
    lufsLabel.setJustificationType (juce::Justification::centred);
    lufsLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    {
        juce::FontOptions opts (15.4f);
        opts = opts.withStyle ("Bold");
        lufsLabel.setFont (juce::Font (opts));
    }
    lufsLabel.setText ("0.00 LUFS", juce::dontSendNotification);
    addAndMakeVisible (lufsLabel);

    //==========================================================
    // LOOK / SETTINGS (top-left, opens popup menu)
    //==========================================================
    lookBox.setTextWhenNothingSelected ("SETTINGS");
    lookBox.setJustificationType (juce::Justification::centred);
    lookBox.setColour (juce::ComboBox::textColourId,        juce::Colours::transparentWhite);
    lookBox.setColour (juce::ComboBox::outlineColourId,     juce::Colours::transparentBlack);
    lookBox.setColour (juce::ComboBox::backgroundColourId,  juce::Colours::transparentBlack);
    lookBox.setColour (juce::ComboBox::arrowColourId,       juce::Colours::white);
    lookBox.setInterceptsMouseClicks (false, false);
    lookBox.setLookAndFeel (&comboLnf);
    addAndMakeVisible (lookBox);

    //==========================================================
    // OVERSAMPLE DROPDOWN (top-right)
    //==========================================================
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
    oversampleBox.setColour (juce::ComboBox::arrowColourId,       juce::Colours::white);

    auto& apvts = processor.getAPVTS();

    oversampleAttachment.reset (
        new juce::AudioProcessorValueTreeState::ComboBoxAttachment (apvts,
                                                                    "oversample",
                                                                    oversampleBox));

    addAndMakeVisible (oversampleBox);

    //==========================================================
    // Slider attachments
    //==========================================================
    gainAttachment.reset (
        new juce::AudioProcessorValueTreeState::SliderAttachment (apvts, "gain", gainSlider));

    ottAttachment.reset (
        new juce::AudioProcessorValueTreeState::SliderAttachment (apvts, "ottMix", ottSlider));

    satAttachment.reset (
        new juce::AudioProcessorValueTreeState::SliderAttachment (apvts, "satMix", satSlider));

    modeAttachment.reset (
        new juce::AudioProcessorValueTreeState::SliderAttachment (apvts, "useLimiter", modeSlider));

    //==========================================================
    // MODE: enable/disable SAT & label text
    //==========================================================
    if (auto* modeParam = apvts.getRawParameterValue ("useLimiter"))
    {
        const bool useLimiter = (modeParam->load() >= 0.5f);
        satSlider.setEnabled (! useLimiter);
        modeLabel.setText (useLimiter ? "LIMITER" : "CLIPPER", juce::dontSendNotification);
    }

    modeSlider.onValueChange = [this]
    {
        const bool useLimiter = (modeSlider.getValue() >= 0.5f);
        satSlider.setEnabled (! useLimiter);
        modeLabel.setText (useLimiter ? "LIMITER" : "CLIPPER", juce::dontSendNotification);
    };

    fingerLnf.setControlledSliders (&gainSlider, &modeSlider, &satSlider);

    currentLookMode = getLookMode();

    // Start GUI update timer
    startTimerHz (30);
}

FruityClipAudioProcessorEditor::~FruityClipAudioProcessorEditor()
{
    stopTimer();
    gainSlider.setLookAndFeel (nullptr);
    ottSlider .setLookAndFeel (nullptr);
    satSlider .setLookAndFeel (nullptr);
    modeSlider.setLookAndFeel (nullptr);

    lookBox.setLookAndFeel (nullptr);
}

//==============================================================
// Painting
//==============================================================
void FruityClipAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();

    const float burnRaw = juce::jlimit (0.0f, 1.0f, lastBurn);
    const float burnShaped = std::pow (burnRaw, 1.3f);

    // 1) Base background
    if (bgImage.isValid())
        g.drawImageWithin (bgImage, 0, 0, w, h, juce::RectanglePlacement::stretchToFit);
    else
        g.fillAll (juce::Colours::black);

    // 2) Slam overlay
    if (slamImage.isValid() && burnShaped > 0.02f)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.setOpacity (burnShaped);
        g.drawImageWithin (slamImage, 0, 0, w, h, juce::RectanglePlacement::stretchToFit);
    }

    // 3) Logo with burn-to-white
    if (logoImage.isValid())
    {
        const float targetW = w * 0.80f;
        const float scale   = targetW / (float) logoImage.getWidth();

        const int drawW = (int) (logoImage.getWidth()  * scale);
        const int drawH = (int) (logoImage.getHeight() * scale);

        const int x = (w - drawW) / 2;
        const int y = 0;

        const int cropY     = (int) (logoImage.getHeight() * 0.20f);
        const int cropHeight = logoImage.getHeight() - cropY;

        g.setOpacity (1.0f);
        g.drawImage (logoImage,
                     x, y, drawW, drawH,
                     0, cropY,
                     logoImage.getWidth(),
                     cropHeight);

        if (logoWhiteImage.isValid() && burnShaped > 0.0f)
        {
            juce::Graphics::ScopedSaveState save2 (g);
            g.setOpacity (burnShaped);
            g.drawImage (logoWhiteImage,
                         x, y, drawW, drawH,
                         0, cropY,
                         logoWhiteImage.getWidth(),
                         cropHeight);
        }
    }

    //==========================================================
    // Layout knobs + labels
    //==========================================================
    const int knobsTop = h / 3;
    const int knobsH   = h / 3;
    const int knobsW   = w / 4;

    gainSlider.setBounds (0 * knobsW, knobsTop, knobsW, knobsH);
    ottSlider .setBounds (1 * knobsW, knobsTop, knobsW, knobsH);
    satSlider .setBounds (2 * knobsW, knobsTop, knobsW, knobsH);
    modeSlider.setBounds (3 * knobsW, knobsTop, knobsW, knobsH);

    const int labelH = 20;

    gainLabel.setBounds (gainSlider.getX(),
                         gainSlider.getBottom() + 2,
                         gainSlider.getWidth(),
                         labelH);

    ottLabel.setBounds (ottSlider.getX(),
                        ottSlider.getBottom() + 2,
                        ottSlider.getWidth(),
                        labelH);

    satLabel.setBounds (satSlider.getX(),
                        satSlider.getBottom() + 2,
                        satSlider.getWidth(),
                        labelH);

    modeLabel.setBounds (modeSlider.getX(),
                         modeSlider.getBottom() + 2,
                         modeSlider.getWidth(),
                         labelH);

    const int lufsHeight = 18;
    const int lufsY      = modeSlider.getY() - lufsHeight - 4;

    lufsLabel.setBounds (modeSlider.getX(),
                         lufsY,
                         modeSlider.getWidth(),
                         lufsHeight);
}

void FruityClipAudioProcessorEditor::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    const int lookW = juce::jmax (80, w / 6);
    const int lookH = juce::jmax (16, h / 20);
    const int lookX = 0;
    const int lookY = 0;

    lookBox.setBounds (lookX, lookY, lookW, lookH);

    const int osW = juce::jmax (60, w / 10);
    const int osH = juce::jmax (16, h / 20);
    const int osX = w - osW - 6;
    const int osY = 0;

    oversampleBox.setBounds (osX, osY, osW, osH);
}

//==============================================================
// Timer: pull burn + LUFS from processor
//==============================================================
void FruityClipAudioProcessorEditor::timerCallback()
{
    const bool bypassNow = processor.getGainBypass();
    currentLookMode = getLookMode();

    if (bypassNow)
    {
        lastBurn = 0.0f;
        lufsLabel.setVisible (false);
    }
    else
    {
        switch (currentLookMode)
        {
            case LookMode::Lufs:
                lastBurn = processor.getGuiBurnLufs();
                break;

            case LookMode::Static:
                lastBurn = 0.0f;
                break;

            case LookMode::Cooked:
            default:
                lastBurn = processor.getGuiBurn();
                break;
        }

        const float lufs      = processor.getGuiLufs();
        const bool  hasSignal = processor.getGuiHasSignal();

        if (! hasSignal)
        {
            lufsLabel.setVisible (false);
        }
        else
        {
            lufsLabel.setVisible (true);

            juce::String text = juce::String (lufs, 2) + " LUFS";
            if (lufs <= -99.0f)
                text = "-inf LUFS";

            lufsLabel.setText (text, juce::dontSendNotification);
        }
    }

    repaint();
}

//==============================================================
// LOOK MODE HELPERS
//==============================================================
LookMode FruityClipAudioProcessorEditor::getLookMode() const
{
    const int index = processor.getLookModeIndex();

    switch (index)
    {
        case 0:  return LookMode::Cooked;
        case 1:  return LookMode::Lufs;
        case 2:  return LookMode::Static;
        default: return LookMode::Cooked;
    }
}

void FruityClipAudioProcessorEditor::setLookMode (LookMode mode)
{
    currentLookMode = mode;

    int index = 0;
    switch (mode)
    {
        case LookMode::Cooked: index = 0; break;
        case LookMode::Lufs:   index = 1; break;
        case LookMode::Static: index = 2; break;
    }

    processor.setLookModeIndex (index);
    repaint();
}

void FruityClipAudioProcessorEditor::openKlipBible()
{
    showBypassInfoPopup();
}

//==============================================================
// SETTINGS POPUP
//==============================================================
void FruityClipAudioProcessorEditor::showSettingsMenu()
{
    juce::PopupMenu menu;
    auto mode = getLookMode();

    constexpr int idLookCooked = 1;
    constexpr int idLookLufs   = 2;
    constexpr int idLookStatic = 3;
    constexpr int idKlipBible  = 4;

    menu.addItem (idLookCooked,
                  "LOOK - COOKED",
                  true,
                  mode == LookMode::Cooked);

    menu.addItem (idLookLufs,
                  "LOOK - LUFS",
                  true,
                  mode == LookMode::Lufs);

    menu.addItem (idLookStatic,
                  "LOOK - STATIC",
                  true,
                  mode == LookMode::Static);

    menu.addSeparator();

    menu.addItem (idKlipBible,
                  "KLIPBIBLE",
                  true);

    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [this] (int result)
                        {
                            switch (result)
                            {
                                case idLookCooked:
                                    setLookMode (LookMode::Cooked);
                                    break;

                                case idLookLufs:
                                    setLookMode (LookMode::Lufs);
                                    break;

                                case idLookStatic:
                                    setLookMode (LookMode::Static);
                                    break;

                                case idKlipBible:
                                    openKlipBible();
                                    break;

                                default:
                                    break;
                            }
                        });
}

//==============================================================
// BYPASS INFO POPUP
//==============================================================
void FruityClipAudioProcessorEditor::showBypassInfoPopup()
{
    juce::AlertWindow::showMessageBoxAsync (
        juce::AlertWindow::InfoIcon,
        "BYPASS INFO",
        "Click the GAIN label to toggle circuit bypass.\n\n"
        "This bypasses only the processing (LOVE / DEATH / LIMITER / LOOK burn)\n"
        "but keeps the GAIN level the same, so you can A/B without loudness bias.",
        "OK");
}

//==============================================================
// Mouse handling
//==============================================================
void FruityClipAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    // Work in editor coordinates so child components forwarding events
    // (like the GAIN label) don't accidentally trigger the SETTINGS menu.
    auto posInEditor = e.getEventRelativeTo (this).getPosition();

    if (lookBox.getBounds().contains (posInEditor.toInt()))
    {
        showSettingsMenu();
        return;
    }

    juce::AudioProcessorEditor::mouseDown (e);
}

void FruityClipAudioProcessorEditor::mouseUp (const juce::MouseEvent& e)
{
    // Only react if the GAIN label was clicked
    if (e.eventComponent == &gainLabel || e.originalComponent == &gainLabel)
    {
        isGainBypass = ! isGainBypass;
        processor.setGainBypass (isGainBypass);

        gainLabel.setColour (juce::Label::textColourId,
                             isGainBypass ? juce::Colours::grey
                                          : juce::Colours::white);
    }
}

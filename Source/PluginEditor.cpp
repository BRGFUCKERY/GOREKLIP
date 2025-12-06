#include "BinaryData.h"
#include "PluginEditor.h"

#include <cmath>

//==============================================================
//  Helper: map LUFS -> 0..1 burn
//==============================================================
static float clamp01 (float v)
{
    return juce::jlimit (0.0f, 1.0f, v);
}

float FruityClipAudioProcessorEditor::mapLufsToBurn (float lufs)
{
    if (! std::isfinite (lufs))
        return 0.0f;

    // Below about -18 LUFS = pretty chill -> no burn
    const float low  = -18.0f;
    const float high =   0.0f;   // 0 LUFS and above = fully cooked

    if (lufs <= low)
        return 0.0f;
    if (lufs >= high)
        return 1.0f;

    float norm = (lufs - low) / (high - low);  // -18 -> 0, 0 -> 1
    norm = clamp01 (norm);

    // Bit of curve so mid loudness is calmer, top end slams
    return std::pow (norm, 1.5f);
}

//==============================================================
//  Middle-Finger LookAndFeel
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
        juce::LookAndFeel_V4::drawRotarySlider (g, x, y, width, height,
                                                sliderPosProportional,
                                                rotaryStartAngle, rotaryEndAngle,
                                                slider);
        return;
    }

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
        // MODE FINGER: two positions only (CLIPPER / LIMITER)
        const bool useLimiter = (slider.getValue() >= 0.5f);

        // 12 o'clock (up) = CLIPPER, 6 o'clock (down) = LIMITER
        const float angleDegrees = useLimiter ? 180.0f : 0.0f;
        angle = juce::degreesToRadians (angleDegrees);
    }
    else if (gainSlider != nullptr && &slider == gainSlider)
    {
        // GAIN FINGER: map the actual gain in dB (-12..+12) to angle
        const auto& range = slider.getRange();
        const float minDb = (float) range.getStart(); // -12
        const float maxDb = (float) range.getEnd();   // +12

        const float gainDb = (float) slider.getValue();

        float norm = (gainDb - minDb) / (maxDb - minDb);
        norm = clamp01 (norm);

        angle = minAngle + (maxAngle - minAngle) * norm;
    }
    else
    {
        // Normal knobs (OTT, SAT)
        angle = minAngle + (maxAngle - minAngle) * sliderPosProportional;
    }

    juce::AffineTransform t;
    t = t.rotated (angle, imgRect.getCentreX(), imgRect.getCentreY());
    g.addTransform (t);

    g.drawImage (knobImage,
                 imgRect.getX(), imgRect.getY(),
                 imgRect.getWidth(), imgRect.getHeight(),
                 0, 0,
                 knobImage.getWidth(), knobImage.getHeight());
}

//==============================================================
//  Editor
//==============================================================
FruityClipAudioProcessorEditor::FruityClipAudioProcessorEditor (FruityClipAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    // Load background
    bgImage = juce::ImageCache::getFromMemory (
        BinaryData::bg_png,
        BinaryData::bg_pngSize);

    // SLAM background
    slamImage = juce::ImageCache::getFromMemory (
        BinaryData::slam_jpg,
        BinaryData::slam_jpgSize);

    // Load GOREKLIPER logo
    logoImage = juce::ImageCache::getFromMemory (
        BinaryData::gorekliper_logo_png,
        BinaryData::gorekliper_logo_pngSize);

    // Precompute a white version of the logo (same alpha)
    if (logoImage.isValid())
    {
        logoWhiteImage = logoImage.createCopy();

        juce::Image::BitmapData d (logoWhiteImage,
                                   juce::Image::BitmapData::readWrite);
        for (int y = 0; y < d.height; ++y)
        {
            for (int x = 0; x < d.width; ++x)
            {
                auto c = d.getPixelColour (x, y);
                auto a = c.getAlpha();
                if (a > 0)
                {
                    d.setPixelColour (x, y,
                                      juce::Colour::fromRGBA (255, 255, 255, a));
                }
            }
        }
    }

    // Size: keep the wide ratio based on bg, not a square
    if (bgImage.isValid())
        setSize ((int) (bgImage.getWidth()  * bgScale),
                 (int) (bgImage.getHeight() * bgScale));
    else
        setSize (700, 350);

    //==========================================================
    // KNOBS
    //==========================================================
    auto setupKnob = [this] (juce::Slider& s, const juce::String& name, juce::Label& label)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setMouseDragSensitivity (250);
        s.setLookAndFeel (&fingerLnf);
        addAndMakeVisible (s);

        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, juce::Colours::white);
        label.setFont (juce::Font (12.0f, juce::Font::bold));
        addAndMakeVisible (label);
    };

    // Set knob image for all fingers
    juce::Image fingerImage = juce::ImageCache::getFromMemory (
        BinaryData::finger_png,
        BinaryData::finger_pngSize);

    fingerLnf.setKnobImage (fingerImage);

    setupKnob (gainSlider, "GAIN", gainLabel);
    setupKnob (ottSlider,  "OTT",  ottLabel);
    setupKnob (satSlider,  "SAT",  satLabel);
    setupKnob (modeSlider, "MODE", modeLabel);

    // Tell the LookAndFeel which sliders are gain/mode so it can do special angles
    fingerLnf.setGainAndModeSliders (&gainSlider, &modeSlider);

    //==========================================================
    // LUFS label (above MODE finger)
    //==========================================================
    lufsLabel.setJustificationType (juce::Justification::centred);
    lufsLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    lufsLabel.setFont (juce::Font (12.0f, juce::Font::plain));
    lufsLabel.setText ("--", juce::dontSendNotification);
    addAndMakeVisible (lufsLabel);

    //==========================================================
    // OVERSAMPLE DROPDOWN (top-right)
    //==========================================================
    oversampleBox.addItem ("x1",  1);
    oversampleBox.addItem ("x2",  2);
    oversampleBox.addItem ("x4",  3);
    oversampleBox.addItem ("x8",  4);
    oversampleBox.addItem ("x16", 5);
    oversampleBox.setSelectedId (1, juce::dontSendNotification); // default x1

    oversampleBox.setJustificationType (juce::Justification::centred);
    oversampleBox.setColour (juce::ComboBox::textColourId,       juce::Colours::white);
    oversampleBox.setColour (juce::ComboBox::outlineColourId,    juce::Colours::transparentBlack);
    oversampleBox.setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    oversampleBox.setColour (juce::ComboBox::arrowColourId,      juce::Colours::white);

    addAndMakeVisible (oversampleBox);

    //==========================================================
    // LOOK MENU BUTTON (top-left)
    //==========================================================
    lookMenuButton.setButtonText ("LOOK");
    lookMenuButton.setColour (juce::TextButton::buttonColourId,     juce::Colours::transparentBlack);
    lookMenuButton.setColour (juce::TextButton::textColourOnId,     juce::Colours::white);
    lookMenuButton.setColour (juce::TextButton::textColourOffId,    juce::Colours::white);
    lookMenuButton.setColour (juce::TextButton::outlineColourId,    juce::Colours::transparentBlack);
    lookMenuButton.setTriggeredOnMouseDown (true);

    lookMenuButton.onClick = [this]()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "LUFS METER",           true, lookMode == LookMode::LufsMeter);
        menu.addItem (2, "FUCKED METER",        true, lookMode == LookMode::FuckedMeter);
        menu.addItem (3, "LOOK: STATIC",        true, lookMode == LookMode::StaticFlat);
        menu.addItem (4, "LOOK: STATIC COOKED", true, lookMode == LookMode::StaticCooked);
        menu.addSeparator();
        menu.addItem (5, "ABOUT");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&lookMenuButton),
                            [this] (int result)
                            {
                                switch (result)
                                {
                                    case 1: lookMode = LookMode::LufsMeter;      break;
                                    case 2: lookMode = LookMode::FuckedMeter;    break;
                                    case 3: lookMode = LookMode::StaticFlat;     break;
                                    case 4: lookMode = LookMode::StaticCooked;   break;
                                    case 5:
                                        juce::AlertWindow::showMessageBoxAsync (
                                            juce::AlertWindow::InfoIcon,
                                            "ABOUT",
                                            "GOREKLIP V1 BETA");
                                        break;
                                    default: break;
                                }
                            });
    };

    addAndMakeVisible (lookMenuButton);

    //==========================================================
    // Attach knobs to parameters
    //==========================================================
    auto& apvts = processor.parameters;

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "inputGain",   gainSlider);

    // SILK is gone from GUI – we keep OTT and SAT only
    ottAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "ottAmount",   ottSlider);

    satAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "satAmount",   satSlider);

    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "useLimiter",  modeSlider);

    oversampleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, "oversampleMode", oversampleBox);

    //==========================================================
    // Start GUI updates
    //==========================================================
    startTimerHz (30); // 30 fps-ish
}

FruityClipAudioProcessorEditor::~FruityClipAudioProcessorEditor()
{
    gainSlider.setLookAndFeel (nullptr);
    ottSlider .setLookAndFeel (nullptr);
    satSlider .setLookAndFeel (nullptr);
    modeSlider.setLookAndFeel (nullptr);
}

//==============================================================
// PAINT
//==============================================================
void FruityClipAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();

    g.fillAll (juce::Colours::black);

    // Map burn into 0..1
    const float burnRaw    = clamp01 (lastBurn);
    const float burnShaped = std::pow (burnRaw, 2.0f);

    // ------------------------------------------------
    // 1) Base background
    // ------------------------------------------------
    if (bgImage.isValid())
        g.drawImageWithin (bgImage, 0, 0, w, h, juce::RectanglePlacement::stretchToFit);
    else
        g.fillAll (juce::Colours::black);

    // ------------------------------------------------
    // 2) Cooked background overlay (slam)
    // ------------------------------------------------
    if (slamImage.isValid() && burnShaped > 0.001f)
    {
        g.setOpacity (burnShaped);
        g.drawImageWithin (slamImage, 0, 0, w, h, juce::RectanglePlacement::stretchToFit);
        g.setOpacity (1.0f);
    }

    // ------------------------------------------------
    // 3) Darken edges as it gets more cooked (vibe)
    // ------------------------------------------------
    if (burnShaped > 0.0f)
    {
        g.setColour (juce::Colours::black.withAlpha (0.25f * burnShaped));
        g.fillRect (0, 0, w, h);
    }

    // ------------------------------------------------
    // 4) Logo in the upper area
    // ------------------------------------------------
    juce::Image logoToUse = logoImage;
    if (burnRaw > 0.85f && logoWhiteImage.isValid())
        logoToUse = logoWhiteImage;

    if (logoToUse.isValid())
    {
        const int logoW = (int) (w * 0.4f);
        const int logoH = (int) (logoToUse.getHeight() * (logoW / (float) logoToUse.getWidth()));

        const int logoX = (w - logoW) / 2;
        const int logoY = (int) (h * 0.12f);

        g.drawImageWithin (logoToUse,
                           logoX, logoY,
                           logoW, logoH,
                           juce::RectanglePlacement::centred);
    }
}

//==============================================================
// LAYOUT
//==============================================================
void FruityClipAudioProcessorEditor::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    // OVERSAMPLE BOX – tiny top-right
    const int osW = juce::jmax (40, w / 10);
    const int osH = juce::jmax (16, h / 20);
    const int osX = w - osW - 6;
    const int osY = 6;
    oversampleBox.setBounds (osX, osY, osW, osH);

    // LOOK MENU – top-left
    const int lookW = juce::jmax (50, w / 8);
    const int lookH = osH;
    const int lookX = 6;
    const int lookY = 6;
    lookMenuButton.setBounds (lookX, lookY, lookW, lookH);

    // 4 knobs in a row (GAIN, OTT, SAT, MODE)
    const int knobSize = juce::jmin (w / 7, h / 3);
    const int spacing  = knobSize / 3;
    const int totalW   = 4 * knobSize + 3 * spacing;
    const int startX   = (w - totalW) / 2;

    const int bottomMargin = (int) (h * 0.05f);
    const int knobY        = h - knobSize - bottomMargin;

    gainSlider.setBounds (startX + 0 * (knobSize + spacing), knobY, knobSize, knobSize);
    ottSlider .setBounds (startX + 1 * (knobSize + spacing), knobY, knobSize, knobSize);
    satSlider .setBounds (startX + 2 * (knobSize + spacing), knobY, knobSize, knobSize);
    modeSlider.setBounds (startX + 3 * (knobSize + spacing), knobY, knobSize, knobSize);

    const int labelH = 20;

    gainLabel.setBounds (gainSlider.getX(),
                         gainSlider.getBottom(),
                         gainSlider.getWidth(), labelH);

    ottLabel.setBounds  (ottSlider.getX(),
                         ottSlider.getBottom(),
                         ottSlider.getWidth(), labelH);

    satLabel.setBounds  (satSlider.getX(),
                         satSlider.getBottom(),
                         satSlider.getWidth(), labelH);

    modeLabel.setBounds (modeSlider.getX(),
                         modeSlider.getBottom(),
                         modeSlider.getWidth(), labelH);

    // LUFS label above MODE finger
    const int lufsHeight = 18;
    const int lufsY      = modeSlider.getY() - lufsHeight - 4;

    lufsLabel.setBounds (modeSlider.getX(),
                         lufsY,
                         modeSlider.getWidth(),
                         lufsHeight);
}

//==============================================================
// TIMER: Update burn + LUFS display
//==============================================================
void FruityClipAudioProcessorEditor::timerCallback()
{
    const float peakBurn = processor.getGuiBurn();  // 0..1 from processor slam logic
    const float lufs     = processor.getGuiLufs();  // momentary LUFS-ish

    float burn = 0.0f;

    switch (lookMode)
    {
        case LookMode::LufsMeter:      burn = mapLufsToBurn (lufs); break;
        case LookMode::FuckedMeter:    burn = peakBurn;             break;
        case LookMode::StaticFlat:     burn = 0.0f;                 break;
        case LookMode::StaticCooked:   burn = 1.0f;                 break;
        default:                       burn = peakBurn;             break;
    }

    lastBurn = clamp01 (burn);

    // LUFS text:
    //  - If basically silence, show "--"
    //  - Otherwise show value
    juce::String text;

    if (! std::isfinite (lufs) || lufs <= -80.0f)
        text = "--";
    else
        text = juce::String (lufs, 1) + " LUFS"; // 1 decimal is enough

    lufsLabel.setText (text, juce::dontSendNotification);

    repaint();
}

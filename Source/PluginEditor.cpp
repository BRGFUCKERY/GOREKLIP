#include "BinaryData.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
    // Map LUFS value to 0..1 burn amount for "LUFS METER" look
    // -24 LUFS and quieter  -> ~0 burn
    //  0 LUFS and louder    -> 1.0 burn
    float mapLufsToBurn (float lufs)
    {
        if (! std::isfinite (lufs) || lufs <= -80.0f)
            return 0.0f;

        const float minLufs = -24.0f;
        const float maxLufs =   0.0f;

        float t = (lufs - minLufs) / (maxLufs - minLufs);
        t = juce::jlimit (0.0f, 1.0f, t);

        // Slight curve so mid-loudness doesn't instantly max the burn
        return std::pow (t, 1.5f);
    }
}

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
        // Fallback: draw a simple circle if image is missing
        const float radius = (float) juce::jmin (width, height) * 0.5f;
        const float centreX = (float) x + (float) width  * 0.5f;
        const float centreY = (float) y + (float) height * 0.5f;

        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.fillEllipse (centreX - radius, centreY - radius,
                       radius * 2.0f, radius * 2.0f);
        return;
    }

    const float angle = juce::jmap (sliderPosProportional, 0.0f, 1.0f,
                                    -juce::MathConstants<float>::pi * 0.75f,
                                     juce::MathConstants<float>::pi * 0.75f);

    juce::AffineTransform transform;
    transform = transform.rotated (angle,
                                   knobImage.getWidth()  * 0.5f,
                                   knobImage.getHeight() * 0.5f);

    juce::Image rotated = knobImage.createCopy();
    {
        juce::Graphics g2 (rotated);
        g2.addTransform (transform.inverted());
        g2.drawImageTransformed (knobImage, transform);
    }

    const float scale = (float) juce::jmin (width, height) /
                        (float) juce::jmax (knobImage.getWidth(), knobImage.getHeight()) * 0.90f;

    const int drawW = (int) (knobImage.getWidth()  * scale);
    const int drawH = (int) (knobImage.getHeight() * scale);

    const int centreX = x + width  / 2;
    const int centreY = y + height / 2;

    const int destX = centreX - drawW / 2;
    const int destY = centreY - drawH / 2;

    g.drawImage (rotated, destX, destY, drawW, drawH,
                 0, 0, rotated.getWidth(), rotated.getHeight());
}

//==============================================================
// Editor constructor
//==============================================================
FruityClipAudioProcessorEditor::FruityClipAudioProcessorEditor (FruityClipAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (600, 400);

    // Load background image (bg.png) and slam image (slam.jpg)
    backgroundImage = juce::ImageCache::getFromMemory (
        BinaryData::bg_png,
        BinaryData::bg_pngSize);

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
    juce::Image fingerImage = juce::ImageCache::getFromMemory (
        BinaryData::finger_png,
        BinaryData::finger_pngSize);

    middleFingerLnf.setKnobImage (fingerImage);

    gainSlider.setLookAndFeel (&middleFingerLnf);
    silkSlider.setLookAndFeel (&middleFingerLnf);
    ottSlider .setLookAndFeel (&middleFingerLnf);
    satSlider .setLookAndFeel (&middleFingerLnf);
    modeSlider.setLookAndFeel (&middleFingerLnf);

    auto setupKnob = [] (juce::Slider& s, juce::Label& lbl, const juce::String& text)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

        lbl.setText (text, juce::dontSendNotification);
        lbl.setJustificationType (juce::Justification::centred);
        lbl.setColour (juce::Label::textColourId, juce::Colours::white);
    };

    setupKnob (gainSlider, gainLabel, "INPUT");
    setupKnob (silkSlider, silkLabel, "SILK");
    setupKnob (ottSlider,  ottLabel,  "OTT");
    setupKnob (satSlider,  satLabel,  "SAT");
    setupKnob (modeSlider, modeLabel, "MODE");

    addAndMakeVisible (gainSlider);
    addAndMakeVisible (silkSlider);
    addAndMakeVisible (ottSlider);
    addAndMakeVisible (satSlider);
    addAndMakeVisible (modeSlider);

    addAndMakeVisible (gainLabel);
    addAndMakeVisible (silkLabel);
    addAndMakeVisible (ottLabel);
    addAndMakeVisible (satLabel);
    addAndMakeVisible (modeLabel);

    // LUFS label
    lufsLabel.setJustificationType (juce::Justification::centred);
    lufsLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    {
        // original 14.0f -> about +10% = 15.4f
        juce::FontOptions opts (15.4f);
        opts = opts.withStyle ("Bold");
        lufsLabel.setFont (juce::Font (opts));
    }
    lufsLabel.setText ("-- LUFS", juce::dontSendNotification);
    addAndMakeVisible (lufsLabel);

    // ----------------------
    // OVERSAMPLE DROPDOWN (top-right, tiny, white "x1" etc.)
    // ----------------------
    oversampleBox.addItem ("x1",  1);
    oversampleBox.addItem ("x2",  2);
    oversampleBox.addItem ("x4",  3);
    oversampleBox.addItem ("x8",  4);
    oversampleBox.addItem ("x16", 5);
    oversampleBox.setSelectedId (1, juce::dontSendNotification); // default x1

    oversampleBox.setJustificationType (juce::Justification::centred);
    oversampleBox.setColour (juce::ComboBox::textColourId,        juce::Colours::white);
    oversampleBox.setColour (juce::ComboBox::outlineColourId,     juce::Colours::transparentBlack);
    oversampleBox.setColour (juce::ComboBox::backgroundColourId,  juce::Colours::transparentBlack);
    oversampleBox.setColour (juce::ComboBox::arrowColourId,       juce::Colours::white);

    // NOTE: older JUCE ComboBox has no setFont(), so we skip that call here.
    addAndMakeVisible (oversampleBox);

    // ----------------------
    // LOOK / ABOUT MENU BUTTON (top-left)
    // ----------------------
    lookMenuButton.setButtonText ("≡");
    lookMenuButton.setTooltip ("Look / About");

    lookMenuButton.setColour (juce::TextButton::buttonColourId,    juce::Colours::transparentBlack);
    lookMenuButton.setColour (juce::TextButton::buttonOnColourId,  juce::Colours::transparentBlack);
    lookMenuButton.setColour (juce::TextButton::textColourOffId,   juce::Colours::white);
    lookMenuButton.setColour (juce::TextButton::textColourOnId,    juce::Colours::white);

    lookMenuButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1,   "LOOK: LUFS METER",      true, lookMode == LookMode::LufsMeter);
        menu.addItem (2,   "LOOK: FUCKED METER",    true, lookMode == LookMode::FuckedMeter);
        menu.addItem (3,   "LOOK: STATIC",          true, lookMode == LookMode::Static);
        menu.addItem (4,   "LOOK: STATIC COOKED",   true, lookMode == LookMode::StaticCooked);
        menu.addSeparator();
        menu.addItem (100, "ABOUT: GOREKLIP v1 BETA");

        menu.showMenuAsync (
            juce::PopupMenu::Options().withTargetComponent (&lookMenuButton),
            [this] (int result)
            {
                switch (result)
                {
                    case 1:  lookMode = LookMode::LufsMeter;     break;
                    case 2:  lookMode = LookMode::FuckedMeter;   break;
                    case 3:  lookMode = LookMode::Static;        break;
                    case 4:  lookMode = LookMode::StaticCooked;  break;

                    case 100:
                        juce::AlertWindow::showMessageBoxAsync (
                            juce::AlertWindow::InfoIcon,
                            "GOREKLIP",
                            "GOREKLIP v1 BETA");
                        break;

                    default:
                        break;
                }
            });
    };

    addAndMakeVisible (lookMenuButton);

    // ----------------------
    // PARAMETER ATTACHMENTS
    // ----------------------
    auto& apvts = processor.getParametersState();

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "inputGain", gainSlider);

    silkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "silkAmount", silkSlider);

    ottAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "ottAmount", ottSlider);

    satAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "satAmount", satSlider);

    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "useLimiter", modeSlider);

    oversampleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                        apvts, "oversampleMode", oversampleBox);

    startTimerHz (30);
}

//==============================================================
// Destructor
//==============================================================
FruityClipAudioProcessorEditor::~FruityClipAudioProcessorEditor()
{
    gainSlider.setLookAndFeel (nullptr);
    silkSlider.setLookAndFeel (nullptr);
    ottSlider .setLookAndFeel (nullptr);
    satSlider .setLookAndFeel (nullptr);
    modeSlider.setLookAndFeel (nullptr);
}

//==============================================================
// Paint
//==============================================================
void FruityClipAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    const int w = getWidth();
    const int h = getHeight();

    // ------------------------------------------------
    // 1) Background: original image + burn to slammed version
    // ------------------------------------------------
    if (backgroundImage.isValid())
    {
        g.setOpacity (1.0f);
        g.drawImageWithin (backgroundImage, 0, 0, w, h, juce::RectanglePlacement::stretchToFit);
    }

    // Burn (0..1)
    float burn = juce::jlimit (0.0f, 1.0f, lastBurn);

    if (slamImage.isValid() && burn > 0.001f)
    {
        const float burnShaped = std::pow (burn, 1.5f);

        juce::Graphics::ScopedSaveState save (g);
        g.setOpacity (burnShaped);
        g.drawImageWithin (slamImage, 0, 0, w, h, juce::RectanglePlacement::stretchToFit);
    }

    // ------------------------------------------------
    // 3) Logo – normal at low slam, fades to white as you pin it
    // ------------------------------------------------
    if (logoImage.isValid())
    {
        const float targetW = w * 0.80f;
        const float scale   = targetW / logoImage.getWidth();

        const int drawW = (int) (logoImage.getWidth()  * scale);
        const int drawH = (int) (logoImage.getHeight() * scale);

        const int x = (w - drawW) / 2;
        const int y = 0; // absolutely top

        // Crop top 20% of source logo (remove invisible padding)
        const int cropY      = (int) (logoImage.getHeight() * 0.20f); // remove top 20%
        const int cropHeight = logoImage.getHeight() - cropY;         // keep lower 80%

        // 3a) Draw original logo, fading out as burn increases
        {
            juce::Graphics::ScopedSaveState save (g);
            const float baseOpacity = 1.0f - burn;
            g.setOpacity (baseOpacity);
            g.drawImage (logoImage,
                         x, y, drawW, drawH,      // destination
                         0, cropY, logoImage.getWidth(), cropHeight); // source cropped
        }

        // 3b) Draw white logo on top, fading in as burn increases
        if (logoWhiteImage.isValid())
        {
            juce::Graphics::ScopedSaveState save (g);
            g.setOpacity (burn);
            g.drawImage (logoWhiteImage,
                         x, y, drawW, drawH,
                         0, cropY, logoWhiteImage.getWidth(), cropHeight);
        }
    }
}

//==============================================================
// Resized
//==============================================================
void FruityClipAudioProcessorEditor::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    // OVERSAMPLE BOX – tiny top-right
    const int osW = juce::jmax (50, w / 8);
    const int osH = juce::jmax (16, h / 20);
    const int osX = w - osW - 6;
    const int osY = 6;

    oversampleBox.setBounds (osX, osY, osW, osH);

    // LOOK / ABOUT button – top-left, same size as oversample box
    const int menuW = osW;
    const int menuH = osH;
    const int menuX = 6;
    const int menuY = 6;

    lookMenuButton.setBounds (menuX, menuY, menuW, menuH);

    // 5 knobs in a row
    const int knobSize = juce::jmin (w / 7, h / 3);
    const int spacing  = knobSize / 3;

    const int totalW   = knobSize * 5 + spacing * 4;
    const int startX   = (w - totalW) / 2;

    const int bottomMargin = (int)(h * 0.05f);
    const int knobY        = h - knobSize - bottomMargin;

    gainSlider.setBounds (startX + 0 * (knobSize + spacing), knobY, knobSize, knobSize);
    silkSlider.setBounds (startX + 1 * (knobSize + spacing), knobY, knobSize, knobSize);
    ottSlider .setBounds (startX + 2 * (knobSize + spacing), knobY, knobSize, knobSize);
    satSlider .setBounds (startX + 3 * (knobSize + spacing), knobY, knobSize, knobSize);
    modeSlider.setBounds (startX + 4 * (knobSize + spacing), knobY, knobSize, knobSize);

    const int labelH = 20;

    gainLabel.setBounds (gainSlider.getX(),
                         gainSlider.getBottom() + 2,
                         gainSlider.getWidth(),
                         labelH);

    silkLabel.setBounds (silkSlider.getX(),
                         silkSlider.getBottom() + 2,
                         silkSlider.getWidth(),
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

    // LUFS label – directly above the CLIPPER/LIMITER finger
    const int lufsHeight = 18;
    const int lufsY      = modeSlider.getY() - lufsHeight - 4;

    lufsLabel.setBounds (modeSlider.getX(),
                         lufsY,
                         modeSlider.getWidth(),
                         lufsHeight);
}

//==============================================================
// TIMER – pull burn + LUFS value from processor
//==============================================================
void FruityClipAudioProcessorEditor::timerCallback()
{
    const float peakBurn = processor.getGuiBurn();
    const float lufs     = processor.getGuiLufs();

    // Decide how the background should burn based on current LOOK mode
    float burn = 0.0f;

    switch (lookMode)
    {
        case LookMode::LufsMeter:
            burn = mapLufsToBurn (lufs);
            break;

        case LookMode::FuckedMeter:
            burn = peakBurn;  // original peak-driven slam visual
            break;

        case LookMode::Static:
            burn = 0.0f;      // static clean background
            break;

        case LookMode::StaticCooked:
            burn = 1.0f;      // static fully burnt background
            break;

        default:
            burn = peakBurn;
            break;
    }

    lastBurn = burn;

    // LUFS label – hide / collapse on silence or very low levels
    juce::String text;
    if (! std::isfinite (lufs) || lufs <= -80.0f)
        text = "--";
    else
        text = juce::String (lufs, 2) + " LUFS";

    lufsLabel.setText (text, juce::dontSendNotification);

    repaint();
}


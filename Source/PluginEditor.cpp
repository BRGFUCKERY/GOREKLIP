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
    else if (gainSlider != nullptr && &slider == gainSlider)
    {
        // GAIN FINGER: show ONLY the real gain param
        const auto& range = slider.getRange();
        const float minDb = (float) range.getStart(); // -12
        const float maxDb = (float) range.getEnd();   // +12

        const float gainDb = (float) slider.getValue();

        float norm = (gainDb - minDb) / (maxDb - minDb);
        norm = juce::jlimit (0.0f, 1.0f, norm);

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

    fingerLnf.setKnobImage (fingerImage);

    // Enable double-buffering & non-opaque background
    setOpaque (false);

    // Set LookAndFeel for sliders
    gainSlider.setLookAndFeel (&fingerLnf);
    ottSlider .setLookAndFeel (&fingerLnf);
    satSlider .setLookAndFeel (&fingerLnf);
    modeSlider.setLookAndFeel (&fingerLnf);

    gainSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gainSlider.setRange (-12.0, 12.0, 0.01);
    gainSlider.setDragSensitivities (250, 1000);

    ottSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    ottSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    ottSlider.setRange (0.0, 1.0, 0.001);
    ottSlider.setDragSensitivities (250, 1000);

    satSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    satSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    satSlider.setRange (0.0, 1.0, 0.001);
    satSlider.setDragSensitivities (250, 1000);

    modeSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    modeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    modeSlider.setRange (0.0, 1.0, 0.001);
    modeSlider.setDragSensitivities (250, 1000);

    // Knob labels
    auto setupLabel = [] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, juce::Colours::white);
        label.setFont (juce::Font (13.0f, juce::Font::bold));
    };

    setupLabel (gainLabel, "GAIN");
    setupLabel (ottLabel,  "LOVE");
    setupLabel (satLabel,  "DEATH");
    setupLabel (modeLabel, "CLIPPER");

    addAndMakeVisible (gainSlider);
    addAndMakeVisible (ottSlider);
    addAndMakeVisible (satSlider);
    addAndMakeVisible (modeSlider);

    addAndMakeVisible (gainLabel);
    addAndMakeVisible (ottLabel);
    addAndMakeVisible (satLabel);
    addAndMakeVisible (modeLabel);

    // LUFS label
    lufsLabel.setText ("", juce::dontSendNotification);
    lufsLabel.setJustificationType (juce::Justification::centred);
    lufsLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    lufsLabel.setFont (juce::Font (13.0f, juce::Font::bold));
    addAndMakeVisible (lufsLabel);

    // Oversample ComboBox (top-right)
    oversampleBox.setLookAndFeel (&comboLnf);
    oversampleBox.addItem ("x1", 1);
    oversampleBox.addItem ("x2", 2);
    oversampleBox.addItem ("x4", 3);
    oversampleBox.addItem ("x8", 4);
    oversampleBox.addItem ("x16", 5);
    oversampleBox.setSelectedId (1, juce::dontSendNotification);
    oversampleBox.setTooltip ("Oversampling");

    addAndMakeVisible (oversampleBox);

    // LOOK / SETTINGS ComboBox (top-left)
    lookBox.setLookAndFeel (&comboLnf);
    lookBox.setTextWhenNothingSelected ("");

    lookBox.clear();

    // Top heading: SETTINGS
    lookBox.addSectionHeading ("SETTINGS");

    // Options that actually change the look mode
    lookBox.addItem ("LOOK : COOKED",   1);
    lookBox.addItem ("LOOK : LOUDNESS", 2);  // was "LOOK : LUFS"
    lookBox.addItem ("LOOK : STATIC",   3);

    lookBox.addSeparator();

    // Info entry at the bottom – replaces old BYPASS menu item
    lookBox.addItem ("KLIPPERBIBLE", 100);

    addAndMakeVisible (lookBox);

    // ----------------------
    // PARAMETER ATTACHMENTS
    // ----------------------
    auto& apvts = processor.getParametersState();

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "inputGain", gainSlider);

    ottAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "ottAmount", ottSlider);

    satAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "satAmount",  satSlider);

    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "useLimiter", modeSlider);

    oversampleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                        apvts, "oversampleMode", oversampleBox);

    // Restore look mode, keep it in 1..3 and default to COOKED
    lastLookId = juce::jlimit (1, 3, processor.getStoredLookMode() + 1);
    lookBox.setSelectedId (lastLookId, juce::dontSendNotification);

    lookAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                        apvts, "lookMode", lookBox);

    lookBox.onChange = [this]
    {
        if (isRestoringLook)
            return;

        const int selectedId = lookBox.getSelectedId();

        // KLIPPERBIBLE info entry
        if (selectedId == 100)
        {
            isRestoringLook = true;
            showBypassInfoPopup();
            lookBox.setSelectedId (lastLookId, juce::sendNotificationSync);
            isRestoringLook = false;
            return;
        }

        lastLookId = selectedId;
        const int lookMode = juce::jlimit (0, 2, selectedId - 1);
        processor.setStoredLookMode (lookMode);
    };

    // Initial state from param
    if (auto* modeParam = apvts.getRawParameterValue ("useLimiter"))
    {
        const bool useLimiter = (modeParam->load() >= 0.5f);
        satSlider.setEnabled (! useLimiter);
        modeLabel.setText (useLimiter ? "LIMITER" : "CLIPPER", juce::dontSendNotification);
    }

    // When MODE changes, update SAT enable + text
    modeSlider.onValueChange = [this]
    {
        const bool useLimiter = (modeSlider.getValue() >= 0.5f);
        satSlider.setEnabled (! useLimiter);
        modeLabel.setText (useLimiter ? "LIMITER" : "CLIPPER", juce::dontSendNotification);
    };

    // Now the LNF knows which slider is which for special angles
    fingerLnf.setControlledSliders (&gainSlider, &modeSlider, &satSlider);

    // Start GUI update timer (for burn animation / LUFS text)
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
    oversampleBox.setLookAndFeel (nullptr);
}

//==============================================================
// PAINT
//==============================================================
void FruityClipAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();

    // Map burn into 0..1
    const float burnRaw = juce::jlimit (0.0f, 1.0f, lastBurn);

    // Visual slam comes in later – you really have to hit it
    const float burnShaped = std::pow (burnRaw, 1.3f);

    // 1) Base background
    if (bgImage.isValid())
        g.drawImageWithin (bgImage, 0, 0, w, h, juce::RectanglePlacement::stretchToFit);
    else
        g.fillAll (juce::Colours::black);

    // 2) Slam background
    if (slamImage.isValid() && burnShaped > 0.02f)
    {
        juce::Graphics::ScopedSaveState save (g);

        g.setOpacity (burnShaped);
        g.drawImageWithin (slamImage, 0, 0, w, h, juce::RectanglePlacement::stretchToFit);
    }

    // 3) Logo – normal at low slam, fades to white as you pin it
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
            juce::Graphics::ScopedSaveState save2 (g);
            const float baseOpacity = 1.0f - burnShaped;
            g.setOpacity (baseOpacity);
            g.drawImage (logoImage,
                         x, y, drawW, drawH,
                         0, cropY,
                         logoImage.getWidth(),
                         cropHeight);
        }

        // 3b) Draw white logo overlay, fading in with burn
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
}

//==============================================================
// LAYOUT
//==============================================================
void FruityClipAudioProcessorEditor::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    // LOOK / SETTINGS combo – hug the top-left corner
    const int lookW = juce::jmax (80, w / 6);
    const int lookH = juce::jmax (16, h / 20);
    const int lookX = 0;
    const int lookY = 0;

    lookBox.setBounds (lookX, lookY, lookW, lookH);

    // OVERSAMPLE BOX – tiny top-right
    const int osW = juce::jmax (60, w / 10);
    const int osH = juce::jmax (16, h / 20);
    const int osX = w - osW - 6;
    const int osY = 6;

    oversampleBox.setBounds (osX, osY, osW, osH);

    // Knob layout: 2x2 grid
    const int marginX = 40;
    const int marginY = 120;
    const int spacingX = 40;
    const int spacingY = 80;

    const int knobWidth  = (w - 2 * marginX - spacingX) / 2;
    const int knobHeight = knobWidth;

    const int leftX  = marginX;
    const int rightX = marginX + knobWidth + spacingX;

    const int topY    = marginY;
    const int bottomY = marginY + knobHeight + spacingY;

    gainSlider.setBounds (leftX,  topY,    knobWidth, knobHeight);
    ottSlider .setBounds (rightX, topY,    knobWidth, knobHeight);
    satSlider .setBounds (leftX,  bottomY, knobWidth, knobHeight);
    modeSlider.setBounds (rightX, bottomY, knobWidth, knobHeight);

    const int labelHeight = 20;

    gainLabel.setBounds (gainSlider.getX(),
                         gainSlider.getBottom() + 4,
                         knobWidth,
                         labelHeight);

    ottLabel.setBounds (ottSlider.getX(),
                        ottSlider.getBottom() + 4,
                        knobWidth,
                        labelHeight);

    satLabel.setBounds (satSlider.getX(),
                        satSlider.getBottom() + 4,
                        knobWidth,
                        labelHeight);

    modeLabel.setBounds (modeSlider.getX(),
                         modeSlider.getBottom() + 4,
                         knobWidth,
                         labelHeight);

    // LUFS label above MODE finger
    const int lufsW = knobWidth;
    const int lufsH = 20;
    const int lufsX = modeSlider.getX();
    const int lufsY = modeSlider.getY() - lufsH - 4;
    lufsLabel.setBounds (lufsX, lufsY, lufsW, lufsH);
}

//==============================================================
// Mouse handling – logo bypass + KLIPPERBIBLE is in the menu
//==============================================================
void FruityClipAudioProcessorEditor::mouseUp (const juce::MouseEvent& e)
{
    // Click on the logo area toggles circuit bypass (for A/B)
    const int w = getWidth();

    if (logoImage.isValid())
    {
        const float targetW = w * 0.80f;
        const float scale   = targetW / logoImage.getWidth();

        const int drawW = (int) (logoImage.getWidth()  * scale);
        const int drawH = (int) (logoImage.getHeight() * scale);

        const int x = (w - drawW) / 2;
        const int y = 0; // top

        const int cropY      = (int) (logoImage.getHeight() * 0.20f);
        const int cropHeight = logoImage.getHeight() - cropY;

        juce::Rectangle<int> logoBounds (x, y, drawW, drawH);
        if (logoBounds.contains (e.getPosition()))
        {
            isGainBypass = ! isGainBypass;
            processor.setGainBypass (isGainBypass);
            return;
        }

        juce::ignoreUnused (cropHeight);
    }

    juce::AudioProcessorEditor::mouseUp (e);
}

//==============================================================
// Timer – GUI updates
//==============================================================
void FruityClipAudioProcessorEditor::timerCallback()
{
    // Decide which burn to use based on current look mode
    const int lookMode = processor.getLookMode(); // 0=COOKED, 1=LUFS, 2=STATIC

    float guiBurnValue = 0.0f;

    if (lookMode == 1)
        guiBurnValue = processor.getGuiBurnLufs();  // LUFS-driven
    else
        guiBurnValue = processor.getGuiBurn();      // normal burn

    lastBurn = guiBurnValue;

    // LUFS label text (only when we actually have signal)
    if (processor.getGuiHasSignal())
    {
        const float lufs = processor.getGuiLufs();
        lufsLabel.setText (juce::String (lufs, 1) + " LUFS", juce::dontSendNotification);
    }
    else
    {
        lufsLabel.setText ("", juce::dontSendNotification);
    }

    repaint();
}

//==============================================================
// KLIPPERBIBLE popup
//==============================================================
void FruityClipAudioProcessorEditor::showBypassInfoPopup()
{
    const juce::String message =
        "KLIPPERBIBLE\n\n"
        "BYPASS:\n"
        "• Tap the GAIN logo to bypass the clipping & saturation circuit.\n"
        "• The input GAIN stays active, so your level doesn’t jump.\n"
        "• LOVE, DEATH, limiter, oversampling and ceiling are parked while bypassed.\n"
        "• This lets you A/B the circuit tone at the same loudness instead of just louder vs quieter.\n\n"
        "FINGER TRICKS:\n"
        "• Hold SHIFT while moving any finger for fine-tune moves.\n"
        "• Flick the last finger up and down to switch between CLIPPER and LIMITER mode.\n\n"
        "FOLLOW ME ON INSTAGRAM @BORGORE\n";

    juce::AlertWindow::showMessageBoxAsync (
        juce::AlertWindow::NoIcon,
        "KLIPPERBIBLE",
        message);
}

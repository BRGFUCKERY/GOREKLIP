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
    : AudioProcessorEditor (&p),
      processor (p),
      lookBox ()
{
    // Force popup menus / alerts to be black background with white text (no blue)
    auto& lf = getLookAndFeel();
    lf.setColour (juce::PopupMenu::backgroundColourId,           juce::Colours::black);
    lf.setColour (juce::PopupMenu::textColourId,                 juce::Colours::white);
    lf.setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff202020));
    lf.setColour (juce::PopupMenu::highlightedTextColourId,      juce::Colours::white);

    lf.setColour (juce::AlertWindow::backgroundColourId, juce::Colours::black);
    lf.setColour (juce::AlertWindow::textColourId,       juce::Colours::white);
    lf.setColour (juce::AlertWindow::outlineColourId,    juce::Colours::white);

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
        if (auto* fine = dynamic_cast<FineControlSlider*> (&s))
            fine->setBaseSensitivity (250);
    };

    // GAIN uses dB range
    gainSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gainSlider.setBaseSensitivity (250);
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

    // ----------------------
    // LABELS
    // ----------------------
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
    setupLabel (modeLabel, "CLIPPER"); // will switch to LIMITER in runtime

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

    // ----------------------
    // LOOK DROPDOWN (top-left)
    // ----------------------
    lookBox.addItem ("LOOK : COOKED", 1);
    lookBox.addItem ("LOOK : LUFS",   2);
    lookBox.addItem ("LOOK : STATIC", 3);
    // Keep placeholder empty so the closed box only shows the arrow area
    lookBox.setTextWhenNothingSelected ({});
    lookBox.setJustificationType (juce::Justification::centred);
    lookBox.setColour (juce::ComboBox::textColourId,        juce::Colours::white);
    lookBox.setColour (juce::ComboBox::outlineColourId,     juce::Colours::transparentBlack);
    lookBox.setColour (juce::ComboBox::backgroundColourId,  juce::Colours::black);
    lookBox.setColour (juce::ComboBox::arrowColourId,       juce::Colours::white);

    addAndMakeVisible (lookBox);

    // ----------------------
    // OVERSAMPLE DROPDOWN (top-right, tiny, white "x1" etc.)
    // ----------------------
    oversampleBox.addItem ("x1",  1);
    oversampleBox.addItem ("x2",  2);
    oversampleBox.addItem ("x4",  3);
    oversampleBox.addItem ("x8",  4);
    oversampleBox.addItem ("x16", 5);
    oversampleBox.setSelectedId (1, juce::dontSendNotification); // default x1
    oversampleBox.setTextWhenNothingSelected ("x1");

    oversampleBox.setJustificationType (juce::Justification::centred);
    oversampleBox.setColour (juce::ComboBox::textColourId,        juce::Colours::white);
    oversampleBox.setColour (juce::ComboBox::outlineColourId,     juce::Colours::transparentBlack);
    oversampleBox.setColour (juce::ComboBox::backgroundColourId,  juce::Colours::black);
    oversampleBox.setColour (juce::ComboBox::arrowColourId,       juce::Colours::white);

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
                        apvts, "satAmount",  satSlider);

    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "useLimiter", modeSlider);

    oversampleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                        apvts, "oversampleMode", oversampleBox);

    lookAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                        apvts, "lookMode", lookBox);

    oversampleBox.onChange = [this]
    {
        const int selectedIndex = oversampleBox.getSelectedItemIndex();
        processor.setStoredOversampleMode (selectedIndex);
    };

    lookBox.onChange = [this]
    {
        const int selectedIndex = lookBox.getSelectedItemIndex();
        processor.setStoredLookMode (selectedIndex);
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
    const float burnShaped = std::pow (burnRaw, 2.0f);

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

    const int lookW = juce::jmax (80, w / 6);
    const int lookH = juce::jmax (16, h / 20);
    const int lookX = 6;
    const int lookY = 6;

    lookBox.setBounds (lookX, lookY, lookW, lookH);

    // OVERSAMPLE BOX – tiny top-right
    const int osW = juce::jmax (60, w / 10);
    const int osH = juce::jmax (16, h / 20);
    const int osX = w - osW - 6;
    const int osY = 6;

    oversampleBox.setBounds (osX, osY, osW, osH);

    // 4 knobs in a row (GAIN, OTT, SAT, MODE)
    const int knobSize = juce::jmin (w / 7, h / 3);
    const int spacing  = knobSize / 2;

    const int totalW   = knobSize * 4 + spacing * 3;
    const int startX   = (w - totalW) / 2;

    const int bottomMargin = (int) (h * 0.05f);
    const int knobY        = h - knobSize - bottomMargin;

    gainSlider.setBounds (startX + 0 * (knobSize + spacing), knobY, knobSize, knobSize);
    ottSlider .setBounds (startX + 1 * (knobSize + spacing), knobY, knobSize, knobSize);
    satSlider .setBounds (startX + 2 * (knobSize + spacing), knobY, knobSize, knobSize);
    modeSlider.setBounds (startX + 3 * (knobSize + spacing), knobY, knobSize, knobSize);

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
    const bool bypassNow = processor.getGainBypass();
    const int  lookMode  = processor.getLookMode(); // 0=COOKED, 1=LUFS, 2=STATIC

    const float lufs      = processor.getGuiLufs();
    const bool  hasSignal = processor.getGuiHasSignal();

    // LUFS label visibility is driven only by signal presence,
    // regardless of bypass state
    if (! hasSignal)
    {
        lufsLabel.setVisible (false);
    }
    else
    {
        lufsLabel.setVisible (true);
        juce::String text = juce::String (lufs, 2) + " LUFS";
        lufsLabel.setText (text, juce::dontSendNotification);
    }

    if (bypassNow)
    {
        // In bypass mode, keep background fully clean (no burn)
        lastBurn = 0.0f;
    }
    else
    {
        // Normal reactive background depending on LOOK mode
        switch (lookMode)
        {
            case 1: // LUFS
                lastBurn = processor.getGuiBurnLufs();
                break;

            case 2: // STATIC
                lastBurn = 0.0f; // no burn, static background
                break;

            case 0: // COOKED
            default:
                lastBurn = processor.getGuiBurn();
                break;
        }
    }

    repaint();
}

void FruityClipAudioProcessorEditor::mouseUp (const juce::MouseEvent& e)
{
    // Only react if the GAIN label was clicked
    if (e.eventComponent == &gainLabel || e.originalComponent == &gainLabel)
    {
        isGainBypass = ! isGainBypass;
        processor.setGainBypass (isGainBypass);

        // Visual cue on the label itself
        gainLabel.setColour (juce::Label::textColourId,
                             isGainBypass ? juce::Colours::grey : juce::Colours::white);
    }
}

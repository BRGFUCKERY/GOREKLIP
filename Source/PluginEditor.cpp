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
        juce::LookAndFeel_V4::drawRotarySlider (g, x, y, width, height,
                                                sliderPosProportional,
                                                rotaryStartAngle,
                                                rotaryEndAngle,
                                                slider);
        return;
    }

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
        // Default: linear mapping for other knobs (if any)
        angle = minAngle + (maxAngle - minAngle) * sliderPosProportional;
    }

    const float centreX = (float) x + (float) width  * 0.5f;
    const float centreY = (float) y + (float) height * 0.5f;
    const float radius  = (float) juce::jmin (width, height) * 0.5f;

    juce::Rectangle<float> bounds ((float) x, (float) y, (float) width, (float) height);
    auto knobArea = bounds.withSizeKeepingCentre (radius * 2.0f, radius * 2.0f);

    // Draw knob image rotated
    g.saveState();

    g.addTransform (juce::AffineTransform::rotation (angle,
                                                     centreX,
                                                     centreY));

    g.drawImage (knobImage,
                 (int) knobArea.getX(),
                 (int) knobArea.getY(),
                 (int) knobArea.getWidth(),
                 (int) knobArea.getHeight(),
                 0, 0,
                 knobImage.getWidth(),
                 knobImage.getHeight());

    g.restoreState();
}

//==============================================================
// Editor
//==============================================================
FruityClipAudioProcessorEditor::FruityClipAudioProcessorEditor (FruityClipAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      grainWindow (2048),
      lufsMeter (grainWindow, processor.getSampleRate(), processor.getBlockSize())
{
    setSize (700, 400);

    // Load BG and SLAM
    bgImage = juce::ImageCache::getFromMemory (BinaryData::bg_png,
                                               BinaryData::bg_pngSize);

    slamImage = juce::ImageCache::getFromMemory (BinaryData::bg_clipped_png,
                                                 BinaryData::bg_clipped_pngSize);

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
    ottSlider.setLookAndFeel (&middleFingerLnf);
    satSlider.setLookAndFeel (&middleFingerLnf);
    modeSlider.setLookAndFeel (&middleFingerLnf);

    // Tell LNF which slider is which
    middleFingerLnf.setModeSlider (&modeSlider);
    middleFingerLnf.setGainSlider (&gainSlider);

    // GAIN slider (left finger)
    gainSlider.setRange (-12.0, 12.0, 0.01);
    gainSlider.setValue (0.0);
    gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gainSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);

    // OTT (LOVE) – mix slider
    ottSlider.setRange (0.0, 100.0, 0.01);
    ottSlider.setValue (0.0);
    ottSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    ottSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);

    // SAT (DEATH) – mix slider
    satSlider.setRange (0.0, 100.0, 0.01);
    satSlider.setValue (0.0);
    satSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    satSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);

    // MODE slider: 2-position, CLIPPER (0) / LIMITER (1)
    modeSlider.setRange (0.0, 1.0, 1.0);
    modeSlider.setValue (0.0);
    modeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    modeSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);

    addAndMakeVisible (gainSlider);
    addAndMakeVisible (ottSlider);
    addAndMakeVisible (satSlider);
    addAndMakeVisible (modeSlider);

    // Attach sliders to parameters
    gainAttachment.reset (new juce::AudioProcessorValueTreeState::SliderAttachment (
        processor.getAPVTS(), "GAIN", gainSlider));

    ottAttachment.reset (new juce::AudioProcessorValueTreeState::SliderAttachment (
        processor.getAPVTS(), "LOVE", ottSlider));

    satAttachment.reset (new juce::AudioProcessorValueTreeState::SliderAttachment (
        processor.getAPVTS(), "DEATH", satSlider));

    modeAttachment.reset (new juce::AudioProcessorValueTreeState::SliderAttachment (
        processor.getAPVTS(), "MODE", modeSlider));

    // Label setup function
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
        juce::FontOptions opts (14.0f);
        opts = opts.withStyle ("Bold");
        lufsLabel.setFont (juce::Font (opts));
    }

    lufsLabel.setText ("0.00 LUFS", juce::dontSendNotification);
    addAndMakeVisible (lufsLabel);

    // ----------------------
    // LOOK / SETTINGS (top-left, opens popup menu)
    // ----------------------
    lookBox.setTextWhenNothingSelected ("SETTINGS");
    lookBox.setJustificationType (juce::Justification::centred);
    lookBox.setColour (juce::ComboBox::textColourId,        juce::Colours::transparentWhite);
    lookBox.setColour (juce::ComboBox::outlineColourId,     juce::Colours::transparentBlack);
    lookBox.setColour (juce::ComboBox::backgroundColourId,  juce::Colours::transparentBlack);
    lookBox.setColour (juce::ComboBox::arrowColourId,       juce::Colours::white);
    lookBox.setInterceptsMouseClicks (false, false);

    lookBox.setLookAndFeel (&comboLnf);
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
    oversampleBox.setColour (juce::ComboBox::backgroundColourId,  juce::Colours::transparentBlack);
    oversampleBox.setColour (juce::ComboBox::arrowColourId,       juce::Colours::white);

    oversampleBox.onChange = [this]
    {
        const int id = oversampleBox.getSelectedId();
        int factor = 1;

        switch (id)
        {
            case 2: factor = 2;  break;
            case 3: factor = 4;  break;
            case 4: factor = 8;  break;
            case 5: factor = 16; break;
            case 1:
            default: factor = 1; break;
        }

        processor.setOversamplingFactor (factor);
    };

    addAndMakeVisible (oversampleBox);

    startTimerHz (30);
}

FruityClipAudioProcessorEditor::~FruityClipAudioProcessorEditor()
{
    gainSlider.setLookAndFeel (nullptr);
    ottSlider.setLookAndFeel (nullptr);
    satSlider.setLookAndFeel (nullptr);
    modeSlider.setLookAndFeel (nullptr);

    lookBox.setLookAndFeel (nullptr);
}

void FruityClipAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const int w = getWidth();
    const int h = getHeight();

    // Query "burn" level
    const float burnValue = lastBurn; // 0..1
    const float burnRaw = juce::jlimit (0.0f, 1.0f, burnValue);

    // Slightly reshape so it has more resolution at lower values
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
        const int srcY = (int) (logoImage.getHeight() * 0.20f);
        const int srcH = logoImage.getHeight() - srcY;

        // Mix between original logo and white logo based on slam
        const float logoWhiteAmount = burnShaped;

        if (logoWhiteImage.isValid())
        {
            juce::Graphics::ScopedSaveState save (g);

            g.setOpacity (1.0f);
            g.drawImage (logoImage,
                         x, y, drawW, drawH,
                         0, srcY, logoImage.getWidth(), srcH);

            if (logoWhiteAmount > 0.01f)
            {
                g.setOpacity (logoWhiteAmount);
                g.drawImage (logoWhiteImage,
                             x, y, drawW, drawH,
                             0, srcY, logoWhiteImage.getWidth(), srcH);
            }
        }
        else
        {
            g.setOpacity (1.0f);
            g.drawImage (logoImage,
                         x, y, drawW, drawH,
                         0, srcY, logoImage.getWidth(), srcH);
        }
    }

    //==========================================================
    // Finger knobs + labels
    //==========================================================
    const int knobsTop = h / 3;
    const int knobsH   = h / 3;
    const int knobsW   = w / 4;

    gainSlider.setBounds (0 * knobsW, knobsTop, knobsW, knobsH);
    ottSlider.setBounds  (1 * knobsW, knobsTop, knobsW, knobsH);
    satSlider.setBounds  (2 * knobsW, knobsTop, knobsW, knobsH);
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

    // LUFS label – directly above the CLIPPER/LIMITER finger
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

    // OVERSAMPLE BOX – tiny top-right
    const int osW = juce::jmax (60, w / 10);
    const int osH = juce::jmax (16, h / 20);
    const int osX = w - osW - 6;
    const int osY = 0;

    oversampleBox.setBounds (osX, osY, osW, osH);
}

//==============================================================
// TIMER – pull burn + LUFS value from processor
//==============================================================
void FruityClipAudioProcessorEditor::timerCallback()
{
    const bool bypassNow = processor.getGainBypass();
    const LookMode lookMode = getLookMode(); // 0=COOKED, 1=LUFS, 2=STATIC
    currentLookMode = lookMode;

    if (bypassNow)
    {
        // In bypass mode, keep background static and hide LUFS
        lastBurn = 0.0f;
        lufsLabel.setVisible (false);
    }
    else
    {
        // Normal reactive background and LUFS display
        switch (lookMode)
        {
            case LookMode::Lufs:
                lastBurn = processor.getGuiBurnLufs();
                break;

            case LookMode::Static:
                lastBurn = 0.0f; // no burn, static background
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
// LOOK mode helper
//==============================================================
LookMode FruityClipAudioProcessorEditor::getLookMode() const
{
    // If the user changed from SETTINGS, that is stored in processor
    const int storedMode = processor.getStoredLookMode();

    switch (storedMode)
    {
        case 0: return LookMode::Cooked;
        case 1: return LookMode::Lufs;
        case 2: return LookMode::Static;
        default: return LookMode::Cooked;
    }
}

//==============================================================
// SETTINGS popup (top-left)
//==============================================================
void FruityClipAudioProcessorEditor::showSettingsMenu()
{
    juce::PopupMenu menu;

    const LookMode mode = getLookMode();

    constexpr int idLookCooked = 1;
    constexpr int idLookLufs   = 2;
    constexpr int idLookStatic = 3;
    constexpr int idKlipBible  = 4;

    // LOOK modes – mutually exclusive, ticked based on current mode
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

    // Separator line
    menu.addSeparator();

    // KLIPBIBLE – clickable, NEVER checkable (no tick flag)
    menu.addItem (idKlipBible,
                  "KLIPBIBLE",
                  true); // enabled, but not a toggle

    // Handle selection
    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [this] (int result)
                        {
                            switch (result)
                            {
                                case idLookCooked:
                                    processor.setStoredLookMode (0);
                                    break;

                                case idLookLufs:
                                    processor.setStoredLookMode (1);
                                    break;

                                case idLookStatic:
                                    processor.setStoredLookMode (2);
                                    break;

                                case idKlipBible:
                                    showBypassInfoPopup();
                                    break;

                                default:
                                    break;
                            }
                        });
}

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
    // Always work in editor coordinates so we don't accidentally
    // trigger the menu from child components (e.g. GAIN label/logo)
    auto posInEditor = e.getEventRelativeTo (this).getPosition();

    if (lookBox.getBounds().contains (posInEditor))
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

        // Visual cue on the label itself
        gainLabel.setColour (juce::Label::textColourId,
                             isGainBypass ? juce::Colours::grey : juce::Colours::white);
    }
}

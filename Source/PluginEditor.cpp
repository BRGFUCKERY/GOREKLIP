#include "BinaryData.h"
#include "PluginEditor.h"
#include "CustomLookAndFeel.h"

#include <cmath>
#include <cstdint>

namespace
{
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

class OversampleSettingsComponent : public juce::Component
{
public:
    OversampleSettingsComponent (FruityClipAudioProcessor& proc,
                                 juce::AudioProcessorValueTreeState& vts)
        : processor (proc), parameters (vts)
    {
        setOpaque (true);

        // Title
        titleLabel.setText ("OVERSAMPLING", juce::dontSendNotification);
        titleLabel.setJustificationType (juce::Justification::centred);
        titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        titleLabel.setFont (juce::Font (18.0f, juce::Font::bold)); // simple system bold font
        addAndMakeVisible (titleLabel);

        // Column headers
        liveLabel.setText ("LIVE", juce::dontSendNotification);
        liveLabel.setJustificationType (juce::Justification::centred);
        liveLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (liveLabel);

        offlineLabel.setText ("OFFLINE", juce::dontSendNotification);
        offlineLabel.setJustificationType (juce::Justification::centred);
        offlineLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (offlineLabel);

        // Oversample modes for LIVE: 0=x1, 1=x2, 2=x4, 3=x8, 4=x16, 5=x32, 6=x64
        juce::StringArray modes { "x1", "x2", "x4", "x8", "x16", "x32", "x64" };

        for (int i = 0; i < modes.size(); ++i)
        {
            const int id = i + 1;
            liveCombo.addItem (modes[i], id);
        }

        // OFFLINE combo: first item is "SAME", then explicit x1..x64
        offlineCombo.addItem ("SAME", 1); // id=1 => follow LIVE

        for (int i = 0; i < modes.size(); ++i)
        {
            const int id = i + 2; // shift by 2 because id=1 is SAME
            offlineCombo.addItem (modes[i], id);
        }

        // Combo appearance – black background, white text
        auto setupCombo = [] (juce::ComboBox& c)
        {
            c.setColour (juce::ComboBox::backgroundColourId, juce::Colours::black);
            c.setColour (juce::ComboBox::textColourId,       juce::Colours::white);
            c.setColour (juce::ComboBox::outlineColourId,    juce::Colours::white.withAlpha (0.2f));
        };

        setupCombo (liveCombo);
        setupCombo (offlineCombo);

        addAndMakeVisible (liveCombo);
        addAndMakeVisible (offlineCombo);

        // --- Initial LIVE value from parameter / stored default ---
        {
            // Start from the stored global default (what we save in userSettings)
            int initialLiveIndex = processor.getStoredLiveOversampleIndex(); // 0..6

            // If the oversampleMode parameter already has a value (e.g. restored by the host),
            // let that win so the GUI matches the actual processing.
            if (auto* osParam = parameters.getRawParameterValue ("oversampleMode"))
                initialLiveIndex = juce::jlimit (0, 6, (int) osParam->load());

            // Combo item IDs are 1..7 ==> index 0..6
            liveCombo.setSelectedId (juce::jlimit (0, 6, initialLiveIndex) + 1,
                                     juce::dontSendNotification);
        }

        // LIVE column is bound directly to "oversampleMode" parameter (0..6)
        liveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            parameters, "oversampleMode", liveCombo);

        // When LIVE combo changes here, also update the stored global default
        liveCombo.onChange = [this]
        {
            const int selectedId = liveCombo.getSelectedId(); // 1..7
            if (selectedId > 0)
            {
                const int idx = juce::jlimit (0, 6, selectedId - 1); // 0..6
                processor.setStoredLiveOversampleIndex (idx);
            }
        };

        // OFFLINE column stored in userSettings
        {
            const int offlineIndex = processor.getStoredOfflineOversampleIndex(); // -1..6

            if (offlineIndex < 0)
            {
                // -1 => SAME
                offlineCombo.setSelectedId (1, juce::dontSendNotification); // "SAME"
            }
            else
            {
                // 0..6 map to ids 2..8
                offlineCombo.setSelectedId (offlineIndex + 2, juce::dontSendNotification);
            }

            offlineCombo.onChange = [this]
            {
                const int selectedId = offlineCombo.getSelectedId();

                if (selectedId <= 1)
                {
                    // "SAME"
                    processor.setStoredOfflineOversampleIndex (-1);
                }
                else
                {
                    // explicit oversample index: id 2..8 => 0..6
                    const int idx = juce::jlimit (0, 6, selectedId - 2);
                    processor.setStoredOfflineOversampleIndex (idx);
                }
            };
        }

        // Info label: no longer showing CPU warning text, keep it simple and ASCII-safe
        infoLabel.setText ({}, juce::dontSendNotification);
        infoLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        infoLabel.setJustificationType (juce::Justification::topLeft);
        infoLabel.setMinimumHorizontalScale (0.8f);
        infoLabel.setFont (juce::Font (13.0f)); // standard font so ":" etc. render correctly

        addAndMakeVisible (infoLabel);

        // Make sure popup menus for these combos are also black/white
        if (auto* lf = dynamic_cast<juce::LookAndFeel_V4*> (&getLookAndFeel()))
        {
            lf->setColour (juce::PopupMenu::backgroundColourId, juce::Colours::black);
            lf->setColour (juce::PopupMenu::textColourId,       juce::Colours::white);
            lf->setColour (juce::PopupMenu::highlightedBackgroundColourId,
                           juce::Colours::white.withAlpha (0.15f));
            lf->setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);

        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);

        auto titleArea = r.removeFromTop (28);
        titleLabel.setBounds (titleArea);

        r.removeFromTop (8);

        // Header row: LIVE | OFFLINE
        auto headerRow = r.removeFromTop (20);
        auto halfWidth = headerRow.getWidth() / 2;

        auto liveHeader    = headerRow.removeFromLeft (halfWidth);
        auto offlineHeader = headerRow;

        liveLabel.setBounds    (liveHeader);
        offlineLabel.setBounds (offlineHeader);

        r.removeFromTop (6);

        // Combos row
        auto comboRow = r.removeFromTop (26);
        auto liveArea = comboRow.removeFromLeft (halfWidth).reduced (0, 2);
        auto offArea  = comboRow.reduced (0, 2);

        liveCombo.setBounds    (liveArea);
        offlineCombo.setBounds (offArea);

        r.removeFromTop (10);

        // Info label takes the remaining area
        infoLabel.setBounds (r);
    }

    // Force the LIVE combo to a specific oversample index (0..6 = x1..x64).
    // This does NOT notify the processor – it's just a visual sync helper
    // so the OVERSAMPLE window mirrors the current LIVE dropdown.
    void syncLiveFromIndex (int index)
    {
        const int clampedIndex = juce::jlimit (0, 6, index);
        const int comboId      = clampedIndex + 1; // 0..6 => 1..7

        liveCombo.setSelectedId (comboId, juce::dontSendNotification);
    }

private:
    FruityClipAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& parameters;

    juce::Label     titleLabel;
    juce::Label     liveLabel;
    juce::Label     offlineLabel;
    juce::ComboBox  liveCombo;
    juce::ComboBox  offlineCombo;

    juce::Label infoLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> liveAttachment;
};
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

    // Default angle mapping for "normal" knobs: ~7 o'clock to ~5 o'clock
    const float minAngle = juce::degreesToRadians (-135.0f);
    const float maxAngle = juce::degreesToRadians ( 135.0f);

    float angle = 0.0f;

    if (modeSlider != nullptr && &slider == modeSlider)
    {
        // MODE FINGER: hard 2-position switch
        // slider value: 0.0 = CLIPPER (up, 12 o'clock)
        //               1.0 = LIMITER (down, 6 o'clock)
        const bool useLimiter = (slider.getValue() >= 0.5f);

        const float angleDegrees = useLimiter ? 180.0f : 0.0f;
        angle = juce::degreesToRadians (angleDegrees);
    }
    else if (gainSlider != nullptr && &slider == gainSlider)
    {
        // GAIN FINGER: show ONLY the real gain in dB
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
// DownwardComboBoxLookAndFeel – transparent, arrow-only look
//==============================================================
void DownwardComboBoxLookAndFeel::drawComboBox (juce::Graphics& g,
                                                int width, int height,
                                                bool isButtonDown,
                                                int buttonX, int buttonY,
                                                int buttonW, int buttonH,
                                                juce::ComboBox& box)
{
    juce::ignoreUnused (isButtonDown, buttonX, buttonY, buttonW, buttonH);

    const auto name             = box.getName();
    const bool isLookBox        = (name == "lookBox");
    const bool isOversampleLive = (name == "oversampleLiveBox");

    // For everything except the pentagram boxes, just use the normal V4 drawing.
    if (! isLookBox && ! isOversampleLive)
    {
        juce::LookAndFeel_V4::drawComboBox (g,
                                            width, height,
                                            isButtonDown,
                                            buttonX, buttonY,
                                            buttonW, buttonH,
                                            box);
        return;
    }

    // --- Custom drawing for pentagram boxes (lookBox / oversampleLiveBox) ---

    auto bounds = juce::Rectangle<float> (0.0f, 0.0f,
                                          (float) width, (float) height);

    // Transparent background so the pentagram sits over the plugin art.
    g.setColour (juce::Colours::transparentBlack);
    g.fillRect (bounds);

    const float iconSize   = (float) height * 0.55f;
    const float iconRadius = iconSize * 0.5f;

    float iconCenterX = bounds.getRight() - iconSize * 0.9f; // keep existing X logic
    const float iconCenterY = bounds.getCentreY();

    if (isOversampleLive)
    {
        const float distanceFromLeft = iconCenterX - bounds.getX();
        iconCenterX = bounds.getRight() - distanceFromLeft;
    }

    juce::Rectangle<int> starBounds (
        (int) std::round (iconCenterX - iconRadius),
        (int) std::round (iconCenterY - iconRadius),
        (int) std::round (iconRadius * 2.0f),
        (int) std::round (iconRadius * 2.0f));

    const float cx     = (float) starBounds.getCentreX();
    const float cy     = (float) starBounds.getCentreY();
    const float radius = (float) starBounds.getWidth() * 0.45f;

    juce::Point<float> pts[5];

    // Screen coordinates: +Y goes down.
    // baseAngle = +pi/2 -> first point straight DOWN (one spike down),
    // which gives an inverted pentagram (two spikes up).
    const float baseAngle = juce::MathConstants<float>::halfPi;
    const float step      = juce::MathConstants<float>::twoPi / 5.0f;

    for (int i = 0; i < 5; ++i)
    {
        const float angle = baseAngle + step * (float) i;
        const float x     = cx + radius * std::cos (angle);
        const float y     = cy + radius * std::sin (angle);
        pts[i].setXY (x, y);
    }

    juce::Path pent;
    pent.startNewSubPath (pts[0]);
    pent.lineTo (pts[2]);
    pent.lineTo (pts[4]);
    pent.lineTo (pts[1]);
    pent.lineTo (pts[3]);
    pent.closeSubPath();

    if (isOversampleLive)
        pent.applyTransform (juce::AffineTransform{}.scaled (-1.0f, 1.0f, cx, cy));

    // Pentagram colour follows burnAmount: 0 = black, 1 = white
    const float burn = juce::jlimit (0.0f, 1.0f, burnAmount);

    auto starColour = juce::Colours::white
        .interpolatedWith (juce::Colours::black, 1.0f - burn)
        .withAlpha (0.8f + 0.2f * burn);

    g.setColour (starColour);

    const float strokeThickness =
        (float) starBounds.getWidth() * 0.10f;

    juce::PathStrokeType stroke (strokeThickness,
                                 juce::PathStrokeType::mitered,
                                 juce::PathStrokeType::square);
    g.strokePath (pent, stroke);

    // Text is handled by ComboBox itself. For lookBox we keep textColour transparent
    // so only the pentagram is visible.
}

juce::Font DownwardComboBoxLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return juce::LookAndFeel_V4::getComboBoxFont (box);
}

//==============================================================
// Editor
//==============================================================
FruityClipAudioProcessorEditor::FruityClipAudioProcessorEditor (FruityClipAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    auto& lookSelector = lookBox;
    auto& menuSelector = lookBox;

    setLookAndFeel (&customLookAndFeel);

    // Basic combo colours for the left SETTINGS box
    lookSelector.setColour (juce::ComboBox::backgroundColourId, juce::Colours::black);
    lookSelector.setColour (juce::ComboBox::textColourId,       juce::Colours::transparentWhite);

    menuSelector.setColour (juce::ComboBox::backgroundColourId, juce::Colours::black);
    menuSelector.setColour (juce::ComboBox::textColourId,       juce::Colours::transparentWhite);

    // --------------------------------------------------
    // BACKGROUND + LOGO
    // --------------------------------------------------
    auto makeInvertedCopy = [] (const juce::Image& img)
    {
        if (! img.isValid())
            return juce::Image {};

        auto copy = img.createCopy();
        juce::Image::BitmapData data (copy, juce::Image::BitmapData::readWrite);

        for (int y = 0; y < data.height; ++y)
        {
            for (int x = 0; x < data.width; ++x)
            {
                auto c = data.getPixelColour (x, y);
                auto a = c.getAlpha();
                data.setPixelColour (x, y,
                                     juce::Colour::fromRGBA (255 - c.getRed(),
                                                             255 - c.getGreen(),
                                                             255 - c.getBlue(),
                                                             a));
            }
        }

        return copy;
    };

    bgImage = juce::ImageCache::getFromMemory (BinaryData::bg_png,
                                               BinaryData::bg_pngSize);

    bgImageInverted = makeInvertedCopy (bgImage);

    slamImage = juce::ImageCache::getFromMemory (BinaryData::slam_jpg,
                                                 BinaryData::slam_jpgSize);

    slamImageInverted = makeInvertedCopy (slamImage);

    logoImage = juce::ImageCache::getFromMemory (BinaryData::gorekliper_logo_png,
                                                 BinaryData::gorekliper_logo_pngSize);

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
                    data.setPixelColour (x, y, juce::Colour::fromRGBA (255, 255, 255, a));
            }
        }
    }

    juce::Image fingerImage = juce::ImageCache::getFromMemory (BinaryData::finger_png,
                                                               BinaryData::finger_pngSize);
    fingerLnf.setKnobImage (fingerImage);

    if (bgImage.isValid())
        setSize ((int) (bgImage.getWidth()  * bgScale),
                 (int) (bgImage.getHeight() * bgScale));
    else
        setSize (600, 400);

    // --------------------------------------------------
    // SLIDERS
    // --------------------------------------------------
    auto setupKnob01 = [] (FineControlSlider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (0.0, 1.0, 0.0001);
        s.setMouseDragSensitivity (250);
        s.setDragSensitivities (250, 800);
    };

    // GAIN uses a dB range
    gainSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gainSlider.setMouseDragSensitivity (250);
    gainSlider.setDragSensitivities (250, 800);
    gainSlider.setRange (-12.0, 12.0, 0.01);

    setupKnob01 (fuckSlider);
    setupKnob01 (silkSlider);
    setupKnob01 (satSlider);
    setupKnob01 (modeSlider);

    // MODE is a hard 0/1 switch
    modeSlider.setRange (0.0, 1.0, 1.0); // ONLY 0 or 1

    gainSlider.setLookAndFeel (&fingerLnf);
    fuckSlider.setLookAndFeel (&fingerLnf);
    silkSlider.setLookAndFeel (&fingerLnf);
    satSlider .setLookAndFeel (&fingerLnf);
    modeSlider.setLookAndFeel (&fingerLnf);

    addAndMakeVisible (gainSlider);
    addAndMakeVisible (fuckSlider);
    addAndMakeVisible (silkSlider);
    addAndMakeVisible (satSlider);
    addAndMakeVisible (modeSlider);

    // --------------------------------------------------
    // LABELS
    // --------------------------------------------------
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
    setupLabel (fuckLabel, "FU#K");
    setupLabel (silkLabel, "MARRY");
    setupLabel (satLabel,  "K#LL");
    setupLabel (modeLabel, getClipperLabelText()); // will flip to LIMITER / 50-69 in runtime

    addAndMakeVisible (gainLabel);
    addAndMakeVisible (fuckLabel);
    addAndMakeVisible (silkLabel);
    addAndMakeVisible (satLabel);
    addAndMakeVisible (modeLabel);

    // GAIN label click = bypass after gain
    gainLabel.setInterceptsMouseClicks (true, false);
    gainLabel.addMouseListener (this, false);

    // LUFS label
    lufsLabel.setJustificationType (juce::Justification::centred);
    lufsLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    {
        juce::FontOptions opts (15.4f);
        opts = opts.withStyle ("Bold");
        lufsLabel.setFont (juce::Font (opts));
    }
    lufsLabel.setText ("0.00 LUFS", juce::dontSendNotification);
    addAndMakeVisible (lufsLabel);

    auto setupValueLabel = [] (juce::Label& lbl)
    {
        lbl.setJustificationType (juce::Justification::centred);
        lbl.setColour (juce::Label::textColourId, juce::Colours::white);
        lbl.setInterceptsMouseClicks (false, false);

        juce::FontOptions opts (14.0f);
        opts = opts.withStyle ("Bold");
        lbl.setFont (juce::Font (opts));
    };

    setupValueLabel (gainValueLabel);
    setupValueLabel (fuckValueLabel);
    setupValueLabel (silkValueLabel);
    setupValueLabel (satValueLabel);

    addAndMakeVisible (gainValueLabel);
    addAndMakeVisible (fuckValueLabel);
    addAndMakeVisible (silkValueLabel);
    addAndMakeVisible (satValueLabel);

    gainValueLabel.setVisible (false);
    fuckValueLabel.setVisible (false);
    silkValueLabel.setVisible (false);
    satValueLabel.setVisible (false);

    // SETTINGS (left pentagram)
    lookBox.setName ("lookBox");
    lookBox.setJustificationType (juce::Justification::centred);
    lookBox.setTextWhenNothingSelected ("SETTINGS");
    lookBox.setColour (juce::ComboBox::textColourId,       juce::Colours::transparentWhite);
    lookBox.setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    lookBox.setColour (juce::ComboBox::outlineColourId,    juce::Colours::transparentBlack);
    lookBox.setColour (juce::ComboBox::arrowColourId,      juce::Colours::white);
    lookBox.setLookAndFeel (&comboLnf);
    lookBox.setInterceptsMouseClicks (false, false); // editor handles clicks
    addAndMakeVisible (lookBox);

    // LIVE OVERSAMPLE dropdown (top-right, x1/x2/x4/x8/x16/x32/x64)
    oversampleLiveBox.setName ("oversampleLiveBox");
    oversampleLiveBox.setJustificationType (juce::Justification::centred);
    oversampleLiveBox.setTextWhenNothingSelected ("");

    oversampleLiveBox.setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    oversampleLiveBox.setColour (juce::ComboBox::outlineColourId,    juce::Colours::transparentBlack);
    oversampleLiveBox.setColour (juce::ComboBox::textColourId,       juce::Colours::transparentWhite);
    oversampleLiveBox.setColour (juce::ComboBox::arrowColourId,      juce::Colours::transparentWhite);
    oversampleLiveBox.setLookAndFeel (&comboLnf);
    oversampleLiveBox.setInterceptsMouseClicks (false, false);

    {
        juce::StringArray modes { "x1", "x2", "x4", "x8", "x16", "x32", "x64" };
        for (int i = 0; i < modes.size(); ++i)
        {
            const int id = i + 1; // 0..6 -> ids 1..7
            oversampleLiveBox.addItem (modes[i], id);
        }
    }

    addAndMakeVisible (oversampleLiveBox);

    // --------------------------------------------------
    // PARAMETER ATTACHMENTS
    // --------------------------------------------------
    auto& apvts = processor.getParametersState();

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "inputGain", gainSlider);

    fuckAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "ottAmount", fuckSlider);

    silkAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "silkAmount", silkSlider);

    satAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "satAmount", satSlider);

    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "useLimiter", modeSlider);

    oversampleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, "oversampleMode", oversampleLiveBox);

    auto setupValuePopup = [this] (FineControlSlider& slider,
                                   juce::Label& lbl,
                                   std::function<juce::String()> makeText)
    {
        slider.onDragStart = [this, &lbl, makeText]()
        {
            lbl.setVisible (true);
            lbl.setText (makeText(), juce::dontSendNotification);
        };

        slider.onDragEnd = [this, &lbl]()
        {
            lbl.setVisible (false);
        };

        slider.onValueChange = [this, &lbl, makeText]()
        {
            if (lbl.isVisible())
                lbl.setText (makeText(), juce::dontSendNotification);
        };
    };

    setupValuePopup (gainSlider, gainValueLabel, [this]()
    {
        const double valDb = gainSlider.getValue();
        juce::String s = (valDb >= 0.0 ? "+" : "") + juce::String (valDb, 1) + " dB";
        return s;
    });

    setupValuePopup (fuckSlider, fuckValueLabel, [this]()
    {
        const double raw = fuckSlider.getValue();
        const int percent = (int) std::round (raw * 100.0);
        return juce::String (percent) + " %";
    });

    setupValuePopup (silkSlider, silkValueLabel, [this]()
    {
        const double raw = silkSlider.getValue();
        const int percent = (int) std::round (raw * 100.0);
        return juce::String (percent) + " %";
    });

    setupValuePopup (satSlider, satValueLabel, [this]()
    {
        const double raw = satSlider.getValue();
        const int percent = (int) std::round (raw * 100.0);
        return juce::String (percent) + " %";
    });

    // Small helper to keep SAT enable + label in sync with the mode value
    auto updateModeUI = [this]()
    {
        const bool useLimiter = (modeSlider.getValue() >= 0.5f);
        satSlider.setEnabled (! useLimiter);
        modeLabel.setText (getClipperLabelText(), juce::dontSendNotification);
    };

    // Initial state from parameter
    if (auto* modeParam = apvts.getRawParameterValue ("useLimiter"))
    {
        const bool useLimiter = (modeParam->load() >= 0.5f);
        modeSlider.setValue (useLimiter ? 1.0f : 0.0f, juce::dontSendNotification);
        updateModeUI();
    }

    // Whenever the slider changes (attachment or user), update UI only.
    // DO NOT call setValueNotifyingHost here – the attachment already handles the parameter.
    modeSlider.onValueChange = [this, updateModeUI]()
    {
        updateModeUI();
    };

    // Click on the finger = toggle 0 <-> 1 and notify attachment/host
    modeSlider.onClick = [this]()
    {
        const float newVal = (modeSlider.getValue() >= 0.5f ? 0.0f : 1.0f);
        modeSlider.setValue (newVal, juce::sendNotificationSync);
    };

    // Tell the finger LNF which sliders are special
    fingerLnf.setControlledSliders (&gainSlider, &modeSlider, &satSlider);

    currentLookMode = getLookMode();

    // Burn / LUFS update timer
    startTimerHz (30);
}

void FruityClipAudioProcessorEditor::startFingerAnimation (bool limiterMode)
{
    targetFingerAngle = limiterMode ? juce::MathConstants<float>::pi : 0.0f;

    const int fps = 60;
    const float step = (targetFingerAngle - currentFingerAngle) / (fingerAnimSpeed * fps);

    animationTimer.stopTimer();
    animationTimer.startTimerHz (fps);

    animationTimer.onTimer = [this, step]()
    {
        currentFingerAngle += step;

        if ((step > 0.0f && currentFingerAngle >= targetFingerAngle)
         || (step < 0.0f && currentFingerAngle <= targetFingerAngle))
        {
            currentFingerAngle = targetFingerAngle;
            animationTimer.stopTimer();
        }
        repaint();
    };
}

FruityClipAudioProcessorEditor::~FruityClipAudioProcessorEditor()
{
    stopTimer();
    gainSlider.setLookAndFeel (nullptr);
    fuckSlider.setLookAndFeel (nullptr);
    silkSlider.setLookAndFeel (nullptr);
    satSlider .setLookAndFeel (nullptr);
    modeSlider.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

//==============================================================
// PAINT
//==============================================================
void FruityClipAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();

    const bool isAnalogMode = processor.getClipMode() == FruityClipAudioProcessor::ClipMode::Analog;

    // Map burn into 0..1
    const float burnRaw = juce::jlimit (0.0f, 1.0f, lastBurn);

    // Visual slam comes in later – you really have to hit it
    const float burnShaped = std::pow (burnRaw, 1.3f);

    // 1) Base background
    const auto& bgToUse = (isAnalogMode && bgImageInverted.isValid()) ? bgImageInverted : bgImage;

    if (bgToUse.isValid())
        g.drawImageWithin (bgToUse, 0, 0, w, h, juce::RectanglePlacement::stretchToFit);
    else
        g.fillAll (juce::Colours::black);

    // 2) Slam background
    const auto& slamToUse = (isAnalogMode && slamImageInverted.isValid()) ? slamImageInverted : slamImage;

    if (slamToUse.isValid() && burnShaped > 0.02f)
    {
        juce::Graphics::ScopedSaveState save (g);

        g.setOpacity (burnShaped);
        g.drawImageWithin (slamToUse, 0, 0, w, h, juce::RectanglePlacement::stretchToFit);
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

    // --- Top bar: left SETTINGS pentagram + right LIVE oversample dropdown ---
    const int topMargin = 6;

    // Height of the top bar (and of both combo boxes)
    const int barH = juce::jmax (16, h / 20);

    // Make both boxes perfectly square
    const int boxSize = barH;

    // Base rectangle for the left box (SETTINGS pentagram)
    juce::Rectangle<int> leftBox (topMargin,
                                  topMargin,
                                  boxSize,
                                  boxSize);

    // Mirror that rectangle horizontally for the right box
    juce::Rectangle<int> rightBox (w - topMargin - boxSize,
                                   topMargin,
                                   boxSize,
                                   boxSize);

    // Apply bounds to the actual ComboBoxes
    lookBox.setBounds (leftBox);
    oversampleLiveBox.setBounds (rightBox);

    // --------------------------------------------------
    // Existing layout for knobs, labels, LUFS label etc.
    // --------------------------------------------------
    const int knobSize = juce::jmin (w / 7, h / 3);
    const int spacing  = knobSize / 2;

    const int totalW   = knobSize * 5 + spacing * 4;
    const int startX   = (w - totalW) / 2;

    const int bottomMargin = (int) (h * 0.05f);
    const int knobY        = h - knobSize - bottomMargin;

    gainSlider.setBounds (startX + 0 * (knobSize + spacing), knobY, knobSize, knobSize);
    fuckSlider.setBounds (startX + 1 * (knobSize + spacing), knobY, knobSize, knobSize);
    silkSlider.setBounds (startX + 2 * (knobSize + spacing), knobY, knobSize, knobSize);
    satSlider .setBounds (startX + 3 * (knobSize + spacing), knobY, knobSize, knobSize);
    modeSlider.setBounds (startX + 4 * (knobSize + spacing), knobY, knobSize, knobSize);

    const int labelH = 20;

    gainLabel.setBounds (gainSlider.getX(),
                         gainSlider.getBottom() + 2,
                         gainSlider.getWidth(),
                         labelH);

    fuckLabel.setBounds (fuckSlider.getX(),
                        fuckSlider.getBottom() + 2,
                        fuckSlider.getWidth(),
                        labelH);

    silkLabel.setBounds (silkSlider.getX(),
                         silkSlider.getBottom() + 2,
                         silkSlider.getWidth(),
                         labelH);

    satLabel.setBounds (satSlider.getX(),
                        satSlider.getBottom() + 2,
                        satSlider.getWidth(),
                        labelH);

    modeLabel.setBounds (modeSlider.getX(),
                         modeSlider.getBottom() + 2,
                         modeSlider.getWidth(),
                         labelH);

    const int valueLabelHeight = 18;
    const int valueLabelMargin = 4;

    auto makeValueBounds = [valueLabelHeight, valueLabelMargin] (const juce::Rectangle<int>& knobBounds)
    {
        return juce::Rectangle<int> (
            knobBounds.getX(),
            juce::jmax (0, knobBounds.getY() - valueLabelHeight - valueLabelMargin),
            knobBounds.getWidth(),
            valueLabelHeight);
    };

    gainValueLabel.setBounds (makeValueBounds (gainSlider.getBounds()));
    fuckValueLabel.setBounds (makeValueBounds (fuckSlider.getBounds()));
    silkValueLabel.setBounds (makeValueBounds (silkSlider.getBounds()));
    satValueLabel .setBounds (makeValueBounds (satSlider.getBounds()));

    // LUFS label sits above the MODE/clipper finger
    const int lufsHeight = 18;
    const int lufsMargin = 4;

    const auto modeBounds = modeSlider.getBounds();

    juce::Rectangle<int> lufsBounds (
        modeBounds.getX(),
        juce::jmax (0, modeBounds.getY() - lufsHeight - lufsMargin),
        modeBounds.getWidth(),
        lufsHeight);

    lufsLabel.setBounds (lufsBounds);
}

//==============================================================
// TIMER – pull burn + LUFS value from processor
//==============================================================
void FruityClipAudioProcessorEditor::timerCallback()
{
    // Always read the look mode from the processor so we stay in sync
    auto lookMode = getLookMode();
    currentLookMode = lookMode;

    // Base burn from processor (GUI burn or LUFS burn or static)
    switch (lookMode)
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

    const bool bypassNow = processor.getGainBypass();

    // If bypass is on: burn goes to 0, but LUFS still keeps moving.
    if (bypassNow)
        lastBurn = 0.0f;

    const float lufs      = processor.getGuiLufs();
    const bool  hasSignal = processor.getGuiHasSignal();

    if (! hasSignal)
    {
        lufsLabel.setVisible (false);
        lufsLabel.setText ({}, juce::dontSendNotification);
    }
    else
    {
        lufsLabel.setVisible (true);
        lufsLabel.setText (juce::String (lufs, 2) + " LUFS",
                           juce::dontSendNotification);
    }

    modeLabel.setText (getClipperLabelText(), juce::dontSendNotification);

    // Drive pentagrams / x1 colour from lastBurn (0..1)
    const float burnForIcons = juce::jlimit (0.0f, 1.0f, lastBurn);
    comboLnf.setBurnAmount (burnForIcons);

    const std::uint8_t level = (std::uint8_t) juce::jlimit (
        0, 255, (int) std::round (burnForIcons * 255.0f));
    auto burnColour = juce::Colour::fromRGB (level, level, level);

    lookBox.setColour (juce::ComboBox::arrowColourId, burnColour);

    lookBox.repaint();
    oversampleLiveBox.repaint();

    repaint();
}

void FruityClipAudioProcessorEditor::showOversampleMenu()
{
    auto& state = processor.getParametersState();

    auto content = std::make_unique<OversampleSettingsComponent> (processor, state);

    // Force the LIVE combo inside the OVERSAMPLE dialog to match the
    // actual "oversampleMode" parameter. This guarantees that:
    //   - A fresh instance uses the stored global default LIVE oversample.
    //   - The dialog always mirrors whatever the right pentagram is really doing.
    int currentIndex = 0;
    if (auto* osParam = state.getRawParameterValue ("oversampleMode"))
        currentIndex = juce::jlimit (0, 6, (int) osParam->load());

    content->syncLiveFromIndex (currentIndex);

    content->setSize (320, 120);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle              = "OVERSAMPLING";
    options.dialogBackgroundColour   = juce::Colours::black;
    options.content.setOwned         (content.release());
    options.componentToCentreAround  = this;
    options.useNativeTitleBar        = true;
    options.resizable                = false;
    options.launchAsync();
}

void FruityClipAudioProcessorEditor::showOversampleLiveMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&comboLnf);

    juce::StringArray modes { "x1", "x2", "x4", "x8", "x16", "x32", "x64" };

    int currentIndex = 0;
    if (auto* osParam = processor.getParametersState().getRawParameterValue ("oversampleMode"))
        currentIndex = juce::jlimit (0, modes.size() - 1, (int) osParam->load());

    for (int i = 0; i < modes.size(); ++i)
    {
        const int id = i + 1;
        menu.addItem (id, modes[i], true, currentIndex == i);
    }

    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [this] (int result)
                        {
                            if (result <= 0)
                                return;

                            const int index = juce::jlimit (0, 6, result - 1);
                            oversampleLiveBox.setSelectedId (index + 1, juce::sendNotificationSync);
                            processor.setStoredLiveOversampleIndex (index);
                        });
}

void FruityClipAudioProcessorEditor::showBypassInfoPopup()
{
    juce::String text;

    text << "• BYPASS\n";
    text << "Tap the GAIN label to temporarily bypass the clipping and saturation circuit.\n";
    text << "Only the input gain stays active, so your A/B comparison is at the same loudness,\n";
    text << "not just louder vs quieter.\n\n";

    text << "• Limiter Mode\n";
    text << "Flick the last finger knob (the CLIPPER finger) up and down to switch\n";
    text << "between Clipper and Limiter modes.\n\n";

    text << "• Fine-Tune Control\n";
    text << "Hold SHIFT while turning any knob for tiny mastering adjustments -\n";
    text << "normal drag = big moves, SHIFT drag = precise control.\n\n";

    text << "—\n\n";
    text << "FOLLOW ME ON INSTAGRAM\n";
    text << "@BORGORE\n";

    auto bibleComponent = std::make_unique<KlipBibleComponent> (text);
    bibleComponent->setSize (500, 340);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "KLIPERBIBLE";
    options.dialogBackgroundColour = juce::Colours::black;
    options.content.setOwned (bibleComponent.release());
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}


LookMode FruityClipAudioProcessorEditor::getLookMode() const
{
    const int modeIndex = processor.getLookModeIndex();

    switch (modeIndex)
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

juce::String FruityClipAudioProcessorEditor::getClipperLabelText() const
{
    const auto mode = processor.getClipMode();
    const bool limiterOn = processor.isLimiterEnabled();

    if (limiterOn)
        return "LIMITER";

    if (mode == FruityClipAudioProcessor::ClipMode::Analog)
        return "50 – 69";

    return "CLIPPER";
}

void FruityClipAudioProcessorEditor::showSettingsMenu()
{
    juce::PopupMenu menu;

    // Use the same LookAndFeel as the combo arrows – black popup, white text
    menu.setLookAndFeel (&comboLnf);

    // Title
    menu.addSectionHeader ("SETTINGS");

    // Current mode from processor / cached state
    const LookMode mode = getLookMode();

    // Stable IDs for items
    constexpr int idLookCooked     = 1;
    constexpr int idLookLufs       = 2;
    constexpr int idLookStatic     = 3;
    constexpr int idModeDigital    = 4;
    constexpr int idModeAnalog     = 5;
    constexpr int idOversampleMenu = 6;
    constexpr int idKlipBible      = 7;

    // LOOK modes – mutually exclusive, ticked based on current mode
    menu.addItem (idLookCooked,
                  "LOOK – COOKED",
                  true,
                  mode == LookMode::Cooked);

    menu.addItem (idLookLufs,
                  "LOOK – LUFS",
                  true,
                  mode == LookMode::Lufs);

    menu.addItem (idLookStatic,
                  "LOOK – STATIC",
                  true,
                  mode == LookMode::Static);

    // Separator between LOOK and MODE
    menu.addSeparator();

    const auto clipMode = processor.getClipMode();

    menu.addItem (idModeDigital,
                  "MODE – DIGITAL",
                  true,
                  clipMode == FruityClipAudioProcessor::ClipMode::Digital);

    menu.addItem (idModeAnalog,
                  "MODE – ANALOG",
                  true,
                  clipMode == FruityClipAudioProcessor::ClipMode::Analog);

    // Separator between MODE and OVERSAMPLE
    menu.addSeparator();

    // New OVERSAMPLE entry (opens oversample settings dialog)
    menu.addItem (idOversampleMenu,
                  "OVERSAMPLE",
                  true);

    // Separator line before KLIPERBIBLE
    menu.addSeparator();

    // KLIPERBIBLE – clickable, NEVER checkable (no tick flag)
    menu.addItem (idKlipBible,
                  "KLIPERBIBLE",
                  true); // enabled, but not a toggle

    // Handle selection...
    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [this] (int result)
                        {
                            auto* clipModeParam = dynamic_cast<juce::AudioParameterChoice*> (
                                processor.getParametersState().getParameter ("clipMode"));

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

                                case idModeDigital:
                                    if (clipModeParam != nullptr)
                                        clipModeParam->setValueNotifyingHost (0.0f);
                                    break;

                                case idModeAnalog:
                                    if (clipModeParam != nullptr)
                                        clipModeParam->setValueNotifyingHost (1.0f);
                                    break;

                                case idOversampleMenu:
                                    // Open the oversample settings window (LIVE/OFFLINE/SAME)
                                    showOversampleMenu();
                                    break;

                                case idKlipBible:
                                    openKlipBible();
                                    break;

                                default:
                                    break; // user cancelled
                            }
                        });
}

void FruityClipAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    // Convert click from child component space into editor coordinates
    auto posInEditor = e.getEventRelativeTo (this).getPosition();
    auto posInt      = posInEditor.toInt();

    if (lookBox.getBounds().contains (posInt))
    {
        showSettingsMenu();
        return;
    }

    if (oversampleLiveBox.getBounds().contains (posInt))
    {
        showOversampleLiveMenu();
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

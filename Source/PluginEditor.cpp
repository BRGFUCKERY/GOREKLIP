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
        titleLabel.setFont (juce::Font (18.0f, juce::Font::bold));
        titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
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

        // Oversample modes
        juce::StringArray modes { "x1", "x2", "x4", "x8", "x16" };
        for (int i = 0; i < modes.size(); ++i)
        {
            const int id = i + 1;
            liveCombo.addItem    (modes[i], id);
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

        // LIVE column is bound to oversampleMode parameter
        liveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            parameters, "oversampleMode", liveCombo);

        // OFFLINE column stored in userSettings
        const int offlineIndex = processor.getStoredOfflineOversampleIndex();
        offlineCombo.setSelectedId (offlineIndex + 1, juce::dontSendNotification);
        offlineCombo.onChange = [this]
        {
            const int idx = juce::jlimit (0, 4, offlineCombo.getSelectedId() - 1);
            processor.setStoredOfflineOversampleIndex (idx);
        };

        // TRIPLEFRY checkboxes
        tripleFryLabel.setText ("TRIPLEFRY", juce::dontSendNotification);
        tripleFryLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        tripleFryLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (tripleFryLabel);

        tripleFryLiveButton.setButtonText ("LIVE");
        tripleFryLiveButton.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
        tripleFryLiveButton.setToggleState (processor.getStoredTripleFryLiveEnabled(), juce::dontSendNotification);
        tripleFryLiveButton.onClick = [this]
        {
            processor.setStoredTripleFryLiveEnabled (tripleFryLiveButton.getToggleState());
        };
        addAndMakeVisible (tripleFryLiveButton);

        tripleFryOfflineButton.setButtonText ("OFFLINE");
        tripleFryOfflineButton.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
        tripleFryOfflineButton.setToggleState (processor.getStoredTripleFryOfflineEnabled(), juce::dontSendNotification);
        tripleFryOfflineButton.onClick = [this]
        {
            processor.setStoredTripleFryOfflineEnabled (tripleFryOfflineButton.getToggleState());
        };
        addAndMakeVisible (tripleFryOfflineButton);

        // Info label: plain ASCII text, nice wrapping
        infoLabel.setText ("TRIPLEFRY CAN FRY YOUR CPU",
                           juce::dontSendNotification);
        infoLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        infoLabel.setJustificationType (juce::Justification::topLeft);
        infoLabel.setMinimumHorizontalScale (0.8f);
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

        // TRIPLEFRY row
        auto tripleRow = r.removeFromTop (24);
        auto tripleLabelArea = tripleRow.removeFromLeft (90);
        tripleFryLabel.setBounds (tripleLabelArea);

        auto tripleLiveArea    = tripleRow.removeFromLeft (halfWidth - 90).reduced (4, 0);
        auto tripleOfflineArea = tripleRow.reduced (4, 0);

        tripleFryLiveButton.setBounds    (tripleLiveArea);
        tripleFryOfflineButton.setBounds (tripleOfflineArea);

        r.removeFromTop (8);
        infoLabel.setBounds (r);
    }

private:
    FruityClipAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& parameters;

    juce::Label     titleLabel;
    juce::Label     liveLabel;
    juce::Label     offlineLabel;
    juce::ComboBox  liveCombo;
    juce::ComboBox  offlineCombo;

    juce::Label        tripleFryLabel;
    juce::ToggleButton tripleFryLiveButton;
    juce::ToggleButton tripleFryOfflineButton;

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

    auto bounds = juce::Rectangle<float> (0.0f, 0.0f,
                                          (float) width, (float) height);

    // Transparent background – we draw icons only, ComboBox handles text if we let it
    g.setColour (juce::Colours::transparentBlack);
    g.fillRect (bounds);

    const auto name           = box.getName();
    const bool isLookBox      = (name == "lookBox");
    const bool isOversampBox  = (name == "oversampleBox");

    // Both top combos (SETTINGS + OVERSAMPLE) use the same pentagram icon,
    // mirrored horizontally so they are perfectly symmetrical.
    if (isLookBox || isOversampBox)
    {
        const float iconSize   = (float) height * 0.55f;
        const float iconRadius = iconSize * 0.5f;

        const float iconCenterX = isLookBox
            ? bounds.getRight() - iconSize * 0.9f      // hug the right edge
            : bounds.getX()     + iconSize * 0.9f;     // hug the left edge (mirror)

        const float iconCenterY = bounds.getCentreY();

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

        // Text is still handled by ComboBox itself.
        // For both top combos, we'll set textColour to transparent from the editor,
        // so only the pentagrams are visible.
        return;
    }

    // Any other ComboBox using this LNF just gets the transparent
    // background; JUCE will draw its text normally.
}

juce::Font DownwardComboBoxLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    // Give the oversample readout a slightly heavier look without overpowering the pentagram.
    if (box.getName() == "oversampleBox")
    {
        // Push a little more weight into the oversample readout without relying on deprecated APIs.
        const float fontHeight = (float) box.getHeight() * 0.52f * 1.2f;

        juce::FontOptions opts (fontHeight);
        opts = opts.withStyle ("SemiBold");

        return juce::Font (opts);
    }

    return juce::LookAndFeel_V4::getComboBoxFont (box);
}

//==============================================================
// Editor
//==============================================================
FruityClipAudioProcessorEditor::FruityClipAudioProcessorEditor (FruityClipAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    auto& lookSelector         = lookBox;
    auto& oversamplingSelector = oversampleBox;
    auto& menuSelector         = lookBox;

    setLookAndFeel (&customLookAndFeel);

    // Basic combo colours (we mostly draw via custom LNF)
    lookSelector.setColour (juce::ComboBox::backgroundColourId, juce::Colours::black);
    lookSelector.setColour (juce::ComboBox::textColourId,       juce::Colours::transparentWhite);

    oversamplingSelector.setColour (juce::ComboBox::backgroundColourId, juce::Colours::black);
    oversamplingSelector.setColour (juce::ComboBox::textColourId,       juce::Colours::transparentWhite);

    menuSelector.setColour (juce::ComboBox::backgroundColourId, juce::Colours::black);
    menuSelector.setColour (juce::ComboBox::textColourId,       juce::Colours::transparentWhite);

    // --------------------------------------------------
    // BACKGROUND + LOGO
    // --------------------------------------------------
    bgImage = juce::ImageCache::getFromMemory (BinaryData::bg_png,
                                               BinaryData::bg_pngSize);

    slamImage = juce::ImageCache::getFromMemory (BinaryData::slam_jpg,
                                                 BinaryData::slam_jpgSize);

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

    setupKnob01 (ottSlider);
    setupKnob01 (satSlider);
    setupKnob01 (modeSlider);

    // MODE is a hard 0/1 switch
    modeSlider.setRange (0.0, 1.0, 1.0); // ONLY 0 or 1

    gainSlider.setLookAndFeel (&fingerLnf);
    ottSlider .setLookAndFeel (&fingerLnf);
    satSlider .setLookAndFeel (&fingerLnf);
    modeSlider.setLookAndFeel (&fingerLnf);

    addAndMakeVisible (gainSlider);
    addAndMakeVisible (ottSlider);
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
    setupLabel (ottLabel,  "LOVE");
    setupLabel (satLabel,  "DEATH");
    setupLabel (modeLabel, "CLIPPER"); // will flip to LIMITER in runtime

    addAndMakeVisible (gainLabel);
    addAndMakeVisible (ottLabel);
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

    // OVERSAMPLING (right pentagram-button)
    oversampleBox.setName ("oversampleBox");
    oversampleBox.setJustificationType (juce::Justification::centred);
    oversampleBox.setTextWhenNothingSelected ("x1");
    oversampleBox.setColour (juce::ComboBox::textColourId,       juce::Colours::transparentWhite);
    oversampleBox.setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    oversampleBox.setColour (juce::ComboBox::outlineColourId,    juce::Colours::transparentBlack);
    oversampleBox.setColour (juce::ComboBox::arrowColourId,      juce::Colours::white);
    oversampleBox.setLookAndFeel (&comboLnf);
    oversampleBox.setInterceptsMouseClicks (false, false); // editor handles clicks
    addAndMakeVisible (oversampleBox);

    // --------------------------------------------------
    // PARAMETER ATTACHMENTS
    // --------------------------------------------------
    auto& apvts = processor.getParametersState();

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "inputGain", gainSlider);

    ottAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "ottAmount", ottSlider);

    satAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "satAmount", satSlider);

    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "useLimiter", modeSlider);

    oversampleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                        apvts, "oversampleMode", oversampleBox);

    // Small helper to keep SAT enable + label in sync with the mode value
    auto updateModeUI = [this]()
    {
        const bool useLimiter = (modeSlider.getValue() >= 0.5f);
        satSlider.setEnabled (! useLimiter);
        modeLabel.setText (useLimiter ? "LIMITER" : "CLIPPER", juce::dontSendNotification);
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
    ottSlider .setLookAndFeel (nullptr);
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

    // --- Top bar: perfectly symmetric pentagrams ---
    const int topMargin = 6;
    const int barH      = juce::jmax (16, h / 20);

    // Left: SETTINGS box (square)
    const int lookSize = barH;
    const int lookX    = topMargin;
    const int lookY    = topMargin;

    // Right: OVERSAMPLING box – same size, mirrored position
    const int osW = lookSize;                // SAME width as left box
    const int osH = barH;                    // SAME height as left box
    const int osX = w - osW - topMargin;     // same side margin
    const int osY = topMargin;               // same distance from top

    lookBox.setBounds       (lookX, lookY, lookSize, barH);
    oversampleBox.setBounds (osX,  osY,  osW,       osH);

    // --------------------------------------------------
    // Existing layout for knobs, labels, LUFS label etc.
    // Keep exactly the same maths you already had below.
    // --------------------------------------------------
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

    // LUFS label can stay where it was –
    // reuse the previous bounds logic if needed.
    // Just keep it centered-ish at the top as before.
    auto lufsBounds = juce::Rectangle<int> (0, 0, w, barH).reduced (80, 0);
    lufsLabel.setBounds (lufsBounds);
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
            lufsLabel.setText (text, juce::dontSendNotification);
        }
    }

    // --- NEW: drive pentagram + x1 colour from lastBurn (0..1) ---
    const float burnForIcons = juce::jlimit (0.0f, 1.0f, lastBurn);
    comboLnf.setBurnAmount (burnForIcons);

    const std::uint8_t level = (std::uint8_t) juce::jlimit (
        0, 255, (int) std::round (burnForIcons * 255.0f));
    auto burnColour = juce::Colour::fromRGB (level, level, level);

    // Both top pentagrams follow the same burn colour via arrowColourId.
    // Text stays transparent, we only care about the icon.
    lookBox.setColour      (juce::ComboBox::arrowColourId, burnColour);
    oversampleBox.setColour (juce::ComboBox::arrowColourId, burnColour);

    // Ensure both combo boxes repaint with the new colour
    lookBox.repaint();
    oversampleBox.repaint();

    repaint();
}

void FruityClipAudioProcessorEditor::showOversampleMenu()
{
    auto& state = processor.getParametersState();

    auto content = std::make_unique<OversampleSettingsComponent> (processor, state);
    content->setSize (380, 210);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle              = "OVERSAMPLING";
    options.dialogBackgroundColour   = juce::Colours::black;
    options.content.setOwned         (content.release());
    options.componentToCentreAround  = this;
    options.useNativeTitleBar        = true;
    options.resizable                = false;
    options.launchAsync();
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

    text << "• TRIPLEFRY\n";
    text << "Experimental high-definition oversampling that keeps your\n";
    text << "transients sharp while pushing the clip stage as clean and\n";
    text << "smooth as possible. It can hit your CPU hard at high\n";
    text << "settings, so treat it like a mastering move, not a default.\n\n";

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

    // KLIPERBIBLE – clickable, NEVER checkable (no tick flag)
    menu.addItem (idKlipBible,
                  "KLIPERBIBLE",
                  true); // enabled, but not a toggle

    // Handle selection
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

    if (oversampleBox.getBounds().contains (posInt))
    {
        showOversampleMenu();
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

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
    else if (gainSlider != nullptr && satSlider != nullptr && &slider == gainSlider)
    {
        // GAIN FINGER: display effective (user gain + static SAT auto-trim visual)
        const float gainDb = (float) slider.getValue();     // -12..+12
        const float sat    = (float) satSlider->getValue(); // 0..1

        const auto& range = slider.getRange();
        const float minDb = (float) range.getStart(); // -12
        const float maxDb = (float) range.getEnd();   // +12

        // Match the processor's static auto-trim law:
        // maxSatTrimDb = -4.5 dB at full SAT, quadratic ramp
        constexpr float maxSatTrimDb = -4.5f;
        const float satCurve  = sat * sat;
        const float satCompDb = maxSatTrimDb * satCurve;

        const float effectiveDb = gainDb + satCompDb;

        float norm = (effectiveDb - minDb) / (maxDb - minDb);
        norm = juce::jlimit (0.0f, 1.0f, norm);

        angle = minAngle + (maxAngle - minAngle) * norm;
    }
    else
    {
        // Normal knobs (SILK, SAT)
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
    auto setupKnob01 = [] (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (0.0, 1.0, 0.0001);
        s.setMouseDragSensitivity (250);
    };

    // GAIN uses dB range
    gainSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gainSlider.setMouseDragSensitivity (250);
    gainSlider.setRange (-12.0, 12.0, 0.01);

    setupKnob01 (silkSlider);
    setupKnob01 (satSlider);
    setupKnob01 (modeSlider); // MODE finger – param is bool, but we use 0..1 range

    gainSlider.setLookAndFeel (&fingerLnf);
    silkSlider.setLookAndFeel (&fingerLnf);
    satSlider.setLookAndFeel  (&fingerLnf);
    modeSlider.setLookAndFeel (&fingerLnf);

    addAndMakeVisible (gainSlider);
    addAndMakeVisible (silkSlider);
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
        lbl.setFont (juce::Font (16.0f, juce::Font::bold));
    };

    setupLabel (gainLabel, "GAIN");
    setupLabel (silkLabel, "SILK");
    setupLabel (satLabel,  "SAT");
    setupLabel (modeLabel, "CLIPPER"); // will switch to LIMITER when mode is on

    addAndMakeVisible (gainLabel);
    addAndMakeVisible (silkLabel);
    addAndMakeVisible (satLabel);
    addAndMakeVisible (modeLabel);

    // ----------------------
    // PARAMETER ATTACHMENTS
    // ----------------------
    auto& apvts = processor.getParametersState();

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "inputGain", gainSlider);

    satAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "satAmount",  satSlider);

    silkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "silkAmount", silkSlider);

    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, "useLimiter", modeSlider);

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

    // Now the LNF knows which slider is which
    fingerLnf.setControlledSliders (&gainSlider, &modeSlider, &satSlider);

    // Start GUI update timer (for burn animation)
    startTimerHz (30);
}

FruityClipAudioProcessorEditor::~FruityClipAudioProcessorEditor()
{
    stopTimer();
    gainSlider.setLookAndFeel (nullptr);
    silkSlider.setLookAndFeel (nullptr);
    satSlider.setLookAndFeel  (nullptr);
    modeSlider.setLookAndFeel (nullptr);
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

    // BURN OVERLAY – more smashed = more washed-out / burnt TikTok meme
    if (lastBurn > 0.01f)
    {
        const float b      = juce::jlimit (0.0f, 1.0f, lastBurn);
        const float shaped = std::pow (b, 0.45f); // ramps VERY fast near the top

        // 1) Heavy white wash – almost overexposed at full burn
        g.setColour (juce::Colours::white.withAlpha (0.70f * shaped));
        g.fillAll();

        // 2) Cold blue / purple-ish tint overlay (TikTok cooked photo vibe)
        const juce::Colour tint1 = juce::Colour::fromFloatRGBA (0.55f, 0.80f, 1.0f, 0.55f * shaped);
        g.setColour (tint1);
        g.fillAll();

        const juce::Colour tint2 = juce::Colour::fromFloatRGBA (0.45f, 0.55f, 0.95f, 0.35f * shaped);
        g.setColour (tint2);
        g.fillAll();

        // 3) Dark frame – makes it feel boxed-in & over-processed
        const int frameThickness = juce::jmax (2, (int) std::round (6.0f + 18.0f * shaped));
        g.setColour (juce::Colours::black.withAlpha (0.30f * shaped));
        g.drawRect (getLocalBounds(), frameThickness);

        // 4) Inner vignette: crushed blacks towards the center
        juce::Rectangle<int> inner = getLocalBounds().reduced ((int) std::round (10.0f + 40.0f * shaped));
        g.setColour (juce::Colours::black.withAlpha (0.55f * shaped));
        g.fillRect (inner);

        // 5) Vertical smear bands – like digital burn/overexposure streaks
        juce::Random& r = juce::Random::getSystemRandom();
        const int bandCount = (int) (20 * shaped);
        for (int i = 0; i < bandCount; ++i)
        {
            const int bandX   = r.nextInt (w);
            const int bandW   = juce::jmax (1, r.nextInt (4));
            const float alpha = 0.15f * shaped;

            juce::Colour bandColour = juce::Colour::fromFloatRGBA (0.9f, 0.95f, 1.0f, alpha);
            g.setColour (bandColour);
            g.fillRect (bandX, 0, bandW, h);
        }

        // 6) Noise speckles – gritty meme texture
        const int noiseCount = (int) (220 * shaped);
        for (int i = 0; i < noiseCount; ++i)
        {
            const int px = r.nextInt (w);
            const int py = r.nextInt (h);
            const int size = 1 + r.nextInt (2);

            const float choice = r.nextFloat();
            if (choice < 0.5f)
                g.setColour (juce::Colours::white.withAlpha (0.20f * shaped));
            else
                g.setColour (juce::Colours::black.withAlpha (0.20f * shaped));

            g.fillRect (px, py, size, size);
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

    const int knobSize = juce::jmin (w / 6, h / 3);
    const int spacing  = knobSize / 2;

    const int totalW   = knobSize * 4 + spacing * 3;
    const int startX   = (w - totalW) / 2;

    // Keep knobs low near the bottom
    const int bottomMargin = (int)(h * 0.05f);
    const int knobY        = h - knobSize - bottomMargin;

    gainSlider.setBounds (startX + 0 * (knobSize + spacing), knobY, knobSize, knobSize);
    silkSlider.setBounds (startX + 1 * (knobSize + spacing), knobY, knobSize, knobSize);
    satSlider .setBounds (startX + 2 * (knobSize + spacing), knobY, knobSize, knobSize);
    modeSlider.setBounds (startX + 3 * (knobSize + spacing), knobY, knobSize, knobSize);

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

    modeLabel.setBounds (modeSlider.getX(),
                         modeSlider.getBottom() + 2,
                         modeSlider.getWidth(),
                         labelH);
}

//==============================================================
// TIMER – pull burn value from processor
//==============================================================
void FruityClipAudioProcessorEditor::timerCallback()
{
    const float newBurn = processor.getGuiBurn();

    // Only repaint if it actually changed a bit
    if (std::abs (newBurn - lastBurn) > 0.01f)
    {
        lastBurn = newBurn;
        repaint();
    }
}

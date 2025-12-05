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

    // Fallback: just draw a simple white circle if no image
    if (! knobImage.isValid())
    {
        const float boundsSize = (float) juce::jmin (width, height);
        const float radius     = boundsSize * 0.5f * 0.85f;
        const float centreX    = (float) x + (float) width  * 0.5f;
        const float centreY    = (float) y + (float) height * 0.5f;

        const juce::Rectangle<float> thumbArea (centreX - radius, centreY - radius,
                                                radius * 2.0f, radius * 2.0f);

        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillEllipse (thumbArea);

        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.drawEllipse (thumbArea, 1.2f);
        return;
    }

    const float boundsSize = (float) juce::jmin (width, height);
    const float radius     = boundsSize * 0.5f;

    juce::Rectangle<float> imgRect ((float) x, (float) y, boundsSize, boundsSize);
    imgRect = imgRect.withCentre ({ (float) x + (float) width  * 0.5f,
                                    (float) y + (float) height * 0.5f });

    // Angle range: -135° to +135° around 12 o'clock
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
    else
    {
        // All other knobs (GAIN, SILK, SAT) – plain 0..1 to angle
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
    juce::Image knobImg = juce::ImageCache::getFromMemory (
        BinaryData::finger_png,
        BinaryData::finger_pngSize);

    fingerLnf.setKnobImage (knobImg);
    fingerLnf.setControlledSliders (&gainSlider, &modeSlider, &satSlider);

    // GAIN / SILK / SAT / MODE sliders
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
        // Deprecated ctor, but fine – JUCE just warns
        lbl.setFont (juce::Font (16.0f, juce::Font::bold));
    };

    setupLabel (gainLabel, "GAIN");
    setupLabel (silkLabel, "SILK");
    setupLabel (satLabel,  "SAT");
    setupLabel (modeLabel, "CLIPPER");

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

    setSize (600, 400);
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

    // 1) Render background into an offscreen image so we can mangle only the BG
    // -------------------------------------------------------------------------
    juce::Image renderedBg (juce::Image::ARGB, w, h, true);

    {
        juce::Graphics g2 (renderedBg);

        if (bgImage.isValid())
            g2.drawImageWithin (bgImage, 0, 0, w, h, juce::RectanglePlacement::stretchToFit);
        else
            g2.fillAll (juce::Colours::black);
    }

    // 2) Apply B&W + chroma-shadow burn and grain based on lastBurn
    //    (harder volume -> stronger effect)
    // -------------------------------------------------------------------------
    if (lastBurn > 0.01f)
    {
        const float b      = juce::jlimit (0.0f, 1.0f, lastBurn);
        const float shaped = std::pow (b, 0.65f); // smoother ramp but still nasty at the top

        const int width  = renderedBg.getWidth();
        const int height = renderedBg.getHeight();

        // Per-pixel: convert to greyscale, then inject coloured shadows
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                juce::Colour c = renderedBg.getPixelAt (x, y);

                if (c.getAlpha() == 0)
                    continue;

                const float r     = c.getFloatRed();
                const float gChan = c.getFloatGreen();
                const float bChan = c.getFloatBlue();
                const float a     = c.getFloatAlpha();

                // Classic luma greyscale
                float grey = 0.299f * r + 0.587f * gChan + 0.114f * bChan;

                // Contrast increases as you push harder
                const float contrast = 1.0f + 0.7f * shaped;
                grey = juce::jlimit (0.0f, 1.0f, 0.5f + (grey - 0.5f) * contrast);

                // Shadow factor: 1 in the darkest, 0 around midtones and above
                float shadow = 0.0f;
                if (grey < 0.5f)
                    shadow = juce::jlimit (0.0f, 1.0f, (0.5f - grey) / 0.5f); // grey 0.5 -> 0, grey 0 -> 1

                // Amount of chroma in shadows scales with how hard you're hitting
                const float chromaAmount = shadow * shaped;

                // Purple/blue chroma for shadows
                const float cr = 0.55f;
                const float cg = 0.20f;
                const float cb = 0.95f;

                const float invChroma = 1.0f - chromaAmount;

                const float outR = juce::jlimit (0.0f, 1.0f, grey * invChroma + cr * chromaAmount);
                const float outG = juce::jlimit (0.0f, 1.0f, grey * invChroma + cg * chromaAmount);
                const float outB = juce::jlimit (0.0f, 1.0f, grey * invChroma + cb * chromaAmount);

                renderedBg.setPixelAt (x, y, juce::Colour::fromFloatRGBA (outR, outG, outB, a));
            }
        }

        // Grain on top – only texture, no frames / bands / shapes
        juce::Graphics g2 (renderedBg);
        juce::Random& r = juce::Random::getSystemRandom();

        const int noiseCount = (int) (800 * shaped); // more smash = more grain
        for (int i = 0; i < noiseCount; ++i)
        {
            const int px   = r.nextInt (width);
            const int py   = r.nextInt (height);
            const int size = 1 + r.nextInt (2);

            const float choice = r.nextFloat();
            if (choice < 0.5f)
                g2.setColour (juce::Colours::white.withAlpha (0.20f * shaped));
            else
                g2.setColour (juce::Colours::black.withAlpha (0.20f * shaped));

            g2.fillRect (px, py, size, size);
        }
    }

    // 3) Draw the processed background
    // -------------------------------------------------------------------------
    g.drawImageAt (renderedBg, 0, 0);

    // 4) LOGO – drawn last, stays clean / untouched
    // -------------------------------------------------------------------------
    if (logoImage.isValid())
    {
        const float targetW = w * 0.80f;
        const float scale   = targetW / logoImage.getWidth();

        const int drawW = (int) (logoImage.getWidth()  * scale);
        const int drawH = (int) (logoImage.getHeight() * scale);

        const int x = (w - drawW) / 2;
        const int y = 0; // absolutely top

        // Crop top 20% of the source logo so the visible part hugs the top
        const int cropY      = (int) (logoImage.getHeight() * 0.20f);
        const int cropHeight = logoImage.getHeight() - cropY;

        g.drawImage (logoImage,
                     x, y, drawW, drawH,     // destination
                     0, cropY,               // source x, y
                     logoImage.getWidth(),
                     cropHeight);            // source height
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

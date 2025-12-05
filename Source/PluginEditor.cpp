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

    const float boundsSize = (float) juce::jmin (width, height);
    const float radius     = boundsSize * 0.5f * 0.85f;
    const float centreX    = (float) x + (float) width  * 0.5f;
    const float centreY    = (float) y + (float) height * 0.5f;

    const float angleRange = juce::MathConstants<float>::pi * 1.25f;
    const float startAngle = juce::MathConstants<float>::pi * 1.5f - angleRange * 0.5f;
    const float endAngle   = startAngle + angleRange;

    const float angle = startAngle + sliderPosProportional * (endAngle - startAngle);

    const juce::Rectangle<float> thumbArea (centreX - radius, centreY - radius,
                                            radius * 2.0f, radius * 2.0f);

    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillEllipse (thumbArea);

    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.drawEllipse (thumbArea, 1.2f);

    const float stickLength = radius * 0.85f;
    const float stickWidth  = radius * 0.15f;

    juce::Path stick;
    stick.addRoundedRectangle (-stickWidth * 0.5f, -stickLength * 0.1f, stickWidth, stickLength, stickWidth * 0.5f);

    g.setColour (juce::Colours::white);
    juce::AffineTransform t = juce::AffineTransform::rotation (angle, centreX, centreY)
                                .translated (0.0f, -radius * 0.15f);
    g.fillPath (stick, t);
}

//==============================================================
// Editor
//==============================================================
FruityClipAudioProcessorEditor::FruityClipAudioProcessorEditor (FruityClipAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (600, 400);

    middleFingerOnImage = juce::ImageFileFormat::loadFrom (juce::MemoryInputStream (BinaryData::middle_finger_on_png,
                                                                                    BinaryData::middle_finger_on_pngSize,
                                                                                    false));

    middleFingerOffImage = juce::ImageFileFormat::loadFrom (juce::MemoryInputStream (BinaryData::middle_finger_off_png,
                                                                                     BinaryData::middle_finger_off_pngSize,
                                                                                     false));

    if (auto* bgStream = new juce::MemoryInputStream (BinaryData::bg_png, BinaryData::bg_pngSize, false))
    {
        bgImage = juce::ImageFileFormat::loadFrom (*bgStream);
        delete bgStream;
    }

    if (auto* logoStream = new juce::MemoryInputStream (BinaryData::gorekliper_logo_png,
                                                        BinaryData::gorekliper_logo_pngSize,
                                                        false))
    {
        logoImage = juce::ImageFileFormat::loadFrom (*logoStream);
        delete logoStream;
    }

    auto& params = audioProcessor.parameters;

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "inputGain", gainSlider);
    satAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "satAmount",  satSlider);
    silkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "silkAmount", silkSlider);

    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "mode", modeSlider);

    auto updateModeLabel = [this]()
    {
        const float modeValue = (float) modeSlider.getValue();
        const bool isLimit    = (modeValue > 0.5f);

        if (isLimit)
            modeLabel.setText ("LIMIT", juce::dontSendNotification);
        else
            modeLabel.setText ("CLIP", juce::dontSendNotification);
    };

    modeSlider.onValueChange = [updateModeLabel]() { updateModeLabel(); };
    updateModeLabel();

    auto setupLabel = [] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, juce::Colours::white);
    };

    setupLabel (gainLabel, "GAIN");
    setupLabel (silkLabel, "SILK");
    setupLabel (satLabel,  "SAT");
    setupLabel (modeLabel, "CLIP");

    addAndMakeVisible (gainLabel);
    addAndMakeVisible (silkLabel);
    addAndMakeVisible (satLabel);
    addAndMakeVisible (modeLabel);

    auto setupKnob01 = [] (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (0.0, 1.0, 0.0001);
        s.setMouseDragSensitivity (250);
    };

    gainSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gainSlider.setMouseDragSensitivity (250);
    gainSlider.setRange (-12.0, 12.0, 0.01);

    setupKnob01 (silkSlider);
    setupKnob01 (satSlider);
    setupKnob01 (modeSlider);

    gainSlider.setLookAndFeel (&fingerLnf);
    silkSlider.setLookAndFeel (&fingerLnf);
    satSlider.setLookAndFeel  (&fingerLnf);
    modeSlider.setLookAndFeel (&fingerLnf);

    addAndMakeVisible (gainSlider);
    addAndMakeVisible (silkSlider);
    addAndMakeVisible (satSlider);
    addAndMakeVisible (modeSlider);

    startTimerHz (30);
}

FruityClipAudioProcessorEditor::~FruityClipAudioProcessorEditor()
{
    gainSlider.setLookAndFeel (nullptr);
    silkSlider.setLookAndFeel (nullptr);
    satSlider .setLookAndFeel (nullptr);
    modeSlider.setLookAndFeel (nullptr);
}

//==============================================================
// Timer – pull GUI burn level from processor
//==============================================================
void FruityClipAudioProcessorEditor::timerCallback()
{
    lastBurn = audioProcessor.guiBurnLevel.load();
    repaint();
}

//==============================================================
// PAINT
//==============================================================
void FruityClipAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();

    //==========================================================
    // 1) Render background into an offscreen image so we can
    //    mangle only the BG and keep the logo clean.
    //==========================================================
    juce::Image renderedBg (juce::Image::ARGB, w, h, true);

    {
        juce::Graphics g2 (renderedBg);

        if (bgImage.isValid())
            g2.drawImageWithin (bgImage, 0, 0, w, h, juce::RectanglePlacement::stretchToFit);
        else
            g2.fillAll (juce::Colours::black);
    }

    //==========================================================
    // 2) Apply B&W + chroma-shadow "burn" based on lastBurn
    //    (harder volume -> scale harder)
    //==========================================================
    if (lastBurn > 0.01f)
    {
        const float b      = juce::jlimit (0.0f, 1.0f, lastBurn);
        const float shaped = std::pow (b, 0.65f); // smoother ramp but still nasty at the top

        const int width  = renderedBg.getWidth();
        const int height = renderedBg.getHeight();

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                juce::Colour c = renderedBg.getPixelAt (x, y);

                if (c.getAlpha() == 0)
                    continue;

                const float r = c.getFloatRed();
                const float gChan = c.getFloatGreen();
                const float bChan = c.getFloatBlue();
                const float a = c.getFloatAlpha();

                float grey = 0.299f * r + 0.587f * gChan + 0.114f * bChan;

                const float contrast = 1.0f + 0.7f * shaped;
                grey = juce::jlimit (0.0f, 1.0f, 0.5f + (grey - 0.5f) * contrast);

                float shadow = 0.0f;
                if (grey < 0.5f)
                    shadow = juce::jlimit (0.0f, 1.0f, (0.5f - grey) / 0.5f);

                const float chromaAmount = shadow * shaped;

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

        juce::Graphics g2 (renderedBg);
        juce::Random& r = juce::Random::getSystemRandom();

        const int noiseCount = (int) (800 * shaped);
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

    g.drawImageAt (renderedBg, 0, 0);

    //==========================================================
    // 3) LOGO – drawn last, stays clean (same crop as before)
    //==========================================================
    if (logoImage.isValid())
    {
        const float targetW = w * 0.80f;
        const float scale   = targetW / logoImage.getWidth();

        const int drawW = (int) (logoImage.getWidth()  * scale);
        const int drawH = (int) (logoImage.getHeight() * scale);

        const int x = (w - drawW) / 2;
        const int y = 0;

        const int cropY      = (int) (logoImage.getHeight() * 0.20f);
        const int cropHeight = logoImage.getHeight() - cropY;

        g.drawImage (logoImage,
                     x, y, drawW, drawH,
                     0, cropY,
                     logoImage.getWidth(),
                     cropHeight);
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

    const int bottomMargin = (int) (h * 0.05f);
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

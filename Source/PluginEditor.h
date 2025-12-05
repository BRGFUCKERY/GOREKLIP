#pragma once

#include "JuceHeader.h"
#include "PluginProcessor.h"

// =============================================================
//  Custom LookAndFeel for the finger knob
// =============================================================
class MiddleFingerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MiddleFingerLookAndFeel()
    {
        // Knob graphic: add your finger PNG to BinaryData
        // and adjust these names if needed.
        knobImage = juce::ImageCache::getFromMemory(
            BinaryData::finger_png,
            BinaryData::finger_pngSize);
    }

    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float /*rotaryStartAngle*/,
                          float /*rotaryEndAngle*/,
                          juce::Slider& slider) override
    {
        if (! knobImage.isValid())
        {
            LookAndFeel_V4::drawRotarySlider(
                g, x, y, width, height,
                sliderPosProportional,
                0.0f, 0.0f,
                slider);
            return;
        }

        auto bounds = juce::Rectangle<float>((float)x, (float)y,
                                             (float)width, (float)height);

        g.setColour(juce::Colours::transparentBlack);
        g.fillRect(bounds);

        auto knobArea = bounds.reduced(width * 0.05f, height * 0.05f);

        float imgW = (float)knobImage.getWidth();
        float imgH = (float)knobImage.getHeight();
        float scale = std::min(knobArea.getWidth()  / imgW,
                               knobArea.getHeight() / imgH);

        juce::Rectangle<float> imgRect(0.0f, 0.0f, imgW * scale, imgH * scale);
        imgRect.setCentre(knobArea.getCentre());

        // Left -> right small arc (-45° to +45°)
        const float minAngle = juce::degreesToRadians(-45.0f);
        const float maxAngle = juce::degreesToRadians( 45.0f);
        const float angle    = minAngle + (maxAngle - minAngle) * sliderPosProportional;

        juce::AffineTransform t;
        t = t.rotated(angle, imgRect.getCentreX(), imgRect.getCentreY());

        g.addTransform(t);
        g.drawImage(knobImage,
                    imgRect.getX(), imgRect.getY(),
                    imgRect.getWidth(), imgRect.getHeight(),
                    0, 0, knobImage.getWidth(), knobImage.getHeight());
        g.setTransform(juce::AffineTransform()); // reset
    }

private:
    juce::Image knobImage;
};

// =============================================================
//  Main Editor
// =============================================================
class FruityClipAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    FruityClipAudioProcessorEditor (FruityClipAudioProcessor&);
    ~FruityClipAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    FruityClipAudioProcessor& processor;

    // Background image
    juce::Image bgImage;
    const float bgScale = 0.35f; // scale from original PNG

    // LookAndFeel and knobs
    MiddleFingerLookAndFeel fingerLnf;

    juce::Slider silkSlider; // LEFT
    juce::Slider satSlider;  // RIGHT

    juce::Label  silkLabel;
    juce::Label  satLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> satAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> silkAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruityClipAudioProcessorEditor)
};

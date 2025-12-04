/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
FRUITYCLIPAudioProcessorEditor::FRUITYCLIPAudioProcessorEditor (FRUITYCLIPAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Load GUI image
    backgroundImage = juce::ImageFileFormat::loadFrom(
        BinaryData::bg_png,
        BinaryData::bg_pngSize
    );

    if (backgroundImage.isValid())
    {
        float scale = 0.7f; // 70%

        int w = (int)(backgroundImage.getWidth()  * scale);
        int h = (int)(backgroundImage.getHeight() * scale);

        setSize (w, h);
    }
    else
    {
        // fallback size
        setSize(400, 300);
    }
}


    // Load GUI image from embedded binary data
    backgroundImage = juce::ImageFileFormat::loadFrom (BinaryData::bg_png,
                                                       BinaryData::bg_pngSize);

    // If image loads, resize plugin window to match image size
    if (backgroundImage.isValid())
        setSize (backgroundImage.getWidth(), backgroundImage.getHeight());
}

FRUITYCLIPAudioProcessorEditor::~FRUITYCLIPAudioProcessorEditor()
{
}

//==============================================================================
void FRUITYCLIPAudioProcessorEditor::paint (juce::Graphics& g)
{
    if (backgroundImage.isValid())
    {
        g.drawImageWithin (backgroundImage,
                           0, 0,
                           getWidth(), getHeight(),
                           juce::RectanglePlacement::stretchToFit);
    }
    else
    {
        g.fillAll (juce::Colours::black);
        g.setColour (juce::Colours::white);
        g.setFont (20.0f);
        g.drawFittedText ("FRUITYCLIP", getLocalBounds(), juce::Justification::centred, 1);
    }
}

void FRUITYCLIPAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor.
}

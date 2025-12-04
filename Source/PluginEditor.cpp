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
    // Load GUI image from embedded binary data
    backgroundImage = juce::ImageFileFormat::loadFrom (BinaryData::bg_png,
                                                       BinaryData::bg_pngSize);

    if (backgroundImage.isValid())
    {
        // Final scale = previous 70% * additional 50% = 0.35
        const float scale = 0.35f;

        const int w = (int)(backgroundImage.getWidth()  * scale);
        const int h = (int)(backgroundImage.getHeight() * scale);

        setSize (w, h);
    }
    else
    {
        // Fallback size
        setSize (400, 300);
    }
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
    // Layout for future UI elements comes here
}

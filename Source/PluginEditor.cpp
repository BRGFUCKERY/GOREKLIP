#include "PluginProcessor.h"
#include "PluginEditor.h"

FruityClipAudioProcessorEditor::FruityClipAudioProcessorEditor (FruityClipAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(400, 200);
}

void FruityClipAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::darkgrey);
    g.setColour (juce::Colours::white);
    g.setFont   (20.0f);
    g.drawFittedText ("FRUITY SOFT CLIPPER (DEFAULT)", getLocalBounds(), juce::Justification::centred, 1);
}

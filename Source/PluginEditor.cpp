#include "PluginProcessor.h"
#include "PluginEditor.h"

FruityClipAudioProcessorEditor::FruityClipAudioProcessorEditor (FruityClipAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (400, 200);
}

void FruityClipAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::white);
    g.setFont (20.0f);
    g.drawFittedText ("FRUITYCLIP (use GenericEditor\nfor sat & silk knobs)",
                      getLocalBounds(),
                      juce::Justification::centred,
                      2);
}

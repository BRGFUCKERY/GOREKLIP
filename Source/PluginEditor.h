#pragma once
#include <JuceHeader.h>

class FruityClipAudioProcessor;

class FruityClipAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    FruityClipAudioProcessorEditor (FruityClipAudioProcessor&);
    ~FruityClipAudioProcessorEditor() override {}

    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    FruityClipAudioProcessor& processor;
};

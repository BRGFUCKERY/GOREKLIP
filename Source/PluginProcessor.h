#pragma once

#include "JuceHeader.h"

class FruityClipAudioProcessor : public juce::AudioProcessor
{
public:
    FruityClipAudioProcessor();
    ~FruityClipAudioProcessor() override;

    //==========================================================
    // Core AudioProcessor overrides
    //==========================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================
    // Editor
    //==========================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==========================================================
    // Metadata
    //==========================================================
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==========================================================
    // Programs (we just use 1)
    //==========================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==========================================================
    // State
    //==========================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParametersState() { return parameters; }

private:
    // DSP state
    float thresholdLinear = 0.0f;   // clip threshold for SAT path
    float postGain        = 1.0f;   // Fruity-null alignment gain

    // Limiter state
    double sampleRate       = 44100.0;
    float  limiterGain      = 1.0f;
    float  limiterReleaseCo = 0.0f;

    juce::AudioProcessorValueTreeState parameters;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Helper DSP functions
    static float silkCurveFull (float x);
    static float bassBoostSaturate (float x, float satAmount);
    float        processLimiterSample (float x);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruityClipAudioProcessor)
};

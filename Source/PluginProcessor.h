#pragma once

#include "JuceHeader.h"
#include <atomic>

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
    bool hasEditor() const override { return true; }

    //==========================================================
    // Metadata
    //==========================================================
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==========================================================
    // Programs (we don't really use them)
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

    //==========================================================
    // Accessors
    //==========================================================
    juce::AudioProcessorValueTreeState& getParametersState() { return parameters; }

    // Value used by the GUI for the "burn" animation (0..1)
    float getGuiBurn() const noexcept { return guiBurn.load(); }

    // (Optional) expose auto-trim for debug/visualising later
    float getSatCompDb() const noexcept { return satCompDb.load(); }

private:
    //==========================================================
    // Parameters
    //==========================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==========================================================
    // DSP State
    //==========================================================
    // Soft-clip threshold for SAT at max (~ -6 dB)
    float thresholdLinear = 0.0f;
    // Post-gain that makes us "null" Fruity
    float postGain        = 1.0f;

    // Limiter state
    double sampleRate       = 44100.0;
    float  limiterGain      = 1.0f;
    float  limiterReleaseCo = 0.0f;

    // Auto input trim for SAT (in dB), adjusted dynamically to keep loudness near-unity
    std::atomic<float> satCompDb { 0.0f };

    // GUI meter smoothed state (0..1)
    std::atomic<float> guiBurn { 0.0f };

    // Parameter state
    juce::AudioProcessorValueTreeState parameters;

    // Helpers
    float processLimiterSample (float x);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruityClipAudioProcessor)
};

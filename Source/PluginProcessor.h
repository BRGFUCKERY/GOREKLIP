#pragma once

#include "JuceHeader.h"
#include <atomic>
#include <vector>

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
    // Programs
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
    // Helpers for the editor
    //==========================================================
    juce::AudioProcessorValueTreeState& getParametersState() { return parameters; }

    // 0..1 burn value for the background/white logo
    float getGuiBurn() const { return guiBurn.load(); }

    // Momentary LUFS-style number for GUI
    float getGuiLufs() const { return guiLufs.load(); }

private:
    //==========================================================
    // Internal helpers
    //==========================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Fruity-ish soft clip
    static float fruitySoftClipSample (float x, float threshold);

    // Neve 5060-style "Silk" curve
    static float silkCurveFull (float x);

    // Limiter sample processor
    float processLimiterSample (float x);

    //==========================================================
    // K-weighted LUFS meter state
    //==========================================================
    struct KFilterState
    {
        float z1a = 0.0f;
        float z2a = 0.0f;
        float z1b = 0.0f;
        float z2b = 0.0f;
    };

    void resetKFilterState (int numChannels);

    std::vector<KFilterState> kFilterStates;
    float lufsMeanSquare = 1.0e-6f;  // keep > 0

    //==========================================================
    // OTT high-pass split state (0–150 Hz dry, >150 Hz OTT)
    //==========================================================
    struct OttHPState
    {
        float low = 0.0f; // running lowpass state
    };

    void resetOttState (int numChannels);

    std::vector<OttHPState> ottStates;
    float ottAlpha = 0.0f; // one-pole LP factor based on 150 Hz

    //==========================================================
    // Internal state
    //==========================================================
    double sampleRate      = 44100.0;
    float  postGain        = 0.99999385f;  // Fruity-null alignment
    float  thresholdLinear = 0.5f;         // updated in ctor

    // Limiter
    float limiterGain      = 1.0f;
    float limiterReleaseCo = 0.0f;

    // OTT gain-match smoothing
    float lastOttGain = 1.0f;

    // GUI meter smoothed state (0..1)
    std::atomic<float> guiBurn { 0.0f };

    // GUI LUFS (K-weighted momentary)
    std::atomic<float> guiLufs { -60.0f };

    // Parameter state
    juce::AudioProcessorValueTreeState parameters;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruityClipAudioProcessor)
};

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

    // K-weighted momentary loudness (LUFS-style)
    float getGuiLufs() const { return guiLufs.load(); }

    // True if we currently have enough signal to show LUFS
    bool getGuiHasSignal() const { return guiSignalEnv.load() > 0.2f; }

private:
    //==========================================================
    // Internal helpers
    //==========================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Fruity-ish soft clip
    static float fruitySoftClipSample (float x, float threshold);

    // Limiter sample processor
    float processLimiterSample (float x);

    // Oversampling config helper
    void updateOversampling (int osIndex, int numChannels);

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
    float lufsMeanSquare = 1.0e-6f;  // keep > 0 to avoid log(0)

    //==========================================================
    // OTT high-pass split state (0–150 Hz dry, >150 Hz OTT)
    //==========================================================
    struct OttHPState
    {
        float low = 0.0f; // running lowpass state for low band
    };

    void resetOttState (int numChannels);

    std::vector<OttHPState> ottStates;
    float ottAlpha    = 0.0f;   // one-pole LP factor for 150 Hz split
    float lastOttGain = 1.0f;   // smoothed unity gain-match factor

    //==========================================================
    // Internal state
    //==========================================================
    double sampleRate      = 44100.0;
    float  postGain        = 0.99999385f;  // Fruity-null alignment
    float  thresholdLinear = 0.5f;         // updated in ctor

    // Limiter
    float limiterGain      = 1.0f;
    float limiterReleaseCo = 0.0f;

    // GUI burn value (0..1)
    std::atomic<float> guiBurn { 0.0f };

    // GUI LUFS value
    std::atomic<float> guiLufs { -60.0f };

    // GUI signal envelope (0..1) for gating the LUFS display
    std::atomic<float> guiSignalEnv { 0.0f };

    // Parameter state (includes oversampleMode)
    juce::AudioProcessorValueTreeState parameters;

    //==========================================================
    // Oversampling
    //==========================================================
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int currentOversampleIndex = 0;   // 0:x1, 1:x2, 2:x4, 3:x8, 4:x16
    int maxBlockSize           = 0;   // for oversampler->initProcessing

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruityClipAudioProcessor)
};

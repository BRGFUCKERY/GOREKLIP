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
    // Editor / metadata
    //==========================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                      { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==========================================================
    // Programs (we just use one)
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
    // Helpers for editor
    //==========================================================
    juce::AudioProcessorValueTreeState& getParametersState() { return parameters; }

    float getGuiBurn() const noexcept      { return guiBurn.load(); }
    float getGuiLufs() const noexcept      { return guiLufs.load(); }

private:
    //==========================================================
    // Parameter layout
    //==========================================================
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==========================================================
    // Soft clip curve (Fruity-ish)
    //==========================================================
    float fruitySoftClipSample (float x, float threshold);

    //==========================================================
    // K-weighting + LUFS
    //==========================================================
    struct KFilterState
    {
        float z1a = 0.0f, z2a = 0.0f;
        float z1b = 0.0f, z2b = 0.0f;
    };

    void resetKFilterState (int numChannels);

    std::vector<KFilterState> kFilterStates;
    float                     lufsMeanSquare = 1.0e-6f; // running mean-square of K-weighted signal

    //==========================================================
    // OTT split (simple)
    //==========================================================
    struct OttState
    {
        float lowZ = 0.0f;   // one-pole lowpass state
    };

    void resetOttState (int numChannels);

    std::vector<OttState> ottStates;
    float                 ottAlpha    = 0.0f;  // one-pole alpha for 150 Hz split
    float                 lastOttGain = 1.0f;  // simple smoothing

    //==========================================================
    // Internal processing state
    //==========================================================
    double sampleRate   = 44100.0;
    float  limiterGain  = 1.0f;
    float  limiterReleaseCo = 0.999f;

    float thresholdLinear = 0.5f;     // clip threshold (linear, 0..1)
    float modeBlend       = 0.0f;     // 0=clipper, 1=limiter
    float ottAmount       = 0.0f;     // 0..1 for OTT split
    float satAmount       = 0.0f;     // 0..1 saturator
    float silkAmount      = 0.0f;     // 0..1 "silk" tilt

    //==========================================================
    // Per-sample GUI meters (atomic for thread safety)
    //==========================================================
    std::atomic<float> guiBurn { 0.0f };   // 0..1
    std::atomic<float> guiLufs { -80.0f }; // short-term-ish LUFS for GUI

    // Parameter state (includes oversampleMode)
    juce::AudioProcessorValueTreeState parameters;

    //==========================================================
    // Oversampling
    //==========================================================
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int currentOversampleIndex = 0;   // 0:x1, 1:x2, 2:x4, 3:x8, 4:x16
    int maxBlockSize           = 0;   // for oversampler->initProcessing

    void updateOversampling (int osIndex, int numChannels);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruityClipAudioProcessor)
};


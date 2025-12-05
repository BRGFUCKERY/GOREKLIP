#pragma once

#include <JuceHeader.h>

class FruityClipAudioProcessor : public juce::AudioProcessor
{
public:
    FruityClipAudioProcessor();
    ~FruityClipAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FRUITYCLIP"; }

    bool acceptsMidi() const override              { return false; }
    bool producesMidi() const override             { return false; }
    bool isMidiEffect() const override             { return false; }
    double getTailLengthSeconds() const override   { return 0.0; }

    int getNumPrograms() override                  { return 1; }
    int getCurrentProgram() override               { return 0; }
    void setCurrentProgram (int) override          {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    // ----------------- DSP HELPERS -----------------

    // Hard clip that matches Fruity at soft = 0 (0 dBFS = 1.0f)
    static inline float fruityHardClip (float x) noexcept
    {
        if (x >  1.0f) return  1.0f;
        if (x < -1.0f) return -1.0f;
        return x;
    }

    // Base Silk Red curve at 100% amount.
    // We'll morph into this as Silk goes 0 -> 1.
    static inline float silkRedBase (float x) noexcept
    {
        // Odd harmonics: x + a3 x^3 + a5 x^5, then normalised.
        const float x3 = x * x * x;
        const float x5 = x3 * x * x;

        constexpr float a3 = 0.80f;
        constexpr float a5 = 0.25f;

        const float y  = x + a3 * x3 + a5 * x5;
        constexpr float norm = 1.0f / (1.0f + a3 + a5); // y(1) = 1

        return y * norm;
    }

    // Morph x into the Silk curve (NOT dry/wet blend).
    // amount = 0 -> x
    // amount = 1 -> silkRedBase(x)
    static inline float applySilk (float x, float amount) noexcept
    {
        if (amount <= 0.0001f)
            return x;

        if (amount >= 0.9999f)
            return silkRedBase (x);

        const float ySilk = silkRedBase (x);
        return x + amount * (ySilk - x);
    }

    // Saturation curve before clip, peak-locked.
    // satAmount in [0, 1]
    static inline float applySaturation (float x, float satAmount) noexcept
    {
        if (satAmount <= 0.0001f)
            return x;

        // Map amount to a "k" intensity. 0 -> clean, 1 -> pretty hot.
        const float k = satAmount * 10.0f;

        const float drive  = 1.0f + k;
        const float denom  = 1.0f + k * std::abs (x);
        const float y      = (x * drive) / denom;

        return y;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruityClipAudioProcessor)
};

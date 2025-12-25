#pragma once

#include "JuceHeader.h"
#include <atomic>
#include <vector>

class FruityClipAudioProcessor : public juce::AudioProcessor
{
public:
    enum class ClipMode
    {
        Digital = 0,
        Analog
    };

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

    // LUFS-driven burn value
    float getGuiBurnLufs() const { return guiBurnLufs.load(); }

    // K-weighted momentary loudness (LUFS-style)
    float getGuiLufs() const { return guiLufs.load(); }

    // True if we currently have enough signal to show LUFS
    bool getGuiHasSignal() const { return guiSignalEnv.load() > 0.2f; }

    ClipMode getClipMode() const;
    bool isLimiterEnabled() const;

    int  getLookModeIndex() const;
    void setLookModeIndex (int newIndex);

    int getLookMode() const
    {
        if (auto* p = parameters.getRawParameterValue ("lookMode"))
            return (int) p->load();
        return 0;
    }

    int getStoredLookMode() const;
    void setStoredLookMode (int modeIndex);

    // Offline oversample index
    int  getStoredOfflineOversampleIndex() const;
    void setStoredOfflineOversampleIndex (int index);

    // LIVE oversample index (global default for new instances)
    int  getStoredLiveOversampleIndex() const;
    void setStoredLiveOversampleIndex (int index);

    // Bypass all processing after input gain (for A/B)
    void setGainBypass (bool shouldBypass)        { gainBypass.store (shouldBypass); }
    bool getGainBypass() const                    { return gainBypass.load(); }

private:
    //==========================================================
    // Internal helpers
    //==========================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Fruity-ish soft clip
    static float fruitySoftClipSample (float x, float threshold);

    // Limiter sample processor
    float processLimiterSample (float x);

    float applySilkPreEmphasis  (float x, int channel, float silkAmount);
    float applySilkDeEmphasis   (float x, int channel, float silkAmount);

    // SILK color stage at base rate, pre-clip
    float applySilkAnalogSample (float x, int channel, float silkAmount);

    // Analog “Lavry-ish” clipper in oversampled domain
    float applyClipperAnalogSample (float x, int channel, float silkAmount);

    // Analog tone-match tilt, post-clip, back at base rate or in the oversampled block
    float applyAnalogToneMatch (float x, int channel, float silkAmount);

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
    float lufsAverageLufs = -60.0f;  // slow (~2s) averaged LUFS in dB for LOOK = LUFS burn

    struct SilkState
    {
        float pre    = 0.0f;
        float de     = 0.0f;
        float evenDc = 0.0f; // DC tracker for quadratic (even-harmonic) term
    };

    void resetSilkState (int numChannels);

    std::vector<SilkState> silkStates;

    float silkEvenDcAlpha = 0.0f; // DC servo coeff for quadratic even term (base rate)

    //==========================================================
    // SAT bass-tilt state (for gradual TikTok bass boost)
    //==========================================================
    struct SatState
    {
        float low = 0.0f;   // lowpassed state for bass emphasis
    };

    void resetSatState (int numChannels);

    std::vector<SatState> satStates;
    float satLowAlpha = 0.0f;    // one-pole LP factor for SAT bass tilt

    //==========================================================
    // Analog tone-match state (for 0-silk 5060->Lavry match)
    //==========================================================
    struct AnalogToneState
    {
        float low250 = 0.0f; // lowpassed state for ~250 Hz split
        float low10k = 0.0f; // lowpassed state for ~10 kHz split
    };

    void resetAnalogToneState (int numChannels);

    std::vector<AnalogToneState> analogToneStates;
    float analogToneAlpha250 = 0.0f;    // one-pole LP factor for ~250 Hz split
    float analogToneAlpha10k = 0.0f;    // one-pole LP factor for ~10 kHz split
    // Tone split coeff updater (base-rate; independent of oversampling)
    void updateAnalogToneSplitCoefficients();

    struct PostLPState { float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f; };
    std::vector<PostLPState> postLP_a;
    std::vector<PostLPState> postLP_b;

    float pc_b0_1 = 1.0f, pc_b1_1 = 0.0f, pc_b2_1 = 0.0f, pc_a1_1 = 0.0f, pc_a2_1 = 0.0f;
    float pc_b0_2 = 1.0f, pc_b1_2 = 0.0f, pc_b2_2 = 0.0f, pc_a1_2 = 0.0f, pc_a2_2 = 0.0f;

    void updatePostLP4();
    inline float processPostLP4 (float x, int ch) noexcept;

    float analogEnvAttackAlpha  = 0.0f; // envelope follower for analog bias
    float analogEnvReleaseAlpha = 0.0f;
    float analogDcAlpha         = 0.0f; // DC blocker coefficient for analog clipper (computed per-block for OS rate)

    struct AnalogTransientState
    {
        float fastEnv = 0.0f; // legacy (no longer used, kept for compatibility)
        float slowEnv = 0.0f; // legacy (no longer used, kept for compatibility)
        float slew    = 0.0f; // one-pole LP memory for slew smoothing
        float prev    = 0.0f; // previous sample for slope gating (works under oversampling)
    };

    void resetAnalogTransientState (int numChannels);

    std::vector<AnalogTransientState> analogTransientStates;
    float analogFastEnvA = 0.0f;
    float analogSlowEnvA = 0.0f;
    float analogSlewA    = 0.0f;
    float analogBiasA    = 0.0f;

    struct DsmCaptureEq
    {
        static constexpr int kNumBands = 32;

        void prepare (double sampleRate, int numChannels)
        {
            sr = sampleRate;
            filters.clear();
            filters.resize ((size_t) numChannels);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                filters[(size_t) ch].clear();
                filters[(size_t) ch].reserve (kNumBands);

                for (int i = 0; i < kNumBands; ++i)
                {
                    juce::dsp::IIR::Filter<float> f;
                    const float fc = kCentersHz[i];
                    const float Q  = 1.0f;
                    const float g  = juce::Decibels::decibelsToGain (kGainDb[i]);

                    auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter ((double) sr, (double) fc, (double) Q, (double) g);
                    f.coefficients = coeffs;
                    filters[(size_t) ch].push_back (std::move (f));
                }
            }
        }

        float processSample (int ch, float x) noexcept
        {
            float y = x;
            auto& chain = filters[(size_t) ch];
            for (auto& f : chain)
                y = f.processSample (y);
            return y;
        }

        double sr = 48000.0;

        // 32 log-spaced centers, 30 Hz..16 kHz
        static constexpr float kCentersHz[kNumBands] =
        {
            33.092579f,
            40.267004f,
            48.996835f,
            59.619282f,
            72.544660f,
            88.274275f,
            107.414574f,
            130.703420f,
            159.041830f,
            193.508035f,
            235.460310f,
            286.507782f,
            348.616731f,
            424.190115f,
            516.150857f,
            628.044838f,
            764.213258f,
            929.914611f,
            1131.559314f,
            1376.941131f,
            1675.545405f,
            2038.935964f,
            2481.168518f,
            3019.339094f,
            3674.254166f,
            4470.574843f,
            5439.007361f,
            6616.045966f,
            8047.239269f,
            9788.571933f,
            11907.160373f,
            14484.677393f
        };

        // Static capture curve (dB) extracted from your dry vs DSM@10% exports (Song 1+2 averaged, smoothed)
        static constexpr float kGainDb[kNumBands] =
        {
            0.760310f,
            0.000000f,
            0.810142f,
            0.860032f,
            0.911617f,
            0.987374f,
            1.082699f,
            1.183147f,
            1.301452f,
            1.465170f,
            1.554878f,
            1.552840f,
            1.570643f,
            1.567540f,
            1.608158f,
            1.651806f,
            1.683060f,
            1.750147f,
            1.842836f,
            1.982185f,
            2.148078f,
            2.376799f,
            2.646037f,
            2.927008f,
            3.152581f,
            3.302534f,
            3.377938f,
            3.488114f,
            3.564036f,
            4.178587f,
            4.352958f,
            4.352958f
        };

        std::vector<std::vector<juce::dsp::IIR::Filter<float>>> filters;
    };

    DsmCaptureEq dsmCaptureEq;


    //==========================================================
    // Analog clipper state (per channel, for bias memory)
    //==========================================================
    struct AnalogClipState
    {
        float biasMemory = 0.0f;
        float levelEnv   = 0.0f; // slow envelope of |in| for bias engagement
        float dcBlock    = 0.0f; // ultra-low HP state to remove DC without killing even harmonics
    };

    void resetAnalogClipState (int numChannels);

    std::vector<AnalogClipState> analogClipStates;

    //==========================================================
    // Internal state
    //==========================================================
    double sampleRate      = 44100.0;
    float  postGain        = 1.0f;          // kept for potential special modes
    float  thresholdLinear = 0.5f;         // updated in ctor

    // Limiter
    float limiterGain      = 1.0f;
    float limiterReleaseCo = 0.0f;

    // GUI burn value (0..1)
    std::atomic<float> guiBurn { 0.0f };

    // GUI LUFS-based burn value (0..1)
    std::atomic<float> guiBurnLufs { 0.0f };

    // GUI LUFS value
    std::atomic<float> guiLufs { -60.0f };

    // GUI signal envelope (0..1) for gating the LUFS display
    std::atomic<float> guiSignalEnv { 0.0f };

    // When true, only input gain is applied; OTT/SAT/limiter/oversampling/metering are bypassed
    std::atomic<bool> gainBypass { false };

    // Parameter state (includes oversampleMode)
    juce::AudioProcessorValueTreeState parameters;

    // Global user settings (e.g. preferred look mode)
    std::unique_ptr<juce::PropertiesFile> userSettings;

    // Offline oversample index:
    //   -1 = follow LIVE setting ("SAME")
    //    0 = x1, 1 = x2, 2 = x4, 3 = x8, 4 = x16, 5 = x32, 6 = x64
    int  storedOfflineOversampleIndex = -1;

    // LIVE oversample index (0 = x1, 1 = x2, 2 = x4, 3 = x8, 4 = x16, 5 = x32, 6 = x64)
    // NOTE: This is now only used at runtime if needed; it is no longer loaded/saved
    //       from userSettings or used as a global default for new instances.
    int  storedLiveOversampleIndex = 0;

    //==========================================================
    // Oversampling
    //==========================================================
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int currentOversampleIndex = 0;   // 0=x1, 1=x2, 2=x4, 3=x8, 4=x16, 5=x32, 6=x64
    int currentOversampleFactor = 1;  // 1,2,4,8,16,32,64 (derived from index)
    int maxBlockSize           = 0;   // for oversampler->initProcessing

    void updateOversampling (int osIndex, int numChannels);
    void updateAnalogClipperCoefficients();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruityClipAudioProcessor)
};

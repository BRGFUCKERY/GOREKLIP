#include "../Source/PluginProcessor.h"
#include "JuceHeader.h"

#include <iostream>
#include <limits>

namespace
{
    constexpr int   kOfflineOversampleIndex = 3; // x8 oversampling for the render harness
    constexpr int   kBlockSize              = 512;
    const juce::File kInputFile  { juce::File::getCurrentWorkingDirectory().getChildFile ("Tests/SilkMaxRef/fastcartest.wav") };
    const juce::File kOutputFile { juce::File::getCurrentWorkingDirectory().getChildFile ("Tests/SilkMaxRef/gk_silkmax_test.wav") };
}

// Simple offline render that feeds fastcartest.wav through GK's digital clipper
// plus the Silk Max coloration block (analog mode @ 100%). The output is written
// to gk_silkmax_test.wav for nulling against reference_silk_100.wav.
int main()
{
    juce::ScopedJuceInitialiser_GUI guiInit;

    if (! kInputFile.existsAsFile())
    {
        std::cout << "Input file not found: " << kInputFile.getFullPathName() << std::endl;
        return 1;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (kInputFile));
    if (! reader)
    {
        std::cout << "Could not open input wav: " << kInputFile.getFullPathName() << std::endl;
        return 1;
    }

    const int64 numSamples64 = reader->lengthInSamples;
    const int   numSamples   = (int) juce::jlimit<int64> (0, std::numeric_limits<int>::max(), numSamples64);
    const int   numChannels  = 2; // render in stereo even if the source is mono

    juce::AudioBuffer<float> buffer (numChannels, numSamples);
    buffer.clear();

    reader->read (&buffer, 0, numSamples, 0, true, true);

    FruityClipAudioProcessor processor;

    // Configure parameters to force Silk Max analog path
    auto& params = processor.getParametersState();
    if (auto* p = params.getRawParameterValue ("clipMode"))        p->store (1.0f); // Analog
    if (auto* p = params.getRawParameterValue ("useLimiter"))      p->store (0.0f);
    if (auto* p = params.getRawParameterValue ("ottAmount"))       p->store (0.0f);
    if (auto* p = params.getRawParameterValue ("satAmount"))       p->store (0.0f);
    if (auto* p = params.getRawParameterValue ("inputGain"))       p->store (0.0f);
    if (auto* p = params.getRawParameterValue ("oversampleMode"))  p->store ((float) kOfflineOversampleIndex);

    processor.prepareToPlay (reader->sampleRate, kBlockSize);

    juce::MidiBuffer midi;
    int remaining = numSamples;
    int offset    = 0;

    while (remaining > 0)
    {
        const int block = juce::jmin (remaining, kBlockSize);
        juce::AudioBuffer<float> blockView (buffer.getArrayOfWritePointers(), numChannels, offset, block);
        processor.processBlock (blockView, midi);
        offset    += block;
        remaining -= block;
    }

    kOutputFile.deleteFile();
    kOutputFile.getParentDirectory().createDirectory();

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer (wavFormat.createWriterFor (
        kOutputFile.createOutputStream().release(),
        reader->sampleRate,
        (unsigned int) numChannels,
        24,
        {},
        0));

    if (! writer)
    {
        std::cout << "Failed to create output writer: " << kOutputFile.getFullPathName() << std::endl;
        return 1;
    }

    writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
    writer.reset();

    std::cout << "Wrote Silk Max render to: " << kOutputFile.getFullPathName() << std::endl;
    return 0;
}

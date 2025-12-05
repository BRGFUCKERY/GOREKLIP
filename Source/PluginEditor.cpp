#include "PluginEditor.h"
#include "PluginProcessor.h"

// include the generated BinaryData header from your JUCE binary data target
#include "juce_binarydata_FRUITYCLIPData/JuceLibraryCode/BinaryData.h"

//==============================================================================

FruityClipAudioProcessorEditor::FruityClipAudioProcessorEditor (FruityClipAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p)
{
    // -------------------------------------------------------------------------
    // Window size roughly matching your screenshot
    // -------------------------------------------------------------------------
    setResizable (false, false);
    setSize (768, 940);

    // -------------------------------------------------------------------------
    // IMAGES
    // File names: bg.png, finger.png, gorekliper_logo.png
    // JUCE turns them into: bg_png, finger_png, gorekliper_logo_png
    // -------------------------------------------------------------------------
    backgroundImage = juce::ImageCache::getFromMemory (
        BinaryData::bg_png,
        BinaryData::bg_pngSize);

    fingerImage = juce::ImageCache::getFromMemory (
        BinaryData::finger_png,
        BinaryData::finger_pngSize);

    logoImage = juce::ImageCache::getFromMemory (
        BinaryData::gorekliper_logo_png,
        BinaryData::gorekliper_logo_pngSize);

    // -------------------------------------------------------------------------
    // SILK SLIDER
    // -------------------------------------------------------------------------
    addAndMakeVisible (silkSlider);
    silkSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    silkSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    // 7 o'clock -> 5 o'clock sweep
    silkSlider.setRotaryParameters (2.356f, -0.785f, true);

    // your processor has "parameters" (AudioProcessorValueTreeState)
    silkAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment> (
            processorRef.parameters, "SILK", silkSlider);

    // -------------------------------------------------------------------------
    // SAT SLIDER
    // -------------------------------------------------------------------------
    addAndMakeVisible (satSlider);
    satSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    satSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    // 7 o'clock -> 5 o'clock sweep
    satSlider.setRotaryParameters (2.356f, -0.785f, true);

    satAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment> (
            processorRef.parameters, "SAT", satSlider);
}

FruityClipAudioProcessorEditor::~FruityClipAudioProcessorEditor() = default;

//==============================================================================

void FruityClipAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto w = bounds.getWidth();
    auto h = bounds.getHeight();

    // -------------------------------------------------------------------------
    // BACKGROUND (bg.png)
    // -------------------------------------------------------------------------
    if (backgroundImage.isValid())
        g.drawImage (backgroundImage, bounds);
    else
        g.fillAll (juce::Colours::black);

    // -------------------------------------------------------------------------
    // FINGER IMAGE OVERLAY (finger.png) – scaled, centered a bit lower
    // -------------------------------------------------------------------------
    if (fingerImage.isValid())
    {
        const float fingerWidth  = w * 0.80f;
        const float fingerHeight = fingerWidth * (fingerImage.getHeight() / (float) fingerImage.getWidth());

        const float fingerX = (w - fingerWidth) * 0.5f;
        const float fingerY = h * 0.20f; // sits under the logo area

        juce::Rectangle<float> fingerArea (fingerX, fingerY, fingerWidth, fingerHeight);
        g.drawImage (fingerImage, fingerArea);
    }

    // -------------------------------------------------------------------------
    // HUGE METAL LOGO (gorekliper_logo.png, ≈ 60% width at the top)
    // -------------------------------------------------------------------------
    if (logoImage.isValid())
    {
        const float logoWidth  = w * 0.60f;          // 60% of total width
        const float logoHeight = logoWidth * 0.25f;  // tweak if aspect looks off

        const float logoX = (w - logoWidth) * 0.5f;  // center
        const float logoY = h * 0.08f;               // a little down from top

        juce::Rectangle<float> logoArea (logoX, logoY, logoWidth, logoHeight);
        g.drawImage (logoImage, logoArea);
    }
}

//==============================================================================

void FruityClipAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    const int width  = bounds.getWidth();
    const int height = bounds.getHeight();

    // -------------------------------------------------------------------------
    // Big knobs layout
    //  - ~3× size (relative to height)
    //  - closer together
    //  - moved up so they start where the old ones ended
    // -------------------------------------------------------------------------
    const int knobDiameter = (int) (height * 0.30f); // ~30% of height
    const int knobY        = (int) (height * 0.55f); // lifted up

    const int spacingBetweenKnobs = (int) (width * 0.06f); // small gap
    const int centerX             = width / 2;

    const int totalKnobsWidth = knobDiameter * 2 + spacingBetweenKnobs;
    const int firstKnobX      = centerX - (totalKnobsWidth / 2);
    const int secondKnobX     = firstKnobX + knobDiameter + spacingBetweenKnobs;

    silkSlider.setBounds (firstKnobX,  knobY, knobDiameter, knobDiameter);
    satSlider .setBounds (secondKnobX, knobY, knobDiameter, knobDiameter);
}

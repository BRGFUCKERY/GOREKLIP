#include "PluginEditor.h"

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
    //  - Replace the BinaryData names below with your actual ones
    // -------------------------------------------------------------------------
    backgroundImage = juce::ImageCache::getFromMemory (BinaryData::background_jpg,
                                                       BinaryData::background_jpgSize);
    logoImage       = juce::ImageCache::getFromMemory (BinaryData::gorekliper_logo_png,
                                                       BinaryData::gorekliper_logo_pngSize);
    silkIconImage   = juce::ImageCache::getFromMemory (BinaryData::silk_icon_png,
                                                       BinaryData::silk_icon_pngSize);
    satIconImage    = juce::ImageCache::getFromMemory (BinaryData::sat_icon_png,
                                                       BinaryData::sat_icon_pngSize);

    // -------------------------------------------------------------------------
    // SILK SLIDER
    // -------------------------------------------------------------------------
    addAndMakeVisible (silkSlider);
    silkSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    silkSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    // 7 o'clock -> 5 o'clock sweep
    silkSlider.setRotaryParameters (2.356f, -0.785f, true);

    // attach to your APVTS parameter (change "SILK" if your ID is different)
    silkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, "SILK", silkSlider);

    // -------------------------------------------------------------------------
    // SAT SLIDER
    // -------------------------------------------------------------------------
    addAndMakeVisible (satSlider);
    satSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    satSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    // 7 o'clock -> 5 o'clock sweep
    satSlider.setRotaryParameters (2.356f, -0.785f, true);

    // attach to your APVTS parameter (change "SAT" if your ID is different)
    satAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, "SAT", satSlider);
}

FruityClipAudioProcessorEditor::~FruityClipAudioProcessorEditor() = default;

//==============================================================================

void FruityClipAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto w = bounds.getWidth();
    auto h = bounds.getHeight();

    // -------------------------------------------------------------------------
    // BACKGROUND (full photo of the finger / face)
    // -------------------------------------------------------------------------
    if (backgroundImage.isValid())
        g.drawImage (backgroundImage, bounds);
    else
        g.fillAll (juce::Colours::black);

    // -------------------------------------------------------------------------
    // HUGE METAL LOGO (≈ 60% width at the top)
    // -------------------------------------------------------------------------
    if (logoImage.isValid())
    {
        const float logoWidth  = w * 0.60f;          // 60% of total width
        const float logoHeight = logoWidth * 0.25f;  // tweak this ratio if needed

        const float logoX = (w - logoWidth) * 0.5f;  // center
        const float logoY = h * 0.08f;               // a little down from top

        juce::Rectangle<float> logoArea (logoX, logoY, logoWidth, logoHeight);
        g.drawImage (logoImage, logoArea);
    }

    // -------------------------------------------------------------------------
    // SILK / SAT ICONS UNDER KNOBS
    // -------------------------------------------------------------------------
    if (silkIconImage.isValid())
        g.drawImage (silkIconImage, silkIconBounds.toFloat());

    if (satIconImage.isValid())
        g.drawImage (satIconImage, satIconBounds.toFloat());
}

//==============================================================================

void FruityClipAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    const int width  = bounds.getWidth();
    const int height = bounds.getHeight();

    // -------------------------------------------------------------------------
    // Big knobs layout
    //  - ~3× original size (relative to height)
    //  - closer together
    //  - starting higher on the GUI (where the old small knobs ended)
    // -------------------------------------------------------------------------
    const int knobDiameter = (int) (height * 0.30f);     // 30% of height
    const int knobY        = (int) (height * 0.55f);     // lifted up

    const int spacingBetweenKnobs = (int) (width * 0.06f);   // small gap
    const int centerX             = width / 2;

    const int totalKnobsWidth     = knobDiameter * 2 + spacingBetweenKnobs;
    const int firstKnobX          = centerX

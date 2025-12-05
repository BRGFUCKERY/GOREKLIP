#include "PluginEditor.h"

//==============================================================================
FruityClipAudioProcessorEditor::FruityClipAudioProcessorEditor (FruityClipAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    // Load background (bg.png -> BinaryData::bg_png)
    bgImage = juce::ImageCache::getFromMemory(
        BinaryData::bg_png,
        BinaryData::bg_pngSize);

    // Set editor size based on background at 0.35 scale
    if (bgImage.isValid())
    {
        int w = (int)(bgImage.getWidth()  * bgScale);
        int h = (int)(bgImage.getHeight() * bgScale);
        setSize (w, h);
    }
    else
    {
        setSize (600, 400); // fallback
    }

    // Use our finger look & feel for knobs
    silkSlider.setLookAndFeel (&fingerLnf);
    satSlider.setLookAndFeel  (&fingerLnf);

    // Common slider settings
    auto setupKnob = [] (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (0.0, 1.0, 0.0001);
        s.setMouseDragSensitivity (250);
    };

    setupKnob (silkSlider);
    setupKnob (satSlider);

    addAndMakeVisible (silkSlider);
    addAndMakeVisible (satSlider);

    // Labels under knobs
    silkLabel.setText ("SILK", juce::dontSendNotification);
    satLabel.setText  ("SAT",  juce::dontSendNotification);

    auto setupLabel = [] (juce::Label& lbl)
    {
        lbl.setJustificationType (juce::Justification::centred);
        lbl.setEditable (false, false, false);
        lbl.setColour (juce::Label::textColourId, juce::Colours::white);
        lbl.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        lbl.setFont (juce::Font (14.0f, juce::Font::bold));
    };

    setupLabel (silkLabel);
    setupLabel (satLabel);

    addAndMakeVisible (silkLabel);
    addAndMakeVisible (satLabel);

    // Attach to parameters
    auto& apvts = processor.getParametersState();
    satAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "satAmount",  satSlider);
    silkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "silkAmount", silkSlider);
}

FruityClipAudioProcessorEditor::~FruityClipAudioProcessorEditor()
{
    silkSlider.setLookAndFeel (nullptr);
    satSlider.setLookAndFeel  (nullptr);
}

//==============================================================================
void FruityClipAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    if (bgImage.isValid())
    {
        const int w = getWidth();
        const int h = getHeight();

        // Draw bg.png stretched to our editor size
        g.drawImageWithin(bgImage,
                          0, 0, w, h,
                          juce::RectanglePlacement::stretchToFit);
    }
}

//==============================================================================
void FruityClipAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // Base knob size based on editor, then shrink to ~20% (80% smaller)
    const int baseKnobSize = juce::jmin (getWidth() / 4, getHeight() / 3);
    const int knobSize     = juce::jmax (40, (int)(baseKnobSize * 0.2f)); // keep min size 40px

    const int margin = 10;

    // Reserve a strip at bottom for knobs + labels
    auto bottomArea = bounds.removeFromBottom (knobSize + 2 * margin + 20); // extra for labels

    // Split into left (silk) / right (sat)
    auto leftArea  = bottomArea.removeFromLeft (getWidth() / 2);
    auto rightArea = bottomArea;

    // Centre knobs in their halves
    auto silkKnobArea = leftArea.withSizeKeepingCentre (knobSize, knobSize);
    auto satKnobArea  = rightArea.withSizeKeepingCentre (knobSize, knobSize);

    silkSlider.setBounds (silkKnobArea);
    satSlider.setBounds  (satKnobArea);

    // Labels just under each knob
    const int labelHeight = 18;

    silkLabel.setBounds (silkKnobArea.getX(),
                         silkKnobArea.getBottom() + 2,
                         silkKnobArea.getWidth(),
                         labelHeight);

    satLabel.setBounds  (satKnobArea.getX(),
                         satKnobArea.getBottom() + 2,
                         satKnobArea.getWidth(),
                         labelHeight);
}

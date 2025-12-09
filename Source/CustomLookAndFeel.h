#pragma once

class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel()
    {
        // Dropdown / Popup menus
        setColour (juce::PopupMenu::backgroundColourId, juce::Colours::black);
        setColour (juce::PopupMenu::textColourId, juce::Colours::white);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colours::darkgrey);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

        // ComboBox appearance
        setColour (juce::ComboBox::backgroundColourId, juce::Colours::black);
        setColour (juce::ComboBox::outlineColourId, juce::Colours::black);
        setColour (juce::ComboBox::textColourId, juce::Colours::white);
        setColour (juce::ComboBox::buttonColourId, juce::Colours::black);
    }
};

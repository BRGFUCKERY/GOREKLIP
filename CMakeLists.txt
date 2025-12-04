cmake_minimum_required(VERSION 3.15)

project(FRUITYCLIP VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE will be in the JUCE folder (we clone it in the workflow)
add_subdirectory(JUCE)

juce_add_binary_data(FRUITYCLIPData
    SOURCES
        Assets/bg.png
)

juce_add_plugin(FRUITYCLIP
    COMPANY_NAME "BRG"
    PLUGIN_MANUFACTURER_CODE BRGF
    PLUGIN_CODE FRCL
    FORMATS VST3
    PRODUCT_NAME "FRUITYCLIP"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(FRUITYCLIP PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_link_libraries(FRUITYCLIP
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_gui_basics
        juce::juce_gui_extra
        FRUITYCLIPData
)

)

target_compile_definitions(FRUITYCLIP PRIVATE JUCE_VST3_CAN_REPLACE_VST2=0)

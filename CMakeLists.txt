cmake_minimum_required(VERSION 3.15)

project(GOREKLIP VERSION 1.0.0)

# JUCE
set(CMAKE_CXX_STANDARD 17)

# Add JUCE
add_subdirectory(JUCE)

# ============================================================
#  Embed images into plugin binary
# ============================================================
juce_add_binary_data(GOREKLIPData
    SOURCES
        Assets/bg.png
        Assets/finger.png
        Assets/gorekliper_logo.png
        Assets/slam.jpg
)

# ============================================================
#  Plugin definition
# ============================================================
juce_add_plugin(GOREKLIP
    COMPANY_NAME "GOREKLIP"
    PLUGIN_NAME "GOREKLIP"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    # Which formats to build
    FORMATS VST3 AU Standalone

    PRODUCT_NAME "GOREKLIP"
)

# ============================================================
#  Plugin Sources
# ============================================================
target_sources(GOREKLIP PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/fruity_knee_lut_8192-2.cpp
)

# ============================================================
#  Linking
# ============================================================
target_link_libraries(GOREKLIP PRIVATE
    GOREKLIPData
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_dsp
    juce::juce_core
)

# Enable VST3
target_compile_definitions(GOREKLIP PRIVATE
    JUCE_VST3_CAN_REPLACE_VST2=0
)

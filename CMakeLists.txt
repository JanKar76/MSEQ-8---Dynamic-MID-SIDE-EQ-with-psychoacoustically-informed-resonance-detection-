cmake_minimum_required(VERSION 3.22)
project(MSEQ8 VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Hämta JUCE. Alternativ: add_subdirectory(path/to/JUCE) om du har JUCE lokalt.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

set(PLUGIN_FORMATS VST3)
if(APPLE)
    list(APPEND PLUGIN_FORMATS AU)
endif()

juce_add_plugin(MSEQ8
    COMPANY_NAME "JanKarlsson"
    PLUGIN_MANUFACTURER_CODE Jkar
    PLUGIN_CODE Ms8q
    FORMATS ${PLUGIN_FORMATS}
    PRODUCT_NAME "MSEQ 8"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Fx EQ
    AU_MAIN_TYPE kAudioUnitType_Effect
)

target_sources(MSEQ8 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/EQGraphComponent.cpp
)

target_compile_definitions(MSEQ8 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(MSEQ8 PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

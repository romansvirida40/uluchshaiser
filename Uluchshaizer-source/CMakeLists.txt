cmake_minimum_required(VERSION 3.22)
project(Uluchshaizer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

# JUCE: use a local checkout if JUCE_DIR is given, otherwise download JUCE 8.0.15.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(ULZ_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND ULZ_FORMATS AU)
endif()

juce_add_plugin(Uluchshaizer
    COMPANY_NAME "Uluchshaizer"
    COMPANY_WEBSITE ""
    BUNDLE_ID "com.uluchshaizer.uluchshaizer"
    PLUGIN_MANUFACTURER_CODE Ulzr
    PLUGIN_CODE Ulz1
    FORMATS ${ULZ_FORMATS}
    PRODUCT_NAME "Uluchshaizer"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Pitch Shift
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(UluchshaizerData SOURCES
    Resources/Fonts/Inter-Regular.ttf
    Resources/Fonts/Inter-Medium.ttf
    Resources/Fonts/Inter-SemiBold.ttf)

if(APPLE)
    set(ULZ_PLATFORM_SRC Source/ui/Platform.mm)
else()
    set(ULZ_PLATFORM_SRC Source/ui/Platform.cpp)
endif()

target_sources(Uluchshaizer PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PresetManager.cpp
    Source/dsp/PitchDetector.cpp
    Source/dsp/PsolaEngine.cpp
    Source/dsp/VocalProcessor.cpp
    ${ULZ_PLATFORM_SRC})

target_compile_definitions(Uluchshaizer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Uluchshaizer
    PRIVATE
        UluchshaizerData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- offline DSP tests (no JUCE needed) --------------------------------------------------
option(ULZ_BUILD_TESTS "Build the offline DSP test" ON)
if(ULZ_BUILD_TESTS)
    add_executable(UluchshaizerDspTest
        tests/dsp_test.cpp
        Source/dsp/PitchDetector.cpp
        Source/dsp/PsolaEngine.cpp
        Source/dsp/VocalProcessor.cpp)

    # loads the built VST3 like a DAW and processes audio through it
    juce_add_console_app(UluchshaizerHostTest PRODUCT_NAME "UluchshaizerHostTest")
    target_sources(UluchshaizerHostTest PRIVATE tests/host_test.cpp)
    target_compile_definitions(UluchshaizerHostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(UluchshaizerHostTest PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags)

    # renders the real editor driven by the real processor (visual regression check)
    juce_add_gui_app(UluchshaizerUiSnapshot PRODUCT_NAME "UluchshaizerUiSnapshot")
    target_sources(UluchshaizerUiSnapshot PRIVATE tests/ui_snapshot.cpp)
    target_link_libraries(UluchshaizerUiSnapshot PRIVATE Uluchshaizer UluchshaizerData juce::juce_audio_formats)

endif()

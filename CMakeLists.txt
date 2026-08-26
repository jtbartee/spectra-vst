cmake_minimum_required(VERSION 3.22)

project(SPECTRASYNTH VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SPECTRA_UNIVERSAL "Build a universal arm64 + x86_64 binary" ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
    if(SPECTRA_UNIVERSAL)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
endif()

# JUCE lives outside this repo so every ported synth shares one checkout.
# Override with -DSPECTRA_JUCE_PATH=/path/to/JUCE if yours is somewhere else.
set(SPECTRA_JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH "Path to a JUCE checkout")
if(NOT EXISTS "${SPECTRA_JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found at ${SPECTRA_JUCE_PATH}.\n"
        "Clone it with:  git clone --depth 1 --branch 8.0.6 https://github.com/juce-framework/JUCE.git\n"
        "or point at an existing checkout with -DSPECTRA_JUCE_PATH=...")
endif()
add_subdirectory("${SPECTRA_JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# The DSP core is deliberately free of JUCE so it can be verified on its own,
# without a host, a soundcard, or a plugin scan.
# ---------------------------------------------------------------------------
add_library(spectra_dsp STATIC
    source/dsp/Presets.cpp
    source/dsp/Wavetables.cpp
    source/dsp/Effects.cpp
    source/dsp/SpectraVoice.cpp
    source/dsp/SpectraEngine.cpp
)
target_include_directories(spectra_dsp PUBLIC source/dsp)
target_compile_features(spectra_dsp PUBLIC cxx_std_17)

# ---------------------------------------------------------------------------
# The plugin
# ---------------------------------------------------------------------------
juce_add_plugin(SpectraSynth
    COMPANY_NAME                "John Bartee"
    BUNDLE_ID                   com.johnbartee.spectra
    PLUGIN_MANUFACTURER_CODE    Jbrt
    PLUGIN_CODE                 Spct
    FORMATS                     AU VST3 Standalone
    PRODUCT_NAME                "SPECTRA"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
)

target_sources(SpectraSynth PRIVATE
    source/plugin/PluginProcessor.cpp
    source/plugin/PluginEditor.cpp
    source/plugin/Parameters.cpp
)

target_compile_definitions(SpectraSynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(SpectraSynth PRIVATE
    spectra_dsp
    juce::juce_audio_utils
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Offline render / verification harness (no JUCE, no audio device needed)
# ---------------------------------------------------------------------------
add_executable(spectra_render tools/render_test.cpp)
target_link_libraries(spectra_render PRIVATE spectra_dsp)

# ---------------------------------------------------------------------------
# End-to-end host harness: loads the built VST3 the way a DAW does and checks
# that notes in produce audio out. Covers the JUCE wrapper, which the DSP
# harness above deliberately knows nothing about.
# ---------------------------------------------------------------------------
juce_add_console_app(spectra_hosttest PRODUCT_NAME "spectra_hosttest")
target_sources(spectra_hosttest PRIVATE tools/host_test.cpp)
target_compile_definitions(spectra_hosttest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
)
target_link_libraries(spectra_hosttest PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
)
add_dependencies(spectra_hosttest SpectraSynth_VST3)

enable_testing()
add_test(NAME dsp_checks COMMAND spectra_render --quick)
add_test(NAME plugin_checks
         COMMAND spectra_hosttest
                 "$<TARGET_FILE_DIR:SpectraSynth_VST3>/../../../SPECTRA.vst3")

cmake_minimum_required(VERSION 3.22)
project(FingerSequencer VERSION 0.7.0 LANGUAGES C CXX)

set(JUCE_PATH "/Users/arthur/Documents/JUCE" CACHE PATH "Existing JUCE source directory")
add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")

juce_add_plugin(FingerSequencer
    COMPANY_NAME "Arthur Audio"
    BUNDLE_ID "com.arthuraudio.fingersequencer"
    PLUGIN_MANUFACTURER_CODE ArAu
    PLUGIN_CODE FgSq
    PRODUCT_NAME "Finger Sequencer"
    FORMATS AU
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    IS_MIDI_EFFECT FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(FingerSequencer PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/SamplerEngine.cpp Source/StretchRenderer.cpp Source/SequencerEngine.cpp Source/MemoryBank.cpp)
juce_add_binary_data(FactoryKit SOURCES Assets/Factory/Kick.wav Assets/Factory/Clap.wav Assets/Factory/Snare.wav Assets/Factory/ClosedHat.wav Assets/Factory/OpenHat.wav Assets/Factory/Shaker.wav Assets/Factory/Tom.wav)
target_link_libraries(FingerSequencer PRIVATE FactoryKit)
target_compile_features(FingerSequencer PUBLIC cxx_std_17)
target_compile_definitions(FingerSequencer PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(FingerSequencer PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

add_executable(SamplerTests)
target_sources(SamplerTests PRIVATE Tests/SamplerTests.cpp)
target_include_directories(SamplerTests PRIVATE "${JUCE_PATH}/modules" Source)
target_compile_definitions(SamplerTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_link_libraries(SamplerTests PRIVATE FingerSequencer)
enable_testing()
add_test(NAME SamplerTests COMMAND SamplerTests)
set_tests_properties(SamplerTests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(SequencerTests Tests/SequencerTests.cpp Source/SequencerEngine.cpp)
target_compile_features(SequencerTests PRIVATE cxx_std_17)
add_test(NAME SequencerTests COMMAND SequencerTests)

add_executable(PlaybackTests Tests/PlaybackTests.cpp)
target_include_directories(PlaybackTests PRIVATE "${JUCE_PATH}/modules" Source)
target_compile_definitions(PlaybackTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_link_libraries(PlaybackTests PRIVATE FingerSequencer)
add_test(NAME PlaybackTests COMMAND PlaybackTests)
set_tests_properties(PlaybackTests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

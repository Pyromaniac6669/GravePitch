cmake_minimum_required(VERSION 3.22)

project(GravePitch VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GRAVEPITCH_BUILD_TESTS "Build GravePitch core tests" ON)
set(GRAVEPITCH_JUCE_PATH "" CACHE PATH "Path to a JUCE source checkout")

add_library(gravepitch_core STATIC
    src/core/Note.cpp
    src/core/PitchDetector.cpp
    src/core/PitchSmoother.cpp
    src/core/ReadoutContinuity.cpp
    src/core/TunerEngine.cpp
    src/core/Tuning.cpp
)

target_include_directories(gravepitch_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(gravepitch_core PUBLIC cxx_std_17)

if(GRAVEPITCH_BUILD_TESTS)
    enable_testing()

    add_executable(gravepitch_core_tests
        tests/core_tests.cpp
    )

    target_link_libraries(gravepitch_core_tests PRIVATE gravepitch_core)

    add_test(NAME gravepitch_core_tests COMMAND gravepitch_core_tests)

    add_executable(gravepitch_realtime_pipeline_tests
        tests/realtime_pipeline_tests.cpp
        src/juce/LatestAnalysisWindowExchange.cpp
    )
    target_include_directories(gravepitch_realtime_pipeline_tests PRIVATE src/juce)
    target_link_libraries(gravepitch_realtime_pipeline_tests PRIVATE gravepitch_core)
    add_test(NAME gravepitch_realtime_pipeline_tests COMMAND gravepitch_realtime_pipeline_tests)
endif()

set(_gravepitch_juce_dir "")

if(GRAVEPITCH_JUCE_PATH)
    get_filename_component(_gravepitch_juce_path "${GRAVEPITCH_JUCE_PATH}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

if(GRAVEPITCH_JUCE_PATH AND EXISTS "${_gravepitch_juce_path}/CMakeLists.txt")
    set(_gravepitch_juce_dir "${_gravepitch_juce_path}")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    set(_gravepitch_juce_dir "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE")
endif()

if(_gravepitch_juce_dir)
    add_subdirectory("${_gravepitch_juce_dir}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")

    set(_gravepitch_icon_big "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/gravepitch_app_icon_1024.png")
    set(_gravepitch_icon_small "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/gravepitch_app_icon_64.png")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_gravepitch_icon_big}"
        "${_gravepitch_icon_small}"
    )

    juce_add_binary_data(GravePitchUiAssets
        SOURCES
            resources/fonts/OswaldVariable.ttf
            resources/ui/gravepitch_logo_2x.png
            resources/ui/in_tune_off_2x.png
            resources/ui/in_tune_on_2x.png
            resources/ui/main_plate_clean_wide_2x.png
            resources/ui/mute_off_2x.png
            resources/ui/mute_on_2x.png
    )

    juce_add_plugin(GravePitchPlugin
        COMPANY_NAME "GravePitch"
        PRODUCT_NAME "GravePitch"
        ICON_BIG "${_gravepitch_icon_big}"
        ICON_SMALL "${_gravepitch_icon_small}"
        PLUGIN_MANUFACTURER_CODE Gptc
        PLUGIN_CODE Gp01
        FORMATS Standalone VST3
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "GravePitch 需要访问音频输入以检测吉他音高。"
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Analyzer
    )

    target_sources(GravePitchPlugin
        PRIVATE
            src/juce/LatestAnalysisWindowExchange.cpp
            src/juce/PluginEditor.cpp
            src/juce/PluginProcessor.cpp
            src/juce/RealtimePitchAnalyzer.cpp
    )

    target_include_directories(GravePitchPlugin
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src/juce
    )

    target_compile_definitions(GravePitchPlugin
        PUBLIC
            JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
    )

    target_link_libraries(GravePitchPlugin
        PRIVATE
            GravePitchUiAssets
            gravepitch_core
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )

    if(GRAVEPITCH_BUILD_TESTS)
        add_executable(gravepitch_realtime_analyzer_tests
            tests/realtime_analyzer_tests.cpp
            src/juce/LatestAnalysisWindowExchange.cpp
            src/juce/RealtimePitchAnalyzer.cpp
        )
        target_include_directories(gravepitch_realtime_analyzer_tests PRIVATE include src/juce)
        target_compile_definitions(gravepitch_realtime_analyzer_tests PRIVATE GRAVEPITCH_ENABLE_TEST_SEAMS=1)
        target_link_libraries(gravepitch_realtime_analyzer_tests PRIVATE gravepitch_core juce::juce_audio_utils)
        add_test(NAME gravepitch_realtime_analyzer_tests COMMAND gravepitch_realtime_analyzer_tests)

        add_executable(gravepitch_plugin_processor_tests
            tests/plugin_processor_tests.cpp
        )
        target_include_directories(gravepitch_plugin_processor_tests PRIVATE include src/juce)
        target_link_libraries(gravepitch_plugin_processor_tests PRIVATE GravePitchPlugin juce::juce_audio_utils)
        add_test(NAME gravepitch_plugin_processor_tests COMMAND gravepitch_plugin_processor_tests)
    endif()

    if(APPLE)
        set_property(TARGET GravePitchPlugin_Standalone GravePitchPlugin_VST3
            APPEND PROPERTY LINK_DEPENDS
                "${_gravepitch_icon_big}"
                "${_gravepitch_icon_small}"
        )

        add_custom_command(TARGET GravePitchPlugin_VST3 POST_BUILD
            COMMAND /usr/bin/codesign --force --sign -
                "$<TARGET_BUNDLE_DIR:GravePitchPlugin_VST3>"
            COMMENT "Ad-hoc signing GravePitch VST3 after manifest generation"
            VERBATIM
        )
    endif()

    if(APPLE AND GRAVEPITCH_BUILD_TESTS)
        add_test(
            NAME gravepitch_vst3_codesign
            COMMAND /usr/bin/codesign --verify --deep --strict
                "$<TARGET_BUNDLE_DIR:GravePitchPlugin_VST3>"
        )
    endif()
else()
    message(STATUS "JUCE not found. Set GRAVEPITCH_JUCE_PATH or place JUCE at external/JUCE to build Standalone/VST3.")
endif()

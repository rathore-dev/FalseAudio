cmake_minimum_required(VERSION 3.22)

project(FalseAudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(external/JUCE)

# ---------------------------------------------------------------------------
# Core library: measurement, analysis, auto-tune and DSP bridge.
# Kept free of UI code so it can be unit-tested headless.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE FA_CORE_SOURCES CONFIGURE_DEPENDS
    src/core/*.cpp src/core/*.h)

add_library(FalseAudioCore STATIC ${FA_CORE_SOURCES})
target_include_directories(FalseAudioCore PUBLIC src)
target_compile_definitions(FalseAudioCore PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_ASIO=0
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1)
target_link_libraries(FalseAudioCore
    PUBLIC
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
if (WIN32)
    target_link_libraries(FalseAudioCore PUBLIC ole32 oleaut32 uiautomationcore)
endif()

# ---------------------------------------------------------------------------
# Desktop application
# ---------------------------------------------------------------------------
juce_add_gui_app(FalseAudio
    PRODUCT_NAME "FalseAudio"
    COMPANY_NAME "FalseAudio"
    VERSION ${PROJECT_VERSION})

juce_generate_juce_header(FalseAudio)

file(GLOB_RECURSE FA_APP_SOURCES CONFIGURE_DEPENDS
    src/app/*.cpp src/app/*.h
    src/ui/*.cpp src/ui/*.h)

target_sources(FalseAudio PRIVATE ${FA_APP_SOURCES})
target_link_libraries(FalseAudio PRIVATE FalseAudioCore)

# ---------------------------------------------------------------------------
# Headless tests for the analysis maths (synthetic systems, no hardware).
# ---------------------------------------------------------------------------
juce_add_console_app(FalseAudioTests PRODUCT_NAME "FalseAudioTests")
file(GLOB_RECURSE FA_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
target_sources(FalseAudioTests PRIVATE ${FA_TEST_SOURCES})
target_link_libraries(FalseAudioTests PRIVATE FalseAudioCore)

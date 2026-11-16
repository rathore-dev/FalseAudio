cmake_minimum_required(VERSION 3.22)

# Static MSVC runtime (single self-contained exe). Must be set before project() and before JUCE is added.
cmake_policy(SET CMP0091 NEW)
project(FalseAudio VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(external/JUCE)

# Source files and string literals are UTF-8 (juce::CharPointer_UTF8 wraps non-ASCII literals).
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ---------------------------------------------------------------------------
# Embedded data: the HouseCurves target collection (full curves + per-band sums).
# ---------------------------------------------------------------------------
file(GLOB_RECURSE FA_HOUSE_CURVES CONFIGURE_DEPENDS HouseCurves/*.txt)
# The gear database (data/gear/**/*.json) is zipped at build time and embedded as "gear_zip".
# Any JSON change re-packs the zip (and relinks) automatically.
file(GLOB_RECURSE FA_GEAR_JSON CONFIGURE_DEPENDS data/gear/*.json)
set(FA_GEAR_ZIP ${CMAKE_BINARY_DIR}/embedded/gear_zip.zip)
add_custom_command(
    OUTPUT ${FA_GEAR_ZIP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/embedded
    COMMAND ${CMAKE_COMMAND} -E rm -f ${FA_GEAR_ZIP}
    COMMAND ${CMAKE_COMMAND} -E tar cf ${FA_GEAR_ZIP} --format=zip -- .
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/data/gear
    DEPENDS ${FA_GEAR_JSON}
    COMMENT "Packing data/gear into embedded zip"
    VERBATIM)
juce_add_binary_data(FalseAudioData
    HEADER_NAME FalseAudioData.h
    NAMESPACE FalseAudioData
    SOURCES ${FA_HOUSE_CURVES} ${FA_GEAR_ZIP})

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
    PRIVATE
        FalseAudioData
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

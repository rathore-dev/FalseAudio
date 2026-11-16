cmake_minimum_required(VERSION 3.22)

# Static MSVC runtime (single self-contained exe). Must be set before project() and before JUCE is added.
cmake_policy(SET CMP0091 NEW)
# CMP0141: debug info format is chosen by CMAKE_MSVC_DEBUG_INFORMATION_FORMAT. Embedded (/Z7) is
# required for sccache (no shared .pdb) and is also friendlier to parallel builds.
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
endif()
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
# fa_juce: ALL JUCE module sources compiled ONCE into a static library.
# JUCE module targets are INTERFACE libraries that carry .cpp files, so every target linking them
# would recompile them. Here the modules are linked PRIVATE (sources compile only into fa_juce) and
# their include dirs / compile definitions / options are re-exported PUBLIC to consumers by hand.
# JUCE_STANDALONE_APPLICATION=1 is shared by the GUI app and the console tests (the tests use a plain
# main(), so one build of the modules serves both).
# ---------------------------------------------------------------------------
set(FA_JUCE_MODULES
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_gui_extra)

# Transitive closure of module targets (INTERFACE_LINK_LIBRARIES walk).
function(fa_collect_juce_targets out)
    set(todo ${ARGN})
    set(seen)
    while(todo)
        list(POP_FRONT todo t)
        if(NOT TARGET ${t})
            continue()
        endif()
        get_target_property(real ${t} ALIASED_TARGET)
        if(real)
            set(t ${real})
        endif()
        if(t IN_LIST seen)
            continue()
        endif()
        list(APPEND seen ${t})
        get_target_property(deps ${t} INTERFACE_LINK_LIBRARIES)
        if(deps)
            foreach(d IN LISTS deps)
                if(d MATCHES "^[$]<LINK_ONLY:(.*)>[$]$")
                    set(d "${CMAKE_MATCH_1}")
                endif()
                if(d MATCHES "^(juce::|juce_)")
                    list(APPEND todo ${d})
                endif()
            endforeach()
        endif()
    endwhile()
    set(${out} ${seen} PARENT_SCOPE)
endfunction()

fa_collect_juce_targets(FA_JUCE_ALL ${FA_JUCE_MODULES})

add_library(fa_juce STATIC)
target_link_libraries(fa_juce PRIVATE ${FA_JUCE_MODULES})
target_compile_definitions(fa_juce PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_ASIO=0
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_STANDALONE_APPLICATION=1)
# Re-export (PUBLIC, not via link) the usage requirements of every JUCE module target.
foreach(t IN LISTS FA_JUCE_ALL)
    foreach(kind COMPILE_DEFINITIONS INCLUDE_DIRECTORIES COMPILE_OPTIONS)
        get_target_property(v ${t} INTERFACE_${kind})
        if(v)
            if(kind STREQUAL "COMPILE_DEFINITIONS")
                target_compile_definitions(fa_juce PUBLIC ${v})
            elseif(kind STREQUAL "INCLUDE_DIRECTORIES")
                target_include_directories(fa_juce SYSTEM PUBLIC ${v})
            else()
                target_compile_options(fa_juce PUBLIC ${v})
            endif()
        endif()
    endforeach()
endforeach()
# Flags-only INTERFACE libraries (no sources), safe to link PUBLIC.
target_link_libraries(fa_juce PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if (WIN32)
    target_link_libraries(fa_juce PUBLIC ole32 oleaut32 uiautomationcore)
endif()

# Precompiled headers: the JUCE headers the project actually pulls in + hot std headers.
set(FA_PCH_HEADERS
    <juce_audio_utils/juce_audio_utils.h>
    <juce_audio_devices/juce_audio_devices.h>
    <juce_dsp/juce_dsp.h>
    <juce_gui_extra/juce_gui_extra.h>
    <algorithm> <array> <atomic> <cmath> <complex> <cstdint> <functional> <map>
    <memory> <mutex> <optional> <string> <thread> <unordered_map> <vector>)
option(FA_USE_PCH "Use precompiled headers" ON)

# ---------------------------------------------------------------------------
# Core library: measurement, analysis, auto-tune and DSP bridge.
# Kept free of UI code so it can be unit-tested headless.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE FA_CORE_SOURCES CONFIGURE_DEPENDS
    src/core/*.cpp src/core/*.h)

add_library(FalseAudioCore STATIC ${FA_CORE_SOURCES})
target_include_directories(FalseAudioCore PUBLIC src)
target_link_libraries(FalseAudioCore
    PRIVATE
        FalseAudioData
    PUBLIC
        fa_juce)
if(FA_USE_PCH)
    target_precompile_headers(FalseAudioCore PRIVATE ${FA_PCH_HEADERS})
endif()

# ---------------------------------------------------------------------------
# Desktop application
# ---------------------------------------------------------------------------
juce_add_gui_app(FalseAudio
    PRODUCT_NAME "FalseAudio"
    COMPANY_NAME "FalseAudio"
    VERSION ${PROJECT_VERSION}
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/FalseAudio_1024.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/FalseAudio_256.png"
    DESCRIPTION "FalseAudio automated car audio tuning")

juce_generate_juce_header(FalseAudio)

file(GLOB_RECURSE FA_APP_SOURCES CONFIGURE_DEPENDS
    src/app/*.cpp src/app/*.h
    src/ui/*.cpp src/ui/*.h)

target_sources(FalseAudio PRIVATE ${FA_APP_SOURCES})
target_link_libraries(FalseAudio PRIVATE FalseAudioCore)
if(FA_USE_PCH)
    target_precompile_headers(FalseAudio PRIVATE ${FA_PCH_HEADERS})
endif()

# Dev/QA automation hook (--qa-script). OFF in release/packaged builds.
option(FA_QA_HOOKS "Compile the --qa-script automation hook" OFF)
target_compile_definitions(FalseAudio PRIVATE FA_QA_HOOKS=$<BOOL:${FA_QA_HOOKS}>)

# ---------------------------------------------------------------------------
# Headless tests for the analysis maths (synthetic systems, no hardware).
# ---------------------------------------------------------------------------
juce_add_console_app(FalseAudioTests PRODUCT_NAME "FalseAudioTests")
file(GLOB_RECURSE FA_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
target_sources(FalseAudioTests PRIVATE ${FA_TEST_SOURCES})
target_link_libraries(FalseAudioTests PRIVATE FalseAudioCore)
if(FA_USE_PCH)
    target_precompile_headers(FalseAudioTests PRIVATE ${FA_PCH_HEADERS})
endif()

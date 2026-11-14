cmake_minimum_required(VERSION 3.24)

project(ArrangerMidiDaw
    VERSION 0.0.7
    DESCRIPTION "Arranger MIDI DAW"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# The core and engine layers never depend on JUCE. Turn the app off to build
# and test them alone; CI does this to prove the rule holds.
option(ARRANGER_BUILD_APP   "Build the JUCE desktop app"        ON)
option(ARRANGER_BUILD_TESTS "Build the unit tests (Catch2)"     ON)
option(ARRANGER_BUILD_TEST_PLUGINS "Build two small VST3 plugins to try plugin hosting with" OFF)
option(ARRANGER_WITH_WHISPER "Build lyrics transcription (whisper.cpp) into the app" ON)
option(ARRANGER_WARNINGS_AS_ERRORS "Treat warnings as errors in our code" OFF)

include(FetchContent)
include(cmake/Warnings.cmake)

# nlohmann/json: plain C++ JSON, used by the core for the project file.
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)


add_subdirectory(src/core)
add_subdirectory(src/engine)
add_subdirectory(src/soundfont)

# yaml-cpp: reads device profiles (resources/devices). Fetched after
# FluidSynth, whose unguarded "uninstall" target would clash with it.
FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG yaml-cpp-0.9.0
    GIT_SHALLOW TRUE)
set(YAML_CPP_BUILD_TESTS OFF CACHE INTERNAL "")
set(YAML_CPP_BUILD_TOOLS OFF CACHE INTERNAL "")
set(YAML_CPP_BUILD_CONTRIB OFF CACHE INTERNAL "")
set(YAML_CPP_INSTALL OFF CACHE INTERNAL "")
set(YAML_BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(yaml-cpp)

if(ARRANGER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(ARRANGER_BUILD_APP)
    # Point ARRANGER_JUCE_DIR at a local JUCE checkout to skip the download.
    set(ARRANGER_JUCE_DIR "" CACHE PATH "Local JUCE checkout (optional)")
    if(ARRANGER_JUCE_DIR)
        add_subdirectory(${ARRANGER_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.15
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()
    if(ARRANGER_WITH_WHISPER)
        add_subdirectory(src/whisper)
    endif()
    add_subdirectory(src/app)
    add_subdirectory(src/mcp) # after the app: it copies itself next to it
    if(ARRANGER_BUILD_TEST_PLUGINS)
        add_subdirectory(tests/plugins)
    endif()
endif()

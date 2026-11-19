FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(arranger_tests
    AuditRegressionTests.cpp
    TimeTests.cpp
    VersionTests.cpp
    RecentFilesTests.cpp
    ProjectFileTests.cpp
    AutosaveTests.cpp
    ProjectRoundTripTests.cpp
    UndoRedoTests.cpp
    FileEdgeCaseTests.cpp
    MetronomeTests.cpp
    EngineTests.cpp
    EngineStressTests.cpp
    SongTests.cpp
    MidiFileTests.cpp
    SequencerTests.cpp
    SoundFontTests.cpp
    DeviceProfileTests.cpp
    SynthEditorTests.cpp
    PsrExportTests.cpp
    ControllerProfileTests.cpp
    SampleTests.cpp
    DrumSamplerTests.cpp
    SamplerTrackTests.cpp
    AudioTrackTests.cpp
    RecorderImportTests.cpp
    RackTests.cpp
    AutomationTests.cpp
    ClipGridTests.cpp
    ClipboardTests.cpp
    ChordInputTests.cpp
    KidsModeTests.cpp
    ChordTests.cpp
    AudioAnalysisTests.cpp
    TheoryTests.cpp
    ArrangingTests.cpp
    ClarifyTests.cpp
    SkillsTests.cpp
    McpTests.cpp
    MidiAiTests.cpp
    AssistantTests.cpp
    StyleFileTests.cpp
    StyleFromAudioTests.cpp
    LinkAudioTests.cpp
    MusicXmlTests.cpp
    NotationTests.cpp
    GrooveTests.cpp
    SongFormTests.cpp
    CrashReport.cpp)

target_link_libraries(arranger_tests PRIVATE arranger::core arranger::engine arranger::soundfont Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
arranger_set_warnings(arranger_tests)
target_compile_definitions(arranger_tests PRIVATE ARRANGER_DEVICES_DIR="${PROJECT_SOURCE_DIR}/resources/devices"
    ARRANGER_CONTROLLERS_DIR="${PROJECT_SOURCE_DIR}/resources/controllers"
    ARRANGER_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")

include(CTest)
include(Catch)
catch_discover_tests(arranger_tests)

# The audio thread must not allocate: a test program with its own operator new.
# Not in sanitizer builds, which replace operator new themselves.
if(NOT ARRANGER_SANITIZE)
    add_executable(arranger_realtime_tests RealtimeTests.cpp)
    target_link_libraries(arranger_realtime_tests PRIVATE arranger::core arranger::engine Catch2::Catch2WithMain)
    arranger_set_warnings(arranger_realtime_tests)
    target_compile_definitions(arranger_realtime_tests PRIVATE ARRANGER_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")
    catch_discover_tests(arranger_realtime_tests)
endif()

# Fuzzer for the file readers and the MCP server (see tests/fuzz/FuzzMain.cpp).
# Each target runs briefly with every test run, and replays the inputs that once
# failed (tests/fuzz/regressions/<target>). Nightly CI runs it longer.
add_executable(arranger_fuzz fuzz/FuzzMain.cpp fuzz/FuzzTargets.cpp)
target_link_libraries(arranger_fuzz PRIVATE arranger::core nlohmann_json::nlohmann_json)
arranger_set_warnings(arranger_fuzz)
target_compile_definitions(arranger_fuzz PRIVATE ARRANGER_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    ARRANGER_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")
set(ARRANGER_FUZZ_SECONDS 5 CACHE STRING "Seconds per fuzz target in a normal test run")
foreach(target project midi musicxml style wav device controller l6max mcp)
    add_test(NAME "fuzz ${target}" COMMAND arranger_fuzz ${target} --seconds ${ARRANGER_FUZZ_SECONDS} --seed 1
        --out "${CMAKE_CURRENT_BINARY_DIR}/fuzz-findings")
    add_test(NAME "fuzz ${target} regressions" COMMAND arranger_fuzz ${target}
        --replay "${PROJECT_SOURCE_DIR}/tests/fuzz/regressions/${target}")
endforeach()

if(ARRANGER_BUILD_APP)
    add_executable(arranger_async_tests MessageThreadTests.cpp)
    target_include_directories(arranger_async_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
    target_compile_definitions(arranger_async_tests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(arranger_async_tests PRIVATE Catch2::Catch2WithMain juce::juce_events)
    arranger_set_warnings(arranger_async_tests)
    catch_discover_tests(arranger_async_tests)

    add_executable(arranger_media_tests VideoAudioTests.cpp ${PROJECT_SOURCE_DIR}/src/app/AudioFiles.cpp)
    target_include_directories(arranger_media_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
    target_compile_definitions(arranger_media_tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
        ARRANGER_MEDIA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
    target_link_libraries(arranger_media_tests PRIVATE Catch2::Catch2WithMain arranger::core juce::juce_audio_formats)
    # No strict warnings here: this target compiles JUCE audio modules (FLAC) and app code that
    # includes JUCE headers, which fail -Wconversion/-Werror on GCC and /WX on MSVC.
    catch_discover_tests(arranger_media_tests)
endif()

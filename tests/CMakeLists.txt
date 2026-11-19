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
    MetronomeTests.cpp
    EngineTests.cpp
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
    SongFormTests.cpp
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
    CrashReport.cpp)

target_link_libraries(arranger_tests PRIVATE arranger::core arranger::engine arranger::soundfont Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
arranger_set_warnings(arranger_tests)
target_compile_definitions(arranger_tests PRIVATE ARRANGER_DEVICES_DIR="${PROJECT_SOURCE_DIR}/resources/devices"
    ARRANGER_CONTROLLERS_DIR="${PROJECT_SOURCE_DIR}/resources/controllers"
    ARRANGER_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")

include(CTest)
include(Catch)
catch_discover_tests(arranger_tests)

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

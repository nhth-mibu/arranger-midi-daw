FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(arranger_tests
    TimeTests.cpp
    ProjectFileTests.cpp
    MetronomeTests.cpp
    EngineTests.cpp
    SongTests.cpp
    MidiFileTests.cpp
    SequencerTests.cpp
    SoundFontTests.cpp
    DeviceProfileTests.cpp
    ControllerProfileTests.cpp
    SampleTests.cpp
    DrumSamplerTests.cpp
    SamplerTrackTests.cpp
    AudioTrackTests.cpp
    RackTests.cpp
    AutomationTests.cpp
    ClipGridTests.cpp
    ClipboardTests.cpp
    ChordTests.cpp
    McpTests.cpp
    AssistantTests.cpp
    StyleFileTests.cpp
    MusicXmlTests.cpp
    NotationTests.cpp
    CrashReport.cpp)

target_link_libraries(arranger_tests PRIVATE arranger::core arranger::engine arranger::soundfont Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
arranger_set_warnings(arranger_tests)
target_compile_definitions(arranger_tests PRIVATE ARRANGER_DEVICES_DIR="${PROJECT_SOURCE_DIR}/resources/devices"
    ARRANGER_CONTROLLERS_DIR="${PROJECT_SOURCE_DIR}/resources/controllers")

include(CTest)
include(Catch)
catch_discover_tests(arranger_tests)

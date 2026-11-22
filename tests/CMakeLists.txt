FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(arranger_tests
    AuditFixCoreMcpTests.cpp
    AuditRegressionTests.cpp
    AuditFixParsersTests.cpp
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
    RenderTests.cpp
    TimingTests.cpp
    ExchangeRoundTripTests.cpp
    DeviceProfileTests.cpp
    DeviceChartTests.cpp
    SynthEditorTests.cpp
    SysexFileTests.cpp
    D05ReverbTests.cpp
    MusicFinderTests.cpp
    PlaylistTests.cpp
    PsrExportTests.cpp
    PsrStyleTests.cpp
    PsrFileRulesTests.cpp
    ControllerProfileTests.cpp
    MidiInputPolicyTests.cpp
    MidiPanicTests.cpp
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
    McpArgumentTests.cpp
    McpToolGapTests.cpp
    MidiAiTests.cpp
    AssistantTests.cpp
    AuditFixAppTests.cpp
    StyleFileTests.cpp
    StyleSampleTests.cpp
    StyleFromAudioTests.cpp
    LinkAudioTests.cpp
    MusicXmlTests.cpp
    NotationTests.cpp
    ScoreSnapshotTests.cpp
    GrooveTests.cpp
    CrashReport.cpp)

target_link_libraries(arranger_tests PRIVATE arranger::core arranger::engine arranger::soundfont Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
target_include_directories(arranger_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/platform/include)
arranger_set_warnings(arranger_tests)
# Tests keep Engines (~330 KB each) on the stack. Give Windows the 8 MB stack that macOS and Linux have.
if(MSVC)
    target_link_options(arranger_tests PRIVATE /STACK:8388608)
endif()
target_compile_definitions(arranger_tests PRIVATE ARRANGER_DEVICES_DIR="${PROJECT_SOURCE_DIR}/resources/devices"
    ARRANGER_CONTROLLERS_DIR="${PROJECT_SOURCE_DIR}/resources/controllers"
    ARRANGER_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples"
    ARRANGER_TESTS_DIR="${PROJECT_SOURCE_DIR}/tests")

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

if(ARRANGER_BUILD_APP AND ARRANGER_BUILD_TEST_PLUGINS)
    # The plugin host with the two test VST3 plugins, loaded as the app loads plugins.
    add_executable(arranger_plugin_tests PluginHostTests.cpp ${PROJECT_SOURCE_DIR}/src/app/plugins/PluginHost.cpp)
    target_include_directories(arranger_plugin_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
    target_compile_definitions(arranger_plugin_tests PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        ARRANGER_TEST_GAIN_VST3="$<GENEX_EVAL:$<TARGET_PROPERTY:ArrangerTestGain_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>"
        ARRANGER_TEST_TONE_VST3="$<GENEX_EVAL:$<TARGET_PROPERTY:ArrangerTestTone_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>"
        ARRANGER_TEST_MISBEHAVE_VST3="$<GENEX_EVAL:$<TARGET_PROPERTY:ArrangerTestMisbehave_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
    target_link_libraries(arranger_plugin_tests PRIVATE Catch2::Catch2 arranger::core arranger::engine
        juce::juce_audio_processors juce::juce_gui_basics juce::juce_recommended_config_flags)
    add_dependencies(arranger_plugin_tests ArrangerTestGain_VST3 ArrangerTestTone_VST3 ArrangerTestMisbehave_VST3)
    target_sources(arranger_plugin_tests PRIVATE PluginTestsMain.cpp)
    catch_discover_tests(arranger_plugin_tests)
endif()

if(ARRANGER_BUILD_APP AND UNIX AND NOT APPLE)
    # The UI smoke test: every menu command in every view, on each example song (scripts/ui-smoke-test.sh).
    # Only where Xvfb is installed; on macOS and Windows native file dialogs would wait for a click.
    find_program(ARRANGER_XVFB_RUN xvfb-run)
    if(ARRANGER_XVFB_RUN)
        add_test(NAME ui_smoke_test COMMAND ${PROJECT_SOURCE_DIR}/scripts/ui-smoke-test.sh $<TARGET_FILE:ArrangerMidiDaw>)
        set_tests_properties(ui_smoke_test PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
    endif()
endif()

if(ARRANGER_BUILD_APP)
    add_executable(arranger_localization_tests
        LocalizationTests.cpp
        ${PROJECT_SOURCE_DIR}/src/app/Localization.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/AppTooltip.cpp)
    target_include_directories(arranger_localization_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
    target_compile_definitions(arranger_localization_tests PRIVATE
        ARRANGER_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        ARRANGER_LANGUAGES_DIR="${PROJECT_SOURCE_DIR}/resources/languages"
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(arranger_localization_tests PRIVATE
        Catch2::Catch2WithMain arranger_resources juce::juce_gui_basics)
    # No strict warnings here: this target compiles JUCE GUI code, whose macOS SDK
    # deprecations are warnings, like the media test target.
    catch_discover_tests(arranger_localization_tests)

    add_executable(arranger_async_tests MessageThreadTests.cpp)
    target_include_directories(arranger_async_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
    target_compile_definitions(arranger_async_tests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(arranger_async_tests PRIVATE Catch2::Catch2WithMain juce::juce_events)
    arranger_set_warnings(arranger_async_tests)
    catch_discover_tests(arranger_async_tests)

    add_executable(arranger_audit_app_tests
        AuditFixAppTests.cpp
        ${PROJECT_SOURCE_DIR}/src/app/AssistantChat.cpp
        ${PROJECT_SOURCE_DIR}/src/app/AudioAnalysis.cpp
        ${PROJECT_SOURCE_DIR}/src/app/AudioFiles.cpp
        ${PROJECT_SOURCE_DIR}/src/app/Grooves.cpp
        ${PROJECT_SOURCE_DIR}/src/app/McpService.cpp
        ${PROJECT_SOURCE_DIR}/src/app/MidiAiService.cpp
        ${PROJECT_SOURCE_DIR}/src/app/PythonTools.cpp
        ${PROJECT_SOURCE_DIR}/src/app/Session.cpp
        ${PROJECT_SOURCE_DIR}/src/app/Speech.cpp
        ${PROJECT_SOURCE_DIR}/src/app/StyleFromAudio.cpp
        ${PROJECT_SOURCE_DIR}/src/app/plugins/PluginHost.cpp
        ${PROJECT_SOURCE_DIR}/src/platform/juce/JuceDeviceHost.cpp)
    target_include_directories(arranger_audit_app_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/app
        ${PROJECT_SOURCE_DIR}/src/platform/include)
    target_compile_definitions(arranger_audit_app_tests PRIVATE
        ARRANGER_HAS_APP_TESTS=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_PLUGINHOST_VST3=1
        $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>)
    target_link_libraries(arranger_audit_app_tests PRIVATE
        Catch2::Catch2WithMain
        arranger::core
        arranger::engine
        arranger::soundfont
        arranger_resources
        nlohmann_json::nlohmann_json
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_gui_basics)
    if(TARGET arranger::whisper)
        target_link_libraries(arranger_audit_app_tests PRIVATE arranger::whisper)
    endif()
    if(MSVC)
        target_link_options(arranger_audit_app_tests PRIVATE /STACK:8388608)
    endif()
    catch_discover_tests(arranger_audit_app_tests)

    add_executable(arranger_media_tests VideoAudioTests.cpp ${PROJECT_SOURCE_DIR}/src/app/AudioFiles.cpp)
    target_include_directories(arranger_media_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
    target_compile_definitions(arranger_media_tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
        ARRANGER_MEDIA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
    target_link_libraries(arranger_media_tests PRIVATE Catch2::Catch2WithMain arranger::core juce::juce_audio_formats)
    # No strict warnings here: this target compiles JUCE audio modules (FLAC) and app code that
    # includes JUCE headers, which fail -Wconversion/-Werror on GCC and /WX on MSVC.
    catch_discover_tests(arranger_media_tests)

    add_executable(arranger_audit_ui_tests
        AuditFixUiTests.cpp
        ${PROJECT_SOURCE_DIR}/src/app/Localization.cpp
        ${PROJECT_SOURCE_DIR}/src/app/UpdateChecker.cpp
        ${PROJECT_SOURCE_DIR}/src/app/Session.cpp
        ${PROJECT_SOURCE_DIR}/src/app/McpService.cpp
        ${PROJECT_SOURCE_DIR}/src/app/AssistantChat.cpp
        ${PROJECT_SOURCE_DIR}/src/app/AudioFiles.cpp
        ${PROJECT_SOURCE_DIR}/src/app/GmSoundFont.cpp
        ${PROJECT_SOURCE_DIR}/src/app/AudioAnalysis.cpp
        ${PROJECT_SOURCE_DIR}/src/app/StyleFromAudio.cpp
        ${PROJECT_SOURCE_DIR}/src/app/Grooves.cpp
        ${PROJECT_SOURCE_DIR}/src/app/PythonTools.cpp
        ${PROJECT_SOURCE_DIR}/src/app/Speech.cpp
        ${PROJECT_SOURCE_DIR}/src/app/FileDialogs.cpp
        ${PROJECT_SOURCE_DIR}/src/app/MidiAiService.cpp
        ${PROJECT_SOURCE_DIR}/src/app/plugins/PluginHost.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/Theme.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/AppLookAndFeel.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/AppTooltip.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/HeaderBar.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/Panels.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/TimelineViews.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/PianoRoll.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/MixerView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/ChatView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/BeatView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/ScoreView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/ClipGridView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/StyleRuleEditor.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/SlicerView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/MainComponent.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/PluginViews.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/SoundEditorView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/L6maxMixerView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/PsrStyleView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/AnalysisView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/AiToolsView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/ArrangeView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/SongFormView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/ChordsView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/KidsView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/StyleFromAudioView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/GrooveView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/AiPanel.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/MidiMapView.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ui/SpeechView.cpp
        ${PROJECT_SOURCE_DIR}/src/platform/juce/JuceDeviceHost.cpp)
    target_include_directories(arranger_audit_ui_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/app
        ${PROJECT_SOURCE_DIR}/src/platform/include)
    target_compile_definitions(arranger_audit_ui_tests PRIVATE
        ARRANGER_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        ARRANGER_LANGUAGES_DIR="${PROJECT_SOURCE_DIR}/resources/languages"
        JUCE_WEB_BROWSER=0
        JUCE_PLUGINHOST_VST3=1
        $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_APPLICATION_NAME_STRING="Lazy Arranger Midi DAW"
        JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(arranger_audit_ui_tests PRIVATE
        Catch2::Catch2WithMain
        arranger::core
        arranger::engine
        arranger::soundfont
        arranger_resources
        nlohmann_json::nlohmann_json
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_gui_basics)
    if(TARGET arranger::whisper)
        target_link_libraries(arranger_audit_ui_tests PRIVATE arranger::whisper)
    endif()
    if(MSVC)
        target_link_options(arranger_audit_ui_tests PRIVATE /STACK:8388608)
    endif()
    catch_discover_tests(arranger_audit_ui_tests)
endif()

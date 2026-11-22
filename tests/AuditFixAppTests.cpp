#include "arranger/core/Assistant.h"
#include "arranger/core/Document.h"
#include "arranger/core/McpServer.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#if defined(ARRANGER_HAS_APP_TESTS)
 #include "AssistantChat.h"
 #include "McpService.h"
 #include "MessageThreadTask.h"
 #include "MidiAiService.h"
 #include "PythonTools.h"
 #include "Session.h"
 #include "Speech.h"
 #include "StyleFromAudio.h"
 #include "TestSoundFont.h"

 #if JUCE_MAC || JUCE_LINUX
  #include <sys/stat.h>
 #endif

 #include <atomic>
 #include <chrono>
 #include <future>
 #include <thread>
#endif

using namespace arranger;
using namespace arranger::assistant;
using nlohmann::json;

#if ! defined(ARRANGER_HAS_APP_TESTS)

namespace
{
    struct DummyMcpHost final : mcp::Host
    {
        Document doc;
        const Song& song() const override { return doc.song(); }
        std::uint64_t songRevision() const override { return doc.revision(); }
        void edit (const std::string& d, const std::function<void (Song&)>& fn) override { doc.edit (d, fn); }
    };
}

TEST_CASE ("C5: shellQuote and shellCommand prevent cmd.exe injection and TOML quote breakout", "[audit][c5][assistant]")
{
    // Newlines (\r and \n) are stripped so command separators cannot be injected.
    CHECK (shellQuote ("safe\r\n& calc.exe", true).find ('\n') == std::string::npos);
    CHECK (shellQuote ("safe\r\n& calc.exe", true).find ('\r') == std::string::npos);
    CHECK (shellQuote ("safe\n; rm -rf /", false).find ('\n') == std::string::npos);

    // Windows cmd.exe delimiters '=' and ',' are quoted on Windows.
    CHECK (shellQuote ("a=b", true) == "\"a=b\"");
    CHECK (shellQuote ("a,b", true) == "\"a,b\"");
    CHECK (shellQuote ("a=b", false) == "a=b");

    // Embedded quotes on Windows use balanced "" so cmd.exe never leaves quote mode.
    const auto quotedInj = shellQuote ("x\" & calc.exe & \"y", true);
    CHECK (quotedInj == "\"x\"\" & calc.exe & \"\"y\"");

    // Environment variable expansion %VAR% is disarmed via "^%" outside quotes.
    const auto quotedEnv = shellQuote ("%COMSPEC%", true);
    CHECK (quotedEnv.find ("%COMSPEC%") == std::string::npos);
    CHECK (quotedEnv == "\"\"^%\"COMSPEC\"^%\"\"");

    // Session ID validation rejects shell metacharacters and leading-dash flags.
    CHECK (validSessionId ("abc-123_45.6:7"));
    CHECK_FALSE (validSessionId (""));
    CHECK_FALSE (validSessionId ("--dangerously-skip-permissions"));
    CHECK_FALSE (validSessionId ("s1\" & calc.exe"));
    CHECK_FALSE (validSessionId ("s1\ncalc"));
    CHECK_FALSE (validSessionId ("s1;id"));

    const auto programs = builtInPrograms();
    REQUIRE (programs.size() >= 2);
    const auto& claude = programs[0];
    const auto& codex = programs[1];

    // Invalid session IDs do not activate resumeArgs.
    const auto noResume = arguments (claude, { { "mcp_config", "m.json" }, { "session", "--malicious-flag" } });
    CHECK (std::find (noResume.begin(), noResume.end(), "--resume") == noResume.end());

    // Single quotes in {helper} at Assistant.cpp:154 are safely escaped into a TOML basic string.
    const auto codexWords = arguments (codex, { { "helper", "C:\\Users\\O'Brien\\arranger-mcp.exe" } });
    CHECK (std::find (codexWords.begin(), codexWords.end(),
                      "mcp_servers.arranger-midi-daw.command=\"C:\\\\Users\\\\O'Brien\\\\arranger-mcp.exe\"")
           != codexWords.end());
}

TEST_CASE ("H11: OutputReader and ApiChat tolerate malformed and non-standard JSON shapes", "[audit][h11][assistant]")
{
    SECTION ("OutputReader rejects bad session IDs and handles non-bool is_error and missing fields")
    {
        OutputReader claude (Program::Output::claudeStreamJson);
        const auto initEvents = claude.line (R"({"type":"system","subtype":"init","session_id":"bad\"id"})");
        CHECK (initEvents.empty());

        const auto msgEvents = claude.line (R"({"type":"assistant","message":{"content":[123,{"type":"tool_use","name":null},{"type":"text","text":null}]}})");
        CHECK (msgEvents.empty());

        const auto resEvents = claude.line (R"({"type":"result","subtype":"success","is_error":"true","result":"string bool error","session_id":"--bad"})");
        REQUIRE (resEvents.size() == 1);
        CHECK (resEvents[0].kind == Event::Kind::error);
        CHECK (resEvents[0].value == "string bool error");

        OutputReader claudeNullErr (Program::Output::claudeStreamJson);
        const auto okEvents = claudeNullErr.line (R"({"type":"result","subtype":"success","is_error":null,"result":"ok","session_id":"good_1"})");
        REQUIRE (okEvents.size() == 3);
        CHECK (okEvents[0].kind == Event::Kind::session);
        CHECK (okEvents[0].value == "good_1");
        CHECK (okEvents[1].kind == Event::Kind::text);
        CHECK (okEvents[2].kind == Event::Kind::done);

        OutputReader codex (Program::Output::codexJson);
        CHECK (codex.line (R"({"type":"thread.started","thread_id":"bad id"})").empty());
        CHECK (codex.line (R"({"type":"item.started","item":null})").empty());
        CHECK (codex.line (R"({"msg":{"type":"mcp_tool_call_begin","invocation":null}})").empty());
    }

    SECTION ("ApiChat handles Ollama object arguments, missing tool_use.name, and malformed choices")
    {
        DummyMcpHost host;
        mcp::Server server (host);
        const auto runTool = [&] (const std::string& name, const std::string& args) { return server.call (name, args); };

        // Ollama-style object arguments (not string) in OpenAI mode.
        ApiChat ollama ({ ApiSettings::Provider::openAi, "http://localhost:11434/v1", "", "llama3" }, server.toolList());
        int step = 0;
        std::vector<Event> events;
        ollama.ask (
            "add bass",
            [&] (const HttpRequest&) -> HttpResponse {
                ++step;
                if (step == 1)
                    return { 200, R"({"choices":[{"message":{"role":"assistant","tool_calls":[{"id":"c1","type":"function","function":{"name":"add_track","arguments":{"name":"OllamaBass","sound":"fingered bass"}}}]}}]})", {} };
                return { 200, R"({"choices":[{"message":{"role":"assistant","content":"Done."}}]})", {} };
            },
            runTool,
            [&] (const Event& e) { events.push_back (e); });

        REQUIRE (host.song().tracks.size() == 1);
        CHECK (host.song().tracks[0].name == "OllamaBass");
        CHECK (events.back().kind == Event::Kind::done);

        // Anthropic response with missing tool_use.name and non-array content does not throw.
        ApiChat anthropic ({ ApiSettings::Provider::anthropic, "", "sk-test", "" }, server.toolList());
        step = 0;
        events.clear();
        anthropic.ask (
            "broken tool_use",
            [&] (const HttpRequest&) -> HttpResponse {
                ++step;
                if (step == 1)
                    return { 200, R"({"stop_reason":"tool_use","content":[{"type":"tool_use","id":"tu1","input":null}]})", {} };
                return { 200, R"({"stop_reason":"end_turn","content":[{"type":"text","text":"Recovered."}]})", {} };
            },
            runTool,
            [&] (const Event& e) { events.push_back (e); });
        REQUIRE (! events.empty());
        CHECK (events.back().kind == Event::Kind::done);
    }
}

#else // ARRANGER_HAS_APP_TESTS

namespace
{
    struct NullDeviceHost final : platform::DeviceHost
    {
        platform::AudioStatus audioStatus() const override { return {}; }
        std::vector<platform::MidiPort> midiInputs() const override { return {}; }
        std::vector<platform::MidiPort> midiOutputs() const override { return {}; }
        std::vector<platform::IncomingMidi> takeIncomingMidi() override { return {}; }
        void setControlFilters (std::vector<platform::ControlFilter>) override {}
        void setOutputPorts (const std::vector<std::string>&) override {}
        void sendNow (const std::string&, const std::vector<std::uint8_t>&) override {}
    };

    void pumpMessages (int ms)
    {
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil (ms);
    }
}

TEST_CASE ("C4: Session::loadSoundFont lifetime guard prevents UAF on destroy and stale loads", "[audit][c4][app]")
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto sf2Path = test::writeTestSoundFont ("arranger-c4-test.sf2");
    const juce::File sf2File (juce::String::fromUTF8 (sf2Path.string().c_str()));
    const auto missingFile = sf2File.getSiblingFile ("nonexistent-c4.sf2");

    // 1. Destroying Session while loadSoundFont callback is pending must not UAF.
    {
        auto engine = std::make_unique<Engine>();
        NullDeviceHost host;
        auto session = std::make_unique<Session> (*engine, host);
        session->loadSoundFont (sf2File);
        session.reset();
        pumpMessages (60);
    }

    // 2. A newer loadSoundFont call supersedes an older one via generation counter.
    {
        auto engine = std::make_unique<Engine>();
        NullDeviceHost host;
        Session session (*engine, host);
        session.loadSoundFont (missingFile);
        session.loadSoundFont (sf2File);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);
        while (session.soundFont() == nullptr && std::chrono::steady_clock::now() < deadline)
            pumpMessages (20);
        CHECK (session.soundFont() != nullptr);
    }
}

TEST_CASE ("H1: SessionTools and queued message-thread tasks skip cancelled or post-shutdown Session calls", "[audit][h1][app]")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto engine = std::make_unique<Engine>();
    NullDeviceHost host;
    auto session = std::make_unique<Session> (*engine, host);
    auto tools = std::make_shared<SessionTools> (*session);
    CHECK (tools->isAlive());

    // Destroy Session while SessionTools is still held by a queued lambda.
    session.reset();
    CHECK_FALSE (tools->isAlive());

    bool touchedServer = false;
    std::atomic<bool> done { false };
    auto worker = std::async (std::launch::async, [&] {
        const bool ok = onMessageThread (
            [&] {
                if (! tools->isAlive())
                    return;
                touchedServer = true;
                tools->server().call ("get_song", "{}");
            },
            {});
        done.store (true);
        return ok;
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (3);
    while (! done.load() && std::chrono::steady_clock::now() < deadline)
        pumpMessages (10);
    CHECK (worker.get());
    CHECK_FALSE (touchedServer);
}

#if JUCE_MAC || JUCE_LINUX
TEST_CASE ("H5: API keys in settings and mcp-connection.json keep 0600 permissions on POSIX", "[audit][h5][app]")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto engine = std::make_unique<Engine>();
    NullDeviceHost host;
    Session session (*engine, host);

    const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("arranger_h5_" + juce::Uuid().toString());
    tempDir.createDirectory();
    const auto settingsFile = tempDir.getChildFile ("test.settings");

    juce::PropertiesFile::Options opts;
    opts.applicationName = "test";
    juce::PropertiesFile props (settingsFile, opts);

    {
        AssistantChat chat (session, &props, [] { return true; });
        chat.setApiValues ("api-anthropic", { "https://api.anthropic.com", "claude-opus-5-5", "sk-secret-key" });

        struct stat st {};
        REQUIRE (::stat (settingsFile.getFullPathName().toRawUTF8(), &st) == 0);
        CHECK ((st.st_mode & 0777) == 0600);

        // Subsequent PropertiesFile::saveIfNeeded() must not widen permissions back to 0644.
        props.setValue ("otherKey", "otherValue");
        props.saveIfNeeded();
        REQUIRE (::stat (settingsFile.getFullPathName().toRawUTF8(), &st) == 0);
        CHECK ((st.st_mode & 0777) == 0600);
    }

    {
        const auto connFile = tempDir.getChildFile ("mcp-connection.json");
        McpService::setConnectionFile (connFile);
        McpService mcp (session);
        REQUIRE (mcp.isRunning());
        struct stat st {};
        REQUIRE (::stat (connFile.getFullPathName().toRawUTF8(), &st) == 0);
        CHECK ((st.st_mode & 0777) == 0600);
    }
    McpService::setConnectionFile ({});

    tempDir.deleteRecursively();
}
#endif

TEST_CASE ("H7: python_tools::run interrupts silent child processes promptly on threadShouldExit", "[audit][h7][app]")
{
    struct RunnerThread final : juce::Thread
    {
        RunnerThread() : juce::Thread ("H7 test runner") {}
        int exitCode = 0;
        void run() override
        {
#if JUCE_WINDOWS
            exitCode = python_tools::run ({ "powershell.exe", "-NoProfile", "-Command", "Start-Sleep -Seconds 30" }, *this, [] (const juce::String&) {});
#else
            exitCode = python_tools::run ({ "/bin/sh", "-c", "sleep 30" }, *this, [] (const juce::String&) {});
#endif
        }
    };

    RunnerThread runner;
    runner.startThread();
    juce::Thread::sleep (80);
    const auto t0 = std::chrono::steady_clock::now();
    runner.signalThreadShouldExit();
    CHECK (runner.waitForThreadToExit (1500));
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now() - t0).count();
    CHECK (elapsedMs < 1500);
    CHECK (runner.exitCode == -1);

    // Also verify MidiAiService::runToolCall respects the `cancelled` callback when thread == nullptr.
    juce::ScopedJuceInitialiser_GUI init;
    auto engine = std::make_unique<Engine>();
    NullDeviceHost host;
    Session session (*engine, host);
    auto tools = std::make_shared<SessionTools> (session);
    const auto reply = MidiAiService::runToolCall (
        tools,
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"ai_humanize","arguments":{"track":1}}})",
        nullptr,
        [] { return true; });
    CHECK (reply.empty());
}

TEST_CASE ("H6 & H8: SpeechService passes -- before text and does not block on install cancellation", "[audit][h6][h8][app]")
{
    const juce::String injected = "--output-filename=/tmp/injected.wav";
    const auto spdArgs = SpeechService::spdSayArgs (injected);
    REQUIRE (spdArgs.size() == 4);
    CHECK (spdArgs[2] == "--");
    CHECK (spdArgs[3] == injected);

    const auto ttsArgs = SpeechService::offlineTtsArgs (
        juce::File ("/tmp/sherpa-onnx-offline-tts"),
        SpeechService::Engine::kitten,
        juce::File ("/tmp/model"),
        juce::File ("/tmp/speech.wav"),
        0,
        injected);
    REQUIRE (ttsArgs.size() >= 3);
    CHECK (ttsArgs[ttsArgs.size() - 2] == "--");
    CHECK (ttsArgs[ttsArgs.size() - 1] == injected);

    juce::ScopedJuceInitialiser_GUI init;
    auto engine = std::make_unique<Engine>();
    platform::JuceDeviceHost devices (*engine);
    Session session (*engine, devices);

    const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("arranger_h6_h8_" + juce::Uuid().toString());
    tempDir.createDirectory();
    juce::PropertiesFile::Options opts;
    opts.applicationName = "speech_test";
    juce::PropertiesFile props (tempDir.getChildFile ("speech.settings"), opts);

    SpeechService speech (session, devices, &props);
    speech.cancelInstall();
    CHECK_FALSE (speech.isInstalling());

    tempDir.deleteRecursively();
}

TEST_CASE ("H9: Session::audition syncs samplers against the auditioned song instead of document.song()", "[audit][h9][app]")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto engine = std::make_unique<Engine>();
    NullDeviceHost host;
    Session session (*engine, host);

    CHECK (session.song().tracks.empty());

    Song proposal = session.song();
    auto& samplerTrack = edit::addTrack (proposal, "Audition Drums", 1);
    samplerTrack.output.kind = TrackOutput::Kind::sampler;
    const TrackId trackId = samplerTrack.id;

    session.audition (proposal, 0, 1920);
    CHECK (session.isAuditioning());
    // Because publishSong(song) now passes `song` to syncSamplers(song), the audition track's sampler is built.
    CHECK (session.samplerFor (trackId) != nullptr);

    session.audition (std::nullopt);
    CHECK_FALSE (session.isAuditioning());
    CHECK (session.samplerFor (trackId) == nullptr);
}

TEST_CASE ("H10: McpService stops connection threads before closing sockets and caps concurrent connections", "[audit][h10][app]")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto engine = std::make_unique<Engine>();
    NullDeviceHost host;
    Session session (*engine, host);

    const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("arranger_h10_" + juce::Uuid().toString());
    tempDir.createDirectory();
    McpService::setConnectionFile (tempDir.getChildFile ("mcp-connection.json"));

    auto mcp = std::make_unique<McpService> (session);
    REQUIRE (mcp->isRunning());
    const int port = mcp->getPort();
    std::vector<std::unique_ptr<juce::StreamingSocket>> clients;
    for (int i = 0; i < 22; ++i)
    {
        auto s = std::make_unique<juce::StreamingSocket>();
        if (s->connect ("127.0.0.1", port, 200))
            clients.push_back (std::move (s));
    }
    juce::Thread::sleep (50);
    // Clean shutdown with >16 open connections must not data-race or hang.
    mcp.reset();
    for (auto& c : clients)
        c->close();

    McpService::setConnectionFile ({});
    tempDir.deleteRecursively();
}

TEST_CASE ("H12: saveStyleFromAudio confines paths inside the Styles folder and blocks traversal", "[audit][h12][app]")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto engine = std::make_unique<Engine>();
    NullDeviceHost host;
    Session session (*engine, host);

    const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("arranger_h12_" + juce::Uuid().toString());
    const auto stylesDir = tempDir.getChildFile ("Styles");
    stylesDir.createDirectory();
    SessionTools::setStyleFolder (stylesDir);

    auto& sfa = session.styleFromAudio();
    CHECK_THROWS (sfa.confinedStyleFile ("../escape.sty"));
    CHECK_THROWS (sfa.confinedStyleFile ("sub/../../escape.sty"));
    CHECK_THROWS (sfa.confinedStyleFile ("..\\escape.sty"));
    CHECK_THROWS (sfa.confinedStyleFile (tempDir.getChildFile ("outside.sty").getFullPathName()));

    const auto valid = sfa.confinedStyleFile ("MyStyle.sty");
    CHECK (valid.isAChildOf (stylesDir));

    // Also verify through MCP server tool call save_style_from_audio.
    SessionTools tools (session);
    CHECK_THROWS (tools.server().call ("save_style_from_audio", R"({"path":"../escape.sty"})"));
    CHECK_THROWS (tools.server().call ("save_style_from_audio", json ({ { "path", tempDir.getChildFile ("outside.sty").getFullPathName().toStdString() } }).dump()));

    tempDir.deleteRecursively();
}

#endif // ARRANGER_HAS_APP_TESTS

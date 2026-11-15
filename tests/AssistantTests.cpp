#include "arranger/core/Assistant.h"
#include "arranger/core/Document.h"
#include "arranger/core/McpServer.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace arranger;
using namespace arranger::assistant;
using nlohmann::json;

namespace
{
    struct TestHost final : mcp::Host
    {
        Document doc;
        const Song& song() const override { return doc.song(); }
        void edit (const std::string& d, const std::function<void (Song&)>& fn) override { doc.edit (d, fn); }
    };

    std::vector<Event> feed (OutputReader& reader, std::initializer_list<std::string> lines, int exitCode)
    {
        std::vector<Event> all;
        for (const auto& l : lines)
            for (auto& e : reader.line (l))
                all.push_back (e);
        for (auto& e : reader.finish (exitCode))
            all.push_back (e);
        return all;
    }
}

TEST_CASE ("Assistant programs: words, quoting and command lines", "[assistant]")
{
    const auto programs = builtInPrograms();
    REQUIRE (programs.size() == 3);
    const auto& claude = programs[0];

    auto words = arguments (claude, { { "mcp_config", "/tmp/a b/mcp.json" }, { "helper", "/x/arranger-mcp" } });
    CHECK (std::find (words.begin(), words.end(), "/tmp/a b/mcp.json") != words.end());
    CHECK (std::find (words.begin(), words.end(), "--resume") == words.end());

    words = arguments (claude, { { "mcp_config", "m.json" }, { "session", "abc-123" } });
    CHECK (words.back() == "abc-123");

    const auto& codex = programs[1];
    words = arguments (codex, { { "helper", "C:\\Apps\\arranger-mcp.exe" } });
    CHECK (std::find (words.begin(), words.end(), "mcp_servers.arranger-midi-daw.command='C:\\Apps\\arranger-mcp.exe'") != words.end());
    CHECK (std::find (words.begin(), words.end(), "mcp_servers.arranger-midi-daw.default_tools_approval_mode='approve'") != words.end());
    CHECK (words.back() == "-");

    CHECK (shellQuote ("claude", false) == "claude");
    CHECK (shellQuote ("", false) == "''");
    CHECK (shellQuote ("it's", false) == "'it'\\''s'");
    CHECK (shellQuote ("a b", true) == "\"a b\"");
    CHECK (shellQuote ("say \"hi\"", true) == "\"say \\\"hi\\\"\"");
    CHECK (shellQuote ("C:\\a b\\", true) == "\"C:\\a b\\\\\"");

    CHECK (shellCommand ("/w", { "claude", "-p" }, "/t/in.txt", false) == "cd /w && claude -p < /t/in.txt 2>&1");
    CHECK (shellCommand ("C:\\w x", { "codex", "exec" }, "C:\\t\\in.txt", true) == "cd /d \"C:\\w x\" && codex exec < C:\\t\\in.txt 2>&1");

    CHECK (json::parse (mcpConfig ("/x/h"))["mcpServers"]["arranger-midi-daw"]["command"] == "/x/h");
    CHECK (json::parse (geminiSettings ("/x/h"))["mcpServers"]["arranger-midi-daw"]["trust"] == true);

    const auto mine = readPrograms (R"([{"name":"My bot","command":"mybot","args":["--ask"],"output":"text"}])");
    REQUIRE (mine.size() == 1);
    CHECK (mine[0].id == "mybot");
    CHECK (mine[0].args == std::vector<std::string> { "--ask" });
    CHECK_THROWS (readPrograms ("{nope"));
    CHECK_THROWS (readPrograms (R"([{"name":"no command"}])"));
    CHECK_THROWS (readPrograms (R"([{"command":"x","output":"xml"}])"));

    const auto prompt = promptText ({ { true, "add drums" }, { false, "Done." } }, "make it louder", true);
    CHECK (prompt.find ("arranger-midi-daw tools") != std::string::npos);
    CHECK (prompt.find ("User: add drums\nAssistant: Done.\n") != std::string::npos);
    CHECK (prompt.substr (prompt.size() - 14) == "make it louder");
}

TEST_CASE ("Assistant programs: reading the output", "[assistant]")
{
    SECTION ("Claude Code")
    {
        OutputReader reader (Program::Output::claudeStreamJson);
        const auto events = feed (reader,
                                  { R"({"type":"system","subtype":"init","session_id":"s1"})",
                                    R"({"type":"stream_event","event":{}})",
                                    R"({"type":"assistant","message":{"content":[{"type":"tool_use","name":"mcp__arranger-midi-daw__add_track","input":{}}]}})",
                                    R"({"type":"assistant","message":{"content":[{"type":"text","text":"I added a piano track."}]}})",
                                    R"({"type":"result","subtype":"success","is_error":false,"result":"I added a piano track.","session_id":"s1"})" },
                                  0);
        REQUIRE (events.size() == 5);
        CHECK (events[0].kind == Event::Kind::session);
        CHECK (events[1].kind == Event::Kind::tool);
        CHECK (events[1].value == "add_track");
        CHECK (events[2].value == "I added a piano track.");
        CHECK (events[3].kind == Event::Kind::session);
        CHECK (events[4].kind == Event::Kind::done);

        OutputReader failing (Program::Output::claudeStreamJson);
        const auto bad = feed (failing, { R"({"type":"result","subtype":"success","is_error":true,"result":"Not logged in"})" }, 1);
        REQUIRE (bad.size() == 1);
        CHECK (bad[0].kind == Event::Kind::error);
        CHECK (bad[0].value == "Not logged in");
    }

    SECTION ("Codex")
    {
        OutputReader reader (Program::Output::codexJson);
        const auto events = feed (reader,
                                  { "a warning that is not JSON",
                                    R"({"type":"thread.started","thread_id":"t9"})",
                                    R"({"type":"error","message":"Reconnecting... 2/5"})",
                                    R"({"type":"item.started","item":{"type":"mcp_tool_call","server":"arranger-midi-daw","tool":"set_beat"}})",
                                    R"({"type":"item.completed","item":{"type":"agent_message","text":"Beat set."}})",
                                    R"({"type":"turn.completed","usage":{}})" },
                                  0);
        REQUIRE (events.size() == 4);
        CHECK (events[0].value == "t9");
        CHECK (events[1].value == "set_beat");
        CHECK (events[2].value == "Beat set.");
        CHECK (events[3].kind == Event::Kind::done);

        OutputReader failing (Program::Output::codexJson);
        const auto bad = feed (failing, { R"({"type":"turn.failed","error":{"message":"Please log in"}})" }, 1);
        REQUIRE (bad.size() == 1);
        CHECK (bad[0].value == "Please log in");

        OutputReader offline (Program::Output::codexJson);
        const auto gone = feed (offline, { R"({"type":"thread.started","thread_id":"t1"})", R"j({"type":"error","message":"Reconnecting... 5/5 (no network)"})j" }, 1);
        REQUIRE (gone.size() == 2);
        CHECK (gone[1].kind == Event::Kind::error);
        CHECK (gone[1].value.find ("no network") != std::string::npos);
    }

    SECTION ("Plain text, a missing program and no answer")
    {
        OutputReader reader (Program::Output::text);
        const auto events = feed (reader, { "Line one", "Line two" }, 0);
        REQUIRE (events.size() == 3);
        CHECK (events[0].value == "Line one\n");
        CHECK (events[2].kind == Event::Kind::done);

        OutputReader missing (Program::Output::text);
        const auto gone = feed (missing, { "sh: gemini: command not found" }, 127);
        CHECK (gone.back().kind == Event::Kind::error);
        CHECK (gone.back().value == "The program was not found.");

        OutputReader silent (Program::Output::claudeStreamJson);
        const auto none = feed (silent, { "Error: something broke" }, 0);
        REQUIRE (none.size() == 1);
        CHECK (none[0].kind == Event::Kind::error);
        CHECK (none[0].value == "Error: something broke");
    }
}

TEST_CASE ("Assistant API: the tool loop runs the song tools", "[assistant]")
{
    TestHost host;
    mcp::Server server (host);
    const auto runTool = [&] (const std::string& name, const std::string& args) { return server.call (name, args); };

    SECTION ("Anthropic")
    {
        ApiChat chat ({ ApiSettings::Provider::anthropic, "", "sk-test", "" }, server.toolList());
        const auto request = chat.request();
        CHECK (request.url == "https://api.anthropic.com/v1/messages");
        const auto body = json::parse (request.body);
        CHECK (body["model"] == "claude-opus-5-5");
        CHECK (body["fallbacks"] == "default");
        CHECK (body["tools"].size() > 20);
        CHECK (body["tools"][0].contains ("input_schema"));

        int calls = 0;
        std::vector<Event> events;
        chat.ask (
            "add a piano track",
            [&] (const HttpRequest& r) -> HttpResponse {
                ++calls;
                const auto sent = json::parse (r.body);
                if (calls == 1)
                {
                    CHECK (sent["messages"].size() == 1);
                    return { 200, R"({"stop_reason":"tool_use","content":[{"type":"text","text":"Adding it."},{"type":"tool_use","id":"tu1","name":"add_track","input":{"name":"Piano","sound":"piano"}}]})", {} };
                }
                // The tool result went back.
                CHECK (sent["messages"].size() == 3);
                CHECK (sent["messages"][2]["content"][0]["tool_use_id"] == "tu1");
                CHECK (! sent["messages"][2]["content"][0].contains ("is_error"));
                return { 200, R"({"stop_reason":"end_turn","content":[{"type":"text","text":"Done."}]})", {} };
            },
            runTool, [&] (const Event& e) { events.push_back (e); });

        CHECK (calls == 2);
        REQUIRE (host.song().tracks.size() == 1);
        CHECK (host.song().tracks[0].name == "Piano");
        REQUIRE (events.size() == 4);
        CHECK (events[1].kind == Event::Kind::tool);
        CHECK (events[3].kind == Event::Kind::done);
        CHECK (json::parse (chat.requestBody())["messages"].size() == 4);

        // A failed request leaves the conversation as it was.
        chat.ask ("again", [] (const HttpRequest&) { return HttpResponse { 401, R"({"error":{"message":"invalid x-api-key"}})", {} }; }, runTool,
                  [&] (const Event& e) { events.push_back (e); });
        CHECK (events.back().kind == Event::Kind::error);
        CHECK (events.back().value.find ("invalid x-api-key") != std::string::npos);
        CHECK (json::parse (chat.requestBody())["messages"].size() == 4);
    }

    SECTION ("OpenAI-style")
    {
        ApiChat chat ({ ApiSettings::Provider::openAi, "http://localhost:11434/v1/", "", "llama" }, server.toolList());
        const auto request = chat.request();
        CHECK (request.url == "http://localhost:11434/v1/chat/completions");
        for (const auto& [name, value] : request.headers)
            CHECK (name != "authorization");

        int calls = 0;
        std::vector<Event> events;
        chat.ask (
            "add bass",
            [&] (const HttpRequest& r) -> HttpResponse {
                ++calls;
                const auto sent = json::parse (r.body);
                CHECK (sent["messages"][0]["role"] == "system");
                if (calls == 1)
                    return { 200, R"({"choices":[{"message":{"role":"assistant","content":null,"tool_calls":[{"id":"c1","type":"function","function":{"name":"add_track","arguments":"{\"name\":\"Bass\",\"sound\":\"fingered bass\"}"}},{"id":"c2","type":"function","function":{"name":"no_such_tool","arguments":"{}"}}]}}]})", {} };
                CHECK (sent["messages"].size() == 5);
                CHECK (sent["messages"][3]["tool_call_id"] == "c1");
                CHECK (sent["messages"][4]["content"].get<std::string>().rfind ("Error", 0) == 0);
                return { 200, R"({"choices":[{"message":{"role":"assistant","content":"Bass added."}}]})", {} };
            },
            runTool, [&] (const Event& e) { events.push_back (e); });

        CHECK (calls == 2);
        REQUIRE (host.song().tracks.size() == 1);
        CHECK (host.song().tracks[0].name == "Bass");
        CHECK (events.back().kind == Event::Kind::done);
    }
}

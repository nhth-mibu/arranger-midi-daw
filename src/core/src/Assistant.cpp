#include "arranger/core/Assistant.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace arranger::assistant
{

using nlohmann::json;

namespace
{
    constexpr const char* kServerName = "arranger-midi-daw";
    constexpr int kMostSteps = 30;
    constexpr std::size_t kTailLines = 12;

    bool endsWith (const std::string& s, const std::string& end)
    {
        return s.size() >= end.size() && s.compare (s.size() - end.size(), end.size(), end) == 0;
    }

    std::string replaceAll (std::string s, const std::string& from, const std::string& to)
    {
        for (std::size_t at = s.find (from); at != std::string::npos; at = s.find (from, at + to.size()))
            s.replace (at, from.size(), to);
        return s;
    }

    std::string joinLines (const std::vector<std::string>& lines)
    {
        std::string out;
        for (const auto& l : lines)
            out += (out.empty() ? "" : "\n") + l;
        return out;
    }

    /** A tool name without the prefix that programs put before MCP tools. */
    std::string toolName (std::string name)
    {
        for (const auto& prefix : { std::string ("mcp__") + kServerName + "__", std::string (kServerName) + "__", std::string (kServerName) + "." })
            if (name.rfind (prefix, 0) == 0)
                return name.substr (prefix.size());
        return name;
    }

    std::string text (const json& j, const char* key)
    {
        if (j.is_object())
            if (const auto it = j.find (key); it != j.end() && it->is_string())
                return it->get<std::string>();
        return {};
    }

    Program::Output outputFrom (const std::string& s)
    {
        if (s == "claude-stream-json")
            return Program::Output::claudeStreamJson;
        if (s == "codex-json")
            return Program::Output::codexJson;
        if (s.empty() || s == "text")
            return Program::Output::text;
        throw std::runtime_error ("unknown output \"" + s + "\" (use text, claude-stream-json or codex-json)");
    }

    bool isClaudeWithEffort (const std::string& model)
    {
        for (const char* p : { "claude-opus-5", "claude-fable-5", "claude-sonnet-5", "claude-haiku-5", "claude-opus-4-7", "claude-opus-4-8" })
            if (model.rfind (p, 0) == 0)
                return true;
        return false;
    }

    bool hasServerFallback (const std::string& model)
    {
        for (const char* m : { "claude-opus-5-5", "claude-opus-5", "claude-fable-5-1", "claude-fable-5-2", "claude-sonnet-5-5" })
            if (model == m)
                return true;
        return false;
    }

    std::string errorMessage (const HttpResponse& r)
    {
        std::string message;
        try
        {
            const auto j = json::parse (r.body);
            if (j.contains ("error"))
                message = j["error"].is_object() ? text (j["error"], "message") : (j["error"].is_string() ? j["error"].get<std::string>() : "");
        }
        catch (const std::exception&)
        {
        }
        if (message.empty())
            message = r.body.substr (0, 300);
        if (r.status == 401 || r.status == 403)
            return "The API key was not accepted (" + std::to_string (r.status) + "): " + message;
        return "The service answered " + std::to_string (r.status) + ": " + message;
    }
}

std::string instructions()
{
    return "You are the assistant inside Arranger MIDI DAW, a MIDI sequencer. "
           "Use the arranger-midi-daw tools to read and change the song: tracks, notes, clips, the Score (chords, marks, lyrics), "
           "the Beat patterns and the Clip grid. Call get_song first to see what is there. "
           "Bars and beats start at 1; lengths are in beats. Each change is one undo step, so the user can undo it. "
           "Do not read or write files and do not run commands: only use the song tools. "
           "Answer in short, plain sentences and say what you changed.";
}

std::string promptText (const std::vector<Message>& earlier, const std::string& message, bool withInstructions)
{
    std::string out;
    if (withInstructions)
        out += instructions() + "\n\n";
    if (! earlier.empty())
    {
        out += "The conversation so far:\n";
        for (const auto& m : earlier)
            out += (m.fromUser ? "User: " : "Assistant: ") + m.text + "\n";
        out += "\nThe user's new message:\n";
    }
    return out + message;
}

// ---- programs ---------------------------------------------------------------------

std::vector<Program> builtInPrograms()
{
    std::vector<Program> list;

    Program claude;
    claude.id = "claude-code";
    claude.name = "Claude Code (Claude subscription)";
    claude.command = "claude";
    claude.args = { "-p", "--output-format", "stream-json", "--verbose", "--mcp-config", "{mcp_config}", "--strict-mcp-config",
                    "--allowedTools", std::string ("mcp__") + kServerName, "--tools", "" };
    claude.resumeArgs = claude.args;
    claude.resumeArgs.insert (claude.resumeArgs.end(), { "--resume", "{session}" });
    claude.output = Program::Output::claudeStreamJson;
    claude.help = "Install Claude Code (claude.com/claude-code), then run \"claude\" once in a terminal and sign in with your Claude account.";
    list.push_back (claude);

    Program codex;
    codex.id = "codex";
    codex.name = "Codex (ChatGPT subscription)";
    codex.command = "codex";
    // "resume" takes fewer options than "exec": the sandbox goes in as a setting.
    const std::vector<std::string> codexOptions { "--json", "--skip-git-repo-check",
                                                  "-c", std::string ("mcp_servers.") + kServerName + ".command='{helper}'",
                                                  "-c", "sandbox_mode='read-only'", "-c", "approval_policy='never'" };
    codex.args = { "exec" };
    codex.args.insert (codex.args.end(), codexOptions.begin(), codexOptions.end());
    codex.args.push_back ("-");
    codex.resumeArgs = { "exec", "resume" };
    codex.resumeArgs.insert (codex.resumeArgs.end(), codexOptions.begin(), codexOptions.end());
    codex.resumeArgs.insert (codex.resumeArgs.end(), { "{session}", "-" });
    codex.output = Program::Output::codexJson;
    codex.help = "Install the Codex CLI (npm install -g @openai/codex), then run \"codex\" once in a terminal and sign in with your ChatGPT account.";
    list.push_back (codex);

    Program gemini;
    gemini.id = "gemini";
    gemini.name = "Gemini CLI (Google account)";
    gemini.command = "gemini";
    gemini.args = { "--allowed-mcp-server-names", kServerName, "--prompt", "Answer the request above." };
    gemini.output = Program::Output::text;
    gemini.help = "Install the Gemini CLI (npm install -g @google/gemini-cli), then run \"gemini\" once in a terminal and sign in with your Google account.";
    list.push_back (gemini);

    return list;
}

std::vector<Program> readPrograms (const std::string& text)
{
    json j;
    try
    {
        j = json::parse (text);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error (std::string ("the assistants file is not valid JSON: ") + e.what());
    }
    if (j.is_object() && j.contains ("assistants"))
        j = j["assistants"];
    if (! j.is_array())
        throw std::runtime_error ("the assistants file must hold a list of assistants");

    std::vector<Program> list;
    for (const auto& item : j)
    {
        Program p;
        p.id = item.value ("id", "");
        p.command = item.value ("command", "");
        p.name = item.value ("name", p.command);
        if (p.id.empty())
            p.id = p.command;
        if (p.command.empty())
            throw std::runtime_error ("each assistant needs a \"command\"");
        if (item.contains ("args"))
            p.args = item["args"].get<std::vector<std::string>>();
        if (item.contains ("resumeArgs"))
            p.resumeArgs = item["resumeArgs"].get<std::vector<std::string>>();
        p.output = outputFrom (item.value ("output", "text"));
        p.help = item.value ("help", "");
        list.push_back (std::move (p));
    }
    return list;
}

std::vector<std::string> arguments (const Program& p, const std::map<std::string, std::string>& values)
{
    const auto session = values.find ("session");
    const bool resume = ! p.resumeArgs.empty() && session != values.end() && ! session->second.empty();
    auto words = resume ? p.resumeArgs : p.args;
    for (auto& w : words)
        for (const auto& [name, value] : values)
            w = replaceAll (w, "{" + name + "}", value);
    return words;
}

std::string shellQuote (const std::string& word, bool windows)
{
    const bool plain = ! word.empty() && std::all_of (word.begin(), word.end(), [windows] (char c) {
        return std::isalnum (static_cast<unsigned char> (c)) || std::string ("_-./=:,@+").find (c) != std::string::npos || (windows && c == '\\');
    });
    if (plain)
        return word;

    if (! windows)
        return "'" + replaceAll (word, "'", "'\\''") + "'";

    // The C runtime's rules: backslashes before a quote double, quotes get a backslash.
    std::string out = "\"";
    std::size_t backslashes = 0;
    for (char c : word)
    {
        if (c == '\\')
        {
            ++backslashes;
            continue;
        }
        if (c == '"')
            out.append (backslashes * 2 + 1, '\\');
        else
            out.append (backslashes, '\\');
        backslashes = 0;
        out += c;
    }
    out.append (backslashes * 2, '\\');
    return out + "\"";
}

std::string shellCommand (const std::string& folder, const std::vector<std::string>& words, const std::string& inputFile, bool windows)
{
    std::string line = windows ? "cd /d " : "cd ";
    line += shellQuote (folder, windows) + " && ";
    for (const auto& w : words)
        line += shellQuote (w, windows) + " ";
    return line + "< " + shellQuote (inputFile, windows) + " 2>&1";
}

std::string mcpConfig (const std::string& helper)
{
    return json { { "mcpServers", { { kServerName, { { "command", helper } } } } } }.dump (2);
}

std::string geminiSettings (const std::string& helper)
{
    return json { { "mcpServers", { { kServerName, { { "command", helper }, { "trust", true } } } } } }.dump (2);
}

OutputReader::OutputReader (Program::Output o) : output (o) {}

std::vector<Event> OutputReader::line (const std::string& raw)
{
    auto l = raw;
    while (! l.empty() && (l.back() == '\r' || l.back() == '\n'))
        l.pop_back();
    if (l.empty() || finished)
        return {};

    if (output == Program::Output::text)
    {
        gotText = true;
        tail.push_back (l);
        if (tail.size() > kTailLines)
            tail.erase (tail.begin());
        return { { Event::Kind::text, l + "\n" } };
    }

    json j;
    try
    {
        j = json::parse (l);
    }
    catch (const std::exception&)
    {
        // A line that is not JSON: a warning or an error from the program.
        tail.push_back (l);
        if (tail.size() > kTailLines)
            tail.erase (tail.begin());
        return {};
    }
    if (! j.is_object())
        return {};

    std::vector<Event> events;
    const auto type = text (j, "type");

    if (output == Program::Output::claudeStreamJson)
    {
        if (type == "system" && text (j, "subtype") == "init")
            events.push_back ({ Event::Kind::session, text (j, "session_id") });
        else if (type == "assistant" && j.contains ("message") && j["message"].contains ("content") && j["message"]["content"].is_array())
        {
            for (const auto& block : j["message"]["content"])
            {
                const auto kind = text (block, "type");
                if (kind == "text" && ! text (block, "text").empty())
                {
                    gotText = true;
                    events.push_back ({ Event::Kind::text, text (block, "text") });
                }
                else if (kind == "tool_use")
                    events.push_back ({ Event::Kind::tool, toolName (text (block, "name")) });
            }
        }
        else if (type == "result")
        {
            if (! text (j, "session_id").empty())
                events.push_back ({ Event::Kind::session, text (j, "session_id") });
            const bool isError = j.value ("is_error", false) || (! text (j, "subtype").empty() && text (j, "subtype") != "success");
            if (isError)
            {
                failed = true;
                auto why = text (j, "result");
                if (why.empty())
                    why = "The assistant stopped (" + text (j, "subtype") + ").";
                events.push_back ({ Event::Kind::error, why });
            }
            else
            {
                if (! gotText && ! text (j, "result").empty())
                {
                    gotText = true;
                    events.push_back ({ Event::Kind::text, text (j, "result") });
                }
                events.push_back ({ Event::Kind::done, {} });
            }
            finished = true;
        }
        return events;
    }

    // Codex: the newer event names, then the older ones.
    if (type == "thread.started")
        events.push_back ({ Event::Kind::session, text (j, "thread_id") });
    else if ((type == "item.started" || type == "item.completed") && j.contains ("item"))
    {
        const auto& item = j["item"];
        const auto kind = text (item, "type");
        if (type == "item.started" && kind == "mcp_tool_call")
            events.push_back ({ Event::Kind::tool, toolName (text (item, "tool")) });
        else if (type == "item.completed" && kind == "agent_message" && ! text (item, "text").empty())
        {
            gotText = true;
            events.push_back ({ Event::Kind::text, text (item, "text") });
        }
    }
    else if (type == "turn.completed")
    {
        events.push_back ({ Event::Kind::done, {} });
        finished = true;
    }
    else if (type == "error")
    {
        // Often a passing problem ("Reconnecting... 2/5"): keep it for the message if the turn fails.
        tail.push_back (text (j, "message"));
        if (tail.size() > kTailLines)
            tail.erase (tail.begin());
    }
    else if (type == "turn.failed")
    {
        auto why = j.contains ("error") ? text (j["error"], "message") : text (j, "message");
        if (why.empty())
            why = "The assistant stopped with an error.";
        failed = true;
        finished = true;
        events.push_back ({ Event::Kind::error, why });
    }
    else if (j.contains ("msg") && j["msg"].is_object())
    {
        const auto& msg = j["msg"];
        const auto kind = text (msg, "type");
        if (kind == "session_configured")
            events.push_back ({ Event::Kind::session, text (msg, "session_id") });
        else if (kind == "agent_message" && ! text (msg, "message").empty())
        {
            gotText = true;
            events.push_back ({ Event::Kind::text, text (msg, "message") });
        }
        else if (kind == "mcp_tool_call_begin" && msg.contains ("invocation"))
            events.push_back ({ Event::Kind::tool, toolName (text (msg["invocation"], "tool")) });
        else if (kind == "task_complete")
        {
            events.push_back ({ Event::Kind::done, {} });
            finished = true;
        }
        else if (kind == "error")
        {
            failed = true;
            finished = true;
            events.push_back ({ Event::Kind::error, text (msg, "message") });
        }
    }
    return events;
}

std::vector<Event> OutputReader::finish (int exitCode)
{
    if (failed)
        return {};
    if (finished && exitCode == 0)
        return {};
    finished = true;

    const auto said = joinLines (tail);
    const bool missing = exitCode == 127 || said.find ("not recognized as an internal") != std::string::npos
                      || said.find ("command not found") != std::string::npos;
    if (missing)
        return { { Event::Kind::error, "The program was not found." } };
    if (exitCode != 0)
        return { { Event::Kind::error, said.empty() ? "The program stopped with exit code " + std::to_string (exitCode) + "." : said } };
    if (output != Program::Output::text && ! gotText)
        return { { Event::Kind::error, said.empty() ? "The program gave no answer." : said } };
    return { { Event::Kind::done, {} } };
}

// ---- API keys ---------------------------------------------------------------------

std::string ApiChat::defaultModel (ApiSettings::Provider p)
{
    return p == ApiSettings::Provider::anthropic ? "claude-opus-5-5" : "gpt-5";
}

std::string ApiChat::defaultUrl (ApiSettings::Provider p)
{
    return p == ApiSettings::Provider::anthropic ? "https://api.anthropic.com" : "https://api.openai.com/v1";
}

ApiChat::ApiChat (ApiSettings s, const std::string& toolListJson) : settings (std::move (s))
{
    if (settings.model.empty())
        settings.model = defaultModel (settings.provider);
    if (settings.url.empty())
        settings.url = defaultUrl (settings.provider);

    auto list = json::array();
    const auto mcpTools = json::parse (toolListJson);
    for (const auto& t : mcpTools)
    {
        const auto schema = t.contains ("inputSchema") ? t["inputSchema"] : json { { "type", "object" } };
        if (settings.provider == ApiSettings::Provider::anthropic)
            list.push_back ({ { "name", t["name"] }, { "description", t.value ("description", "") }, { "input_schema", schema } });
        else
            list.push_back ({ { "type", "function" }, { "function", { { "name", t["name"] }, { "description", t.value ("description", "") }, { "parameters", schema } } } });
    }
    tools = list.dump();
    history = "[]";
}

void ApiChat::clear()
{
    history = "[]";
}

std::string ApiChat::requestBody() const
{
    const auto messages = json::parse (history);
    if (settings.provider == ApiSettings::Provider::anthropic)
    {
        json body { { "model", settings.model }, { "max_tokens", 16000 }, { "system", instructions() },
                    { "tools", json::parse (tools) }, { "messages", messages },
                    { "cache_control", { { "type", "ephemeral" } } } };
        if (isClaudeWithEffort (settings.model))
            body["output_config"] = { { "effort", "medium" } };
        if (hasServerFallback (settings.model))
            body["fallbacks"] = "default";
        return body.dump();
    }

    auto all = json::array();
    all.push_back ({ { "role", "system" }, { "content", instructions() } });
    for (const auto& m : messages)
        all.push_back (m);
    return json { { "model", settings.model }, { "messages", all }, { "tools", json::parse (tools) } }.dump();
}

HttpRequest ApiChat::request() const
{
    HttpRequest r;
    auto base = settings.url;
    while (! base.empty() && base.back() == '/')
        base.pop_back();
    r.headers.push_back ({ "content-type", "application/json" });
    if (settings.provider == ApiSettings::Provider::anthropic)
    {
        r.url = endsWith (base, "/v1/messages") ? base : (endsWith (base, "/v1") ? base + "/messages" : base + "/v1/messages");
        r.headers.push_back ({ "x-api-key", settings.key });
        r.headers.push_back ({ "anthropic-version", "2023-06-01" });
        if (hasServerFallback (settings.model))
            r.headers.push_back ({ "anthropic-beta", "server-side-fallback-2026-07-01" });
    }
    else
    {
        r.url = endsWith (base, "/chat/completions") ? base : base + "/chat/completions";
        if (! settings.key.empty())
            r.headers.push_back ({ "authorization", "Bearer " + settings.key });
    }
    r.body = requestBody();
    return r;
}

void ApiChat::ask (const std::string& message, const Send& send, const RunTool& runTool, const OnEvent& onEvent, const std::function<bool()>& cancelled)
{
    const auto before = history;
    auto messages = json::parse (history);
    messages.push_back ({ { "role", "user" }, { "content", message } });
    history = messages.dump();

    const auto fail = [&] (const std::string& why) {
        history = before; // so the user can send the message again
        onEvent ({ Event::Kind::error, why });
    };

    for (int step = 0; step < kMostSteps; ++step)
    {
        if (cancelled && cancelled())
            return fail ("Stopped.");

        const auto response = send (request());
        if (response.status == 0)
            return fail (response.error.empty() ? "No answer from the service." : response.error);
        if (response.status < 200 || response.status >= 300)
            return fail (errorMessage (response));

        json body;
        try
        {
            body = json::parse (response.body);
        }
        catch (const std::exception&)
        {
            return fail ("The service gave an answer that is not JSON.");
        }

        messages = json::parse (history);
        std::vector<std::pair<std::string, json>> calls; // id, {name, arguments}

        if (settings.provider == ApiSettings::Provider::anthropic)
        {
            const auto stop = text (body, "stop_reason");
            if (stop == "refusal")
                return fail ("The model declined this request.");
            const auto content = body.contains ("content") ? body["content"] : json::array();
            messages.push_back ({ { "role", "assistant" }, { "content", content } });
            for (const auto& block : content)
            {
                const auto kind = text (block, "type");
                if (kind == "text" && ! text (block, "text").empty())
                    onEvent ({ Event::Kind::text, text (block, "text") });
                else if (kind == "tool_use")
                    calls.push_back ({ text (block, "id"), { { "name", block["name"] }, { "arguments", block.contains ("input") ? block["input"].dump() : "{}" } } });
            }
            if (calls.empty())
            {
                history = messages.dump();
                if (stop == "pause_turn")
                    continue;
                onEvent ({ Event::Kind::done, {} });
                return;
            }
            auto results = json::array();
            for (const auto& [id, call] : calls)
            {
                const auto name = call["name"].get<std::string>();
                onEvent ({ Event::Kind::tool, name });
                json result { { "type", "tool_result" }, { "tool_use_id", id } };
                try
                {
                    result["content"] = runTool (name, call["arguments"].get<std::string>());
                }
                catch (const std::exception& e)
                {
                    result["content"] = e.what();
                    result["is_error"] = true;
                }
                results.push_back (result);
            }
            messages.push_back ({ { "role", "user" }, { "content", results } });
            history = messages.dump();
            continue;
        }

        // OpenAI-style chat completions.
        if (! body.contains ("choices") || ! body["choices"].is_array() || body["choices"].empty())
            return fail ("The service gave no answer.");
        auto msg = body["choices"][0].value ("message", json::object());
        msg["role"] = "assistant";
        messages.push_back (msg);
        if (msg.contains ("content") && msg["content"].is_string() && ! msg["content"].get<std::string>().empty())
            onEvent ({ Event::Kind::text, msg["content"].get<std::string>() });
        if (msg.contains ("tool_calls") && msg["tool_calls"].is_array())
            for (const auto& c : msg["tool_calls"])
                if (c.contains ("function"))
                    calls.push_back ({ text (c, "id"), { { "name", c["function"].value ("name", "") }, { "arguments", c["function"].value ("arguments", "{}") } } });
        if (calls.empty())
        {
            history = messages.dump();
            onEvent ({ Event::Kind::done, {} });
            return;
        }
        for (const auto& [id, call] : calls)
        {
            const auto name = call["name"].get<std::string>();
            onEvent ({ Event::Kind::tool, name });
            std::string result;
            try
            {
                result = runTool (name, call["arguments"].get<std::string>());
            }
            catch (const std::exception& e)
            {
                result = std::string ("Error: ") + e.what();
            }
            messages.push_back ({ { "role", "tool" }, { "tool_call_id", id }, { "content", result } });
        }
        history = messages.dump();
    }
    fail ("The assistant took too many steps. Try a smaller request.");
}

} // namespace arranger::assistant

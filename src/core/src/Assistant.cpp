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

    bool boolValue (const json& j, const char* key, bool fallback = false)
    {
        if (j.is_object())
            if (const auto it = j.find (key); it != j.end())
            {
                if (it->is_boolean())
                    return it->get<bool>();
                if (it->is_string())
                    return *it == "true" || *it == "1";
                if (it->is_number())
                    return it->get<double>() != 0.0;
            }
        return fallback;
    }

    std::string jsonArgs (const json& j, const char* key)
    {
        if (j.is_object())
            if (const auto it = j.find (key); it != j.end())
            {
                if (it->is_string())
                    return it->get<std::string>();
                if (it->is_object())
                    return it->dump();
            }
        return "{}";
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
    return "You are the assistant inside Lazy Arranger Midi DAW, a MIDI sequencer with AI assist. "
           "Use the arranger-midi-daw tools to read and change the song: tracks, notes, clips, the Score (chords, marks, lyrics), "
           "the Beat patterns and the Clip grid. Call get_song first to see what is there. "
           "To start from an arranger style, use find_styles and fill_from_style; to play a track on a hardware synth, use list_devices, find_sounds and set_track_output. "
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
                                                  // The app has no approval dialog, so its song tools are pre-approved.
                                                  // This does not widen Codex's shell sandbox.
                                                  "-c", std::string ("mcp_servers.") + kServerName + ".default_tools_approval_mode='approve'",
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

std::vector<Program> readPrograms (const std::string& jsonText)
{
    json j;
    try
    {
        j = json::parse (jsonText);
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
        if (! item.is_object())
            throw std::runtime_error ("each assistant must be a JSON object");
        Program p;
        p.id = text (item, "id");
        p.command = text (item, "command");
        p.name = text (item, "name");
        if (p.name.empty())
            p.name = p.command;
        if (p.id.empty())
            p.id = p.command;
        if (p.command.empty())
            throw std::runtime_error ("each assistant needs a \"command\"");
        if (item.contains ("args") && item["args"].is_array())
            for (const auto& a : item["args"])
                if (a.is_string())
                    p.args.push_back (a.get<std::string>());
        if (item.contains ("resumeArgs") && item["resumeArgs"].is_array())
            for (const auto& a : item["resumeArgs"])
                if (a.is_string())
                    p.resumeArgs.push_back (a.get<std::string>());
        p.output = outputFrom (text (item, "output"));
        p.help = text (item, "help");
        list.push_back (std::move (p));
    }
    return list;
}

bool validSessionId (const std::string& id)
{
    if (id.empty() || id.size() > 256 || id.front() == '-')
        return false;
    return std::all_of (id.begin(), id.end(), [] (char c) {
        return std::isalnum (static_cast<unsigned char> (c)) || c == '_' || c == '-' || c == '.' || c == ':';
    });
}

std::vector<std::string> arguments (const Program& p, const std::map<std::string, std::string>& values)
{
    const auto session = values.find ("session");
    const bool resume = ! p.resumeArgs.empty() && session != values.end() && validSessionId (session->second);
    auto words = resume ? p.resumeArgs : p.args;
    for (auto& w : words)
        for (const auto& [name, rawValue] : values)
        {
            std::string value;
            if (name == "session")
            {
                if (validSessionId (rawValue))
                    value = rawValue;
            }
            else
            {
                value.reserve (rawValue.size());
                for (char c : rawValue)
                    if (c != '\r' && c != '\n')
                        value += c;
            }
            const std::string placeholder = "{" + name + "}";
            const std::string singleQuoted = "'" + placeholder + "'";
            if (w.find (singleQuoted) != std::string::npos && value.find ('\'') != std::string::npos)
            {
                std::string escaped;
                for (char c : value)
                {
                    if (c == '\\' || c == '"')
                        escaped += '\\';
                    escaped += c;
                }
                w = replaceAll (w, singleQuoted, "\"" + escaped + "\"");
            }
            else if (w.find ('\'') != std::string::npos && w.find (placeholder) != std::string::npos && value.find ('\'') != std::string::npos)
            {
                w = replaceAll (w, placeholder, replaceAll (value, "'", "''"));
            }
            else
            {
                w = replaceAll (w, placeholder, value);
            }
        }
    return words;
}

std::string shellQuote (const std::string& rawWord, bool windows)
{
    std::string word;
    word.reserve (rawWord.size());
    for (char c : rawWord)
        if (c != '\r' && c != '\n')
            word += c;

    const char* allowed = windows ? "_-./:@+\\" : "_-./=:,@+";
    const bool plain = ! word.empty() && std::all_of (word.begin(), word.end(), [allowed] (char c) {
        return std::isalnum (static_cast<unsigned char> (c)) || std::string_view (allowed).find (c) != std::string_view::npos;
    });
    if (plain)
        return word;

    if (! windows)
        return "'" + replaceAll (word, "'", "'\\''") + "'";

    // Windows cmd.exe + C runtime rules:
    // - Double quotes inside "..." are escaped as "" (with preceding backslashes doubled) so cmd.exe
    //   never leaves quote mode on embedded quotes and the C runtime receives a literal ".
    // - % is escaped as "^%" (closing the quote, caret-escaping % outside quotes, and reopening the quote)
    //   so cmd.exe cannot expand %VAR% while the C runtime concatenates the segments seamlessly.
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
        {
            out.append (backslashes * 2, '\\');
            backslashes = 0;
            out += "\"\"";
            continue;
        }
        if (c == '%')
        {
            out.append (backslashes * 2, '\\');
            backslashes = 0;
            out += "\"^%\"";
            continue;
        }
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
        {
            if (const auto sid = text (j, "session_id"); validSessionId (sid))
                events.push_back ({ Event::Kind::session, sid });
        }
        else if (type == "assistant" && j.contains ("message") && j["message"].is_object() && j["message"].contains ("content") && j["message"]["content"].is_array())
        {
            for (const auto& block : j["message"]["content"])
            {
                if (! block.is_object())
                    continue;
                const auto kind = text (block, "type");
                if (kind == "text" && ! text (block, "text").empty())
                {
                    gotText = true;
                    events.push_back ({ Event::Kind::text, text (block, "text") });
                }
                else if (kind == "tool_use")
                {
                    const auto name = toolName (text (block, "name"));
                    if (! name.empty())
                        events.push_back ({ Event::Kind::tool, name });
                }
            }
        }
        else if (type == "result")
        {
            if (const auto sid = text (j, "session_id"); validSessionId (sid))
                events.push_back ({ Event::Kind::session, sid });
            const bool isError = boolValue (j, "is_error", false) || (! text (j, "subtype").empty() && text (j, "subtype") != "success");
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
    {
        if (const auto tid = text (j, "thread_id"); validSessionId (tid))
            events.push_back ({ Event::Kind::session, tid });
    }
    else if ((type == "item.started" || type == "item.completed") && j.contains ("item") && j["item"].is_object())
    {
        const auto& item = j["item"];
        const auto kind = text (item, "type");
        if (type == "item.started" && kind == "mcp_tool_call")
        {
            const auto name = toolName (text (item, "tool"));
            if (! name.empty())
                events.push_back ({ Event::Kind::tool, name });
        }
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
        {
            if (const auto sid = text (msg, "session_id"); validSessionId (sid))
                events.push_back ({ Event::Kind::session, sid });
        }
        else if (kind == "agent_message" && ! text (msg, "message").empty())
        {
            gotText = true;
            events.push_back ({ Event::Kind::text, text (msg, "message") });
        }
        else if (kind == "mcp_tool_call_begin" && msg.contains ("invocation") && msg["invocation"].is_object())
        {
            const auto name = toolName (text (msg["invocation"], "tool"));
            if (! name.empty())
                events.push_back ({ Event::Kind::tool, name });
        }
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
    const auto mcpTools = json::parse (toolListJson, nullptr, false);
    if (mcpTools.is_array())
        for (const auto& t : mcpTools)
        {
            if (! t.is_object())
                continue;
            const auto name = text (t, "name");
            if (name.empty())
                continue;
            // A tool with no arguments can arrive with a null input schema. OpenAI-style
            // APIs reject that, so every schema sent to a model is a JSON object.
            auto schema = t.contains ("inputSchema") && t["inputSchema"].is_object()
                            ? t["inputSchema"] : json { { "type", "object" } };
            if (! schema.contains ("type") || ! schema["type"].is_string())
                schema["type"] = "object";
            if (! schema.contains ("properties") || ! schema["properties"].is_object())
                schema["properties"] = json::object();

            if (settings.provider == ApiSettings::Provider::anthropic)
                list.push_back ({ { "name", name }, { "description", text (t, "description") }, { "input_schema", schema } });
            else
                list.push_back ({ { "type", "function" }, { "function", { { "name", name }, { "description", text (t, "description") }, { "parameters", schema } } } });
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

        if (! body.is_object())
            return fail ("The service gave an invalid JSON answer.");

        messages = json::parse (history);
        std::vector<std::pair<std::string, json>> calls; // id, {name, arguments}

        if (settings.provider == ApiSettings::Provider::anthropic)
        {
            const auto stop = text (body, "stop_reason");
            if (stop == "refusal")
                return fail ("The model declined this request.");
            const auto content = (body.contains ("content") && body["content"].is_array()) ? body["content"] : json::array();
            messages.push_back ({ { "role", "assistant" }, { "content", content } });
            for (const auto& block : content)
            {
                if (! block.is_object())
                    continue;
                const auto kind = text (block, "type");
                if (kind == "text" && ! text (block, "text").empty())
                    onEvent ({ Event::Kind::text, text (block, "text") });
                else if (kind == "tool_use")
                    calls.push_back ({ text (block, "id"), { { "name", text (block, "name") }, { "arguments", jsonArgs (block, "input") } } });
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
                const auto name = text (call, "name");
                if (! name.empty())
                    onEvent ({ Event::Kind::tool, name });
                json result { { "type", "tool_result" }, { "tool_use_id", id } };
                try
                {
                    if (name.empty())
                        throw std::runtime_error ("missing tool name");
                    result["content"] = runTool (name, text (call, "arguments"));
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
        if (! body.contains ("choices") || ! body["choices"].is_array() || body["choices"].empty()
            || ! body["choices"][0].is_object() || ! body["choices"][0].contains ("message") || ! body["choices"][0]["message"].is_object())
            return fail ("The service gave no answer.");
        auto msg = body["choices"][0]["message"];
        msg["role"] = "assistant";
        messages.push_back (msg);
        if (const auto content = text (msg, "content"); ! content.empty())
            onEvent ({ Event::Kind::text, content });
        if (msg.contains ("tool_calls") && msg["tool_calls"].is_array())
            for (const auto& c : msg["tool_calls"])
                if (c.is_object() && c.contains ("function") && c["function"].is_object())
                {
                    const auto& fn = c["function"];
                    calls.push_back ({ text (c, "id"), { { "name", text (fn, "name") }, { "arguments", jsonArgs (fn, "arguments") } } });
                }
        if (calls.empty())
        {
            history = messages.dump();
            onEvent ({ Event::Kind::done, {} });
            return;
        }
        for (const auto& [id, call] : calls)
        {
            const auto name = text (call, "name");
            if (! name.empty())
                onEvent ({ Event::Kind::tool, name });
            std::string result;
            try
            {
                if (name.empty())
                    throw std::runtime_error ("missing tool name");
                result = runTool (name, text (call, "arguments"));
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

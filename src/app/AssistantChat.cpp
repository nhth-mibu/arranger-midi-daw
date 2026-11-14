#include "AssistantChat.h"
#include "MidiAiService.h"

#include <nlohmann/json.hpp>

#include <juce_events/juce_events.h>

#if JUCE_MAC || JUCE_LINUX
 #include <sys/stat.h>
#endif

namespace arranger
{

using assistant::Event;

namespace
{
    constexpr const char* kAnthropicId = "api-anthropic";
    constexpr const char* kOpenAiId = "api-openai";

    void keepPrivate (const juce::File& f)
    {
#if JUCE_MAC || JUCE_LINUX
        ::chmod (f.getFullPathName().toRawUTF8(), 0600);
#else
        juce::ignoreUnused (f);
#endif
    }

    std::string utf8 (const juce::String& s) { return s.toStdString(); }
}

// ---- one message, on a background thread -------------------------------------------

class AssistantChat::Job final : public juce::Thread
{
public:
    Job (AssistantChat& o, juce::String text, std::function<void (const Event&)> callback)
        : juce::Thread ("Assistant"), owner (o), message (std::move (text)), onEvent (std::move (callback))
    {
    }

    ~Job() override
    {
        cancel();
        stopThread (4000);
    }

    void cancel()
    {
        cancelled = true;
        signalThreadShouldExit();
        const juce::ScopedLock lock (processLock);
        if (process != nullptr)
            process->kill();
    }

    // Program mode
    std::optional<assistant::Program> program;
    juce::String sessionId;
    std::vector<assistant::Message> earlier;

    // API mode
    assistant::ApiChat* api = nullptr;
    std::shared_ptr<SessionTools> tools;

private:
    void run() override
    {
        if (program)
            runProgram();
        else if (api != nullptr)
            runApi();
        post ({}, true);
    }

    /** Sends an event to the message thread; `last` tells the owner the job ended. */
    void post (const Event& e, bool last = false)
    {
        juce::MessageManager::callAsync ([alive = std::weak_ptr<bool> (owner.alive), chat = &owner, job = this, cb = onEvent, e, last] {
            if (alive.expired())
                return;
            if (last)
            {
                chat->finished (job);
                return;
            }
            if (e.kind == Event::Kind::session)
                chat->sessionId = juce::String::fromUTF8 (e.value.c_str());
            else if (e.kind == Event::Kind::text)
                chat->reply << juce::String::fromUTF8 (e.value.c_str());
            if (e.kind == Event::Kind::done || e.kind == Event::Kind::error)
            {
                if (chat->reply.isNotEmpty())
                    chat->history.push_back ({ false, chat->reply.trim().toStdString() });
                chat->reply.clear();
            }
            if (cb)
                cb (e);
        });
    }

    /** Runs a command and gives each line it writes. Returns the exit code, or -1 when it could not start. */
    int runCommand (const juce::StringArray& args, const juce::String& commandLine, const std::function<void (const std::string&)>& onLine)
    {
        auto p = std::make_unique<juce::ChildProcess>();
        const bool started = commandLine.isNotEmpty() ? p->start (commandLine, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)
                                                      : p->start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr);
        if (! started)
            return -1;
        {
            const juce::ScopedLock lock (processLock);
            process = p.get();
        }
        if (cancelled)
            p->kill();

        std::string pending;
        char buffer[256];
        for (;;)
        {
            const int got = p->readProcessOutput (buffer, static_cast<int> (sizeof (buffer)));
            if (got <= 0)
            {
                if (! p->isRunning() || cancelled)
                    break;
                juce::Thread::sleep (20);
                continue;
            }
            pending.append (buffer, static_cast<std::size_t> (got));
            for (auto nl = pending.find ('\n'); nl != std::string::npos; nl = pending.find ('\n'))
            {
                onLine (pending.substr (0, nl));
                pending.erase (0, nl + 1);
            }
        }
        if (! pending.empty())
            onLine (pending);
        p->waitForProcessToFinish (5000);
        const auto code = static_cast<int> (p->getExitCode());
        {
            const juce::ScopedLock lock (processLock);
            process = nullptr;
        }
        return code;
    }

    // ---- programs ----

    void runProgram()
    {
        const auto folder = workFolder();
        folder.createDirectory();
        const auto helper = McpService::helperProgram().getFullPathName();
        const auto mcpFile = folder.getChildFile ("mcp.json");
        mcpFile.replaceWithText (assistant::mcpConfig (utf8 (helper)));
        const auto gemini = folder.getChildFile (".gemini").getChildFile ("settings.json");
        gemini.getParentDirectory().createDirectory();
        gemini.replaceWithText (assistant::geminiSettings (utf8 (helper)));

        const bool resume = sessionId.isNotEmpty() && ! program->resumeArgs.empty();
        const auto outcome = attempt (resume, folder, helper, mcpFile);
        if (outcome == Outcome::failedBeforeAnswer && resume && ! cancelled)
        {
            // The program could not continue its conversation: start a new one with the messages so far.
            post ({ Event::Kind::session, {} });
            attempt (false, folder, helper, mcpFile);
        }
    }

    enum class Outcome
    {
        ok,
        failed,
        failedBeforeAnswer
    };

    Outcome attempt (bool resume, const juce::File& folder, const juce::String& helper, const juce::File& mcpFile)
    {
        const auto prompt = assistant::promptText (resume ? std::vector<assistant::Message> {} : earlier, utf8 (message), ! resume);
        const auto input = folder.getNonexistentChildFile ("message", ".txt", false);
        input.replaceWithText (juce::String::fromUTF8 (prompt.c_str()));

        std::map<std::string, std::string> values { { "helper", utf8 (helper) }, { "mcp_config", utf8 (mcpFile.getFullPathName()) } };
        if (resume)
            values["session"] = utf8 (sessionId);
        auto words = assistant::arguments (*program, values);
        words.insert (words.begin(), program->command);

#if JUCE_WINDOWS
        const auto line = assistant::shellCommand (utf8 (folder.getFullPathName()), words, utf8 (input.getFullPathName()), true);
        const auto commandLine = "cmd.exe /d /s /c \"" + juce::String::fromUTF8 (line.c_str()) + "\"";
        juce::StringArray args;
#else
        // Apps started from the Finder don't get the terminal's PATH: add the usual places.
        const juce::String paths = "export PATH=\"$HOME/.local/bin:$HOME/.claude/local:$HOME/.npm-global/bin:$HOME/.bun/bin:/opt/homebrew/bin:/usr/local/bin:$PATH\"; "
                                   "setopt nonomatch 2>/dev/null; for d in \"$HOME\"/.nvm/versions/node/*/bin; do [ -d \"$d\" ] && PATH=\"$d:$PATH\"; done; ";
        const auto line = paths + juce::String::fromUTF8 (assistant::shellCommand (utf8 (folder.getFullPathName()), words, utf8 (input.getFullPathName()), false).c_str());
        juce::String shell = juce::File ("/bin/zsh").existsAsFile() ? "/bin/zsh" : (juce::File ("/bin/bash").existsAsFile() ? "/bin/bash" : "/bin/sh");
        const juce::StringArray args { shell, "-lc", line };
        const juce::String commandLine;
#endif

        assistant::OutputReader reader (program->output);
        bool answered = false, failed = false;
        const auto deliver = [&] (const std::vector<Event>& events) {
            for (const auto& e : events)
            {
                if (e.kind == Event::Kind::text)
                    answered = true;
                if (e.kind == Event::Kind::error)
                {
                    failed = true;
                    // Retried without the old conversation: say nothing yet.
                    if (resume && ! answered)
                        continue;
                    auto why = e.value;
                    if (! program->help.empty())
                        why += "\n\n" + program->help;
                    post ({ Event::Kind::error, why });
                    continue;
                }
                post (e);
            }
        };

        const int code = runCommand (args, commandLine, [&] (const std::string& l) { deliver (reader.line (l)); });
        input.deleteFile();
        if (cancelled)
        {
            post ({ Event::Kind::error, "Stopped." });
            return Outcome::failed;
        }
        if (code == -1)
            deliver ({ { Event::Kind::error, "The program could not start." } });
        else
            deliver (reader.finish (code));
        if (! failed)
            return Outcome::ok;
        return answered ? Outcome::failed : Outcome::failedBeforeAnswer;
    }

    // ---- API ----

    void runApi()
    {
        const auto folder = workFolder();
        folder.createDirectory();

        const auto send = [this, folder] (const assistant::HttpRequest& r) -> assistant::HttpResponse {
            // curl comes with macOS, Windows 10 and later, and Linux. The key goes in a file, not on the command line.
            const auto body = folder.getNonexistentChildFile ("request", ".json", false);
            const auto headers = folder.getNonexistentChildFile ("headers", ".txt", false);
            body.replaceWithText (juce::String::fromUTF8 (r.body.c_str()));
            headers.create();
            keepPrivate (headers);
            juce::String headerText;
            for (const auto& [name, value] : r.headers)
                headerText << juce::String::fromUTF8 (name.c_str()) << ": " << juce::String::fromUTF8 (value.c_str()) << "\n";
            headers.replaceWithText (headerText);

#if JUCE_WINDOWS
            const juce::String curl = "curl.exe";
#else
            const juce::String curl = juce::File ("/usr/bin/curl").existsAsFile() ? "/usr/bin/curl" : "curl";
#endif
            const juce::StringArray args { curl, "-sS", "--max-time", "600", "-X", "POST", juce::String::fromUTF8 (r.url.c_str()),
                                           "-H", "@" + headers.getFullPathName(), "--data-binary", "@" + body.getFullPathName(),
                                           "-w", "\n%{http_code}" };
            std::string output;
            const int code = runCommand (args, {}, [&] (const std::string& l) { output += l + "\n"; });
            body.deleteFile();
            headers.deleteFile();

            assistant::HttpResponse response;
            while (! output.empty() && output.back() == '\n')
                output.pop_back();
            if (code != 0)
            {
                response.error = cancelled ? "Stopped." : (code == -1 ? "curl could not start." : "No connection: " + output.substr (0, output.rfind ('\n')));
                return response;
            }
            const auto last = output.rfind ('\n');
            response.status = juce::String (output.substr (last == std::string::npos ? 0 : last + 1)).getIntValue();
            response.body = last == std::string::npos ? std::string() : output.substr (0, last);
            return response;
        };

        const auto runTool = [this] (const std::string& name, const std::string& args) -> std::string {
            if (ai::isTool (name))
            {
                // The AI music tools run their helper here, not on the message thread.
                const auto message = nlohmann::json ({ { "jsonrpc", "2.0" }, { "id", 1 }, { "method", "tools/call" },
                                                       { "params", { { "name", name }, { "arguments", nlohmann::json::parse (args.empty() ? std::string ("{}") : args) } } } }).dump();
                const auto reply = MidiAiService::runToolCall (tools, message, nullptr, [this] { return cancelled.load(); });
                if (reply.empty())
                    throw std::runtime_error ("Stopped.");
                const auto result = nlohmann::json::parse (reply).at ("result");
                const auto text = result.at ("content").at (0).at ("text").get<std::string>();
                if (result.value ("isError", false))
                    throw std::runtime_error (text.rfind ("Error: ", 0) == 0 ? text.substr (7) : text);
                return text;
            }
            struct Answer
            {
                std::string text, error;
                juce::WaitableEvent ready;
            };
            auto answer = std::make_shared<Answer>();
            juce::MessageManager::callAsync ([t = tools, answer, name, args] {
                try
                {
                    answer->text = t->server().call (name, args);
                }
                catch (const std::exception& e)
                {
                    answer->error = e.what();
                    if (answer->error.empty())
                        answer->error = "the tool failed";
                }
                answer->ready.signal();
            });
            while (! answer->ready.wait (100))
                if (cancelled)
                    throw std::runtime_error ("Stopped.");
            if (! answer->error.empty())
                throw std::runtime_error (answer->error);
            return answer->text;
        };

        api->ask (utf8 (message), send, runTool, [this] (const Event& e) { post (e); }, [this] { return cancelled.load(); });
    }

    AssistantChat& owner;
    juce::String message;
    std::function<void (const Event&)> onEvent;
    std::atomic<bool> cancelled { false };
    juce::CriticalSection processLock;
    juce::ChildProcess* process = nullptr;
};

// ---- the chat ----------------------------------------------------------------------

AssistantChat::AssistantChat (Session& s, juce::PropertiesFile* p, std::function<bool()> start)
    : session (s), settings (p), startService (std::move (start)), tools (std::make_shared<SessionTools> (s))
{
    choiceId = settings != nullptr ? settings->getValue ("aiChoice", "claude-code") : juce::String ("claude-code");
    if (findProgram (choiceId) == nullptr && choiceId != kAnthropicId && choiceId != kOpenAiId)
        choiceId = "claude-code";
}

AssistantChat::~AssistantChat()
{
    alive.reset();
    job.reset();
}

juce::File AssistantChat::workFolder()
{
    return McpService::connectionFile().getSiblingFile ("Assistant");
}

juce::File AssistantChat::programsFile()
{
    return McpService::connectionFile().getSiblingFile ("assistants.json");
}

std::vector<assistant::Program> AssistantChat::programs() const
{
    auto list = assistant::builtInPrograms();
    const auto file = programsFile();
    if (file.existsAsFile())
    {
        try
        {
            for (auto& p : assistant::readPrograms (file.loadFileAsString().toStdString()))
            {
                std::erase_if (list, [&] (const assistant::Program& q) { return q.id == p.id; });
                list.push_back (std::move (p));
            }
        }
        catch (const std::exception& e)
        {
            DBG ("assistants.json: " << e.what());
        }
    }
    if (settings != nullptr)
        for (auto& p : list)
            if (const auto command = settings->getValue ("aiCommand." + juce::String (p.id)); command.isNotEmpty())
                p.command = command.toStdString();
    return list;
}

const assistant::Program* AssistantChat::findProgram (const juce::String& id) const
{
    static thread_local std::vector<assistant::Program> cache;
    cache = programs();
    for (const auto& p : cache)
        if (juce::String (p.id) == id)
            return &p;
    return nullptr;
}

juce::Array<AssistantChat::Choice> AssistantChat::choices() const
{
    juce::Array<Choice> list;
    for (const auto& p : programs())
        list.add ({ juce::String::fromUTF8 (p.id.c_str()), juce::String::fromUTF8 (p.name.c_str()), false });
    list.add ({ kAnthropicId, "Claude with an Anthropic API key", true });
    list.add ({ kOpenAiId, "Other AI with an API key (OpenAI, Gemini, Ollama...)", true });
    return list;
}

void AssistantChat::choose (const juce::String& id)
{
    if (id == choiceId)
        return;
    choiceId = id;
    if (settings != nullptr)
        settings->setValue ("aiChoice", id);
    newChat();
}

bool AssistantChat::chosenUsesApiKey() const
{
    return choiceId == kAnthropicId || choiceId == kOpenAiId;
}

juce::String AssistantChat::programCommand (const juce::String& id) const
{
    const auto* p = findProgram (id);
    return p != nullptr ? juce::String::fromUTF8 (p->command.c_str()) : juce::String();
}

void AssistantChat::setProgramCommand (const juce::String& id, const juce::String& command)
{
    if (settings != nullptr)
        settings->setValue ("aiCommand." + id, command.trim());
}

juce::String AssistantChat::programHelp (const juce::String& id) const
{
    const auto* p = findProgram (id);
    return p != nullptr ? juce::String::fromUTF8 (p->help.c_str()) : juce::String();
}

AssistantChat::ApiValues AssistantChat::apiValues (const juce::String& id) const
{
    ApiValues v;
    if (settings != nullptr)
    {
        v.url = settings->getValue ("aiApi." + id + ".url");
        v.model = settings->getValue ("aiApi." + id + ".model");
        v.key = settings->getValue ("aiApi." + id + ".key");
    }
    return v;
}

void AssistantChat::setApiValues (const juce::String& id, const ApiValues& v)
{
    if (settings != nullptr)
    {
        settings->setValue ("aiApi." + id + ".url", v.url.trim());
        settings->setValue ("aiApi." + id + ".model", v.model.trim());
        settings->setValue ("aiApi." + id + ".key", v.key.trim());
        settings->saveIfNeeded();
        keepPrivate (settings->getFile());
    }
    if (id == choiceId)
        api.reset(); // the next message uses the new values; the conversation starts again
}

assistant::ApiSettings AssistantChat::apiSettings() const
{
    const auto values = apiValues (choiceId);
    assistant::ApiSettings s;
    s.provider = choiceId == kAnthropicId ? assistant::ApiSettings::Provider::anthropic : assistant::ApiSettings::Provider::openAi;
    s.url = values.url.toStdString();
    s.model = values.model.toStdString();
    s.key = values.key.toStdString();
    if (s.key.empty())
        s.key = juce::SystemStats::getEnvironmentVariable (s.provider == assistant::ApiSettings::Provider::anthropic ? "ANTHROPIC_API_KEY" : "OPENAI_API_KEY", {}).toStdString();
    return s;
}

bool AssistantChat::isBusy() const
{
    return job != nullptr;
}

void AssistantChat::stop()
{
    if (job != nullptr)
        job->cancel();
}

void AssistantChat::newChat()
{
    job.reset();
    reply.clear();
    history.clear();
    sessionId.clear();
    api.reset();
}

void AssistantChat::finished (Job* j)
{
    if (job.get() != j)
        return;
    job.reset();
    if (onIdle)
        onIdle();
}

void AssistantChat::send (const juce::String& text, std::function<void (const Event&)> onEvent)
{
    if (job != nullptr || text.trim().isEmpty())
        return;

    const auto earlier = history;
    auto newJob = std::make_unique<Job> (*this, text.trim(), onEvent);

    if (chosenUsesApiKey())
    {
        const auto s = apiSettings();
        if (s.key.empty() && s.provider == assistant::ApiSettings::Provider::anthropic)
        {
            onEvent ({ Event::Kind::error, "Add your Anthropic API key first: click the settings button next to the assistant." });
            return;
        }
        if (api == nullptr)
            api = std::make_unique<assistant::ApiChat> (s, tools->server().toolList());
        newJob->api = api.get();
        newJob->tools = tools;
    }
    else
    {
        const auto* p = findProgram (choiceId);
        if (p == nullptr)
        {
            onEvent ({ Event::Kind::error, "Choose an assistant first." });
            return;
        }
        if (startService && ! startService())
        {
            onEvent ({ Event::Kind::error, "The app could not open its connection for AI assistants (Options > Allow AI Assistants)." });
            return;
        }
        newJob->program = *p;
        newJob->sessionId = sessionId;
        newJob->earlier = earlier;
    }

    history.push_back ({ true, text.trim().toStdString() });
    job = std::move (newJob);
    job->startThread();
}

} // namespace arranger

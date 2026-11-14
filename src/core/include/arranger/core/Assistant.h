#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

/**
    The chat box: talk to an AI assistant that edits the song with the tools of
    mcp::Server. Two ways to reach an assistant:

    - A program on this computer that uses your subscription (Claude Code,
      Codex, Gemini CLI, or one you add). The app runs it once for each
      message; the program reaches the song through the arranger-mcp helper.
    - An API key (Anthropic, or any service with an OpenAI-style chat API).
      The app sends the requests and runs the tools itself.

    This part has no input or output of its own: the app starts the programs
    and sends the web requests.
*/
namespace arranger::assistant
{

/** What a turn of the chat gives back, in order. */
struct Event
{
    enum class Kind
    {
        text,    ///< words from the assistant
        tool,    ///< the assistant uses a tool (value = tool name)
        session, ///< the program's conversation id, to continue it next time
        done,    ///< the turn finished
        error    ///< the turn failed (value = what happened)
    };
    Kind kind = Kind::text;
    std::string value;
};

/** One message of the conversation, as the chat box shows it. */
struct Message
{
    bool fromUser = true;
    std::string text;
};

/** What the assistant is told before the first message. */
std::string instructions();

/**
    The text sent to a program: the instructions, then (when the program
    can't continue a conversation by itself) the conversation so far, then
    the new message.
*/
std::string promptText (const std::vector<Message>& earlier, const std::string& message, bool withInstructions);

// ---- programs ---------------------------------------------------------------------

/** An assistant program on this computer. */
struct Program
{
    enum class Output
    {
        text,             ///< all that the program writes is the answer
        claudeStreamJson, ///< Claude Code: --output-format stream-json
        codexJson         ///< Codex: exec --json
    };

    std::string id;
    std::string name;
    std::string command; ///< program name (found on the PATH) or full path
    /**
        Words after the command for the first message. The message itself goes
        to the program's input. Placeholders: {helper} (the arranger-mcp path),
        {mcp_config} (a Claude-style MCP config file), {session}.
    */
    std::vector<std::string> args;
    /** Words for later messages, with {session}. Empty: the earlier messages go in the prompt. */
    std::vector<std::string> resumeArgs;
    Output output = Output::text;
    /** Shown when the program is missing or fails: how to install it and sign in. */
    std::string help;
};

/** Claude Code, Codex and Gemini CLI. */
std::vector<Program> builtInPrograms();

/**
    More programs from JSON: an array of objects with id, name, command, args,
    resumeArgs, output ("text", "claude-stream-json", "codex-json") and help.
    Throws std::runtime_error on bad JSON.
*/
std::vector<Program> readPrograms (const std::string& json);

/** The words to run, with the placeholders filled in. Resume words are used when `values` has a session. */
std::vector<std::string> arguments (const Program&, const std::map<std::string, std::string>& values);

/** One word for a shell command line: POSIX sh, or Windows cmd.exe. */
std::string shellQuote (const std::string& word, bool windows);

/**
    A command line that changes to `folder`, runs the words with `inputFile` as
    the input, and sends errors to the output too.
*/
std::string shellCommand (const std::string& folder, const std::vector<std::string>& words, const std::string& inputFile, bool windows);

/** The Claude-style MCP config file for {mcp_config}: {"mcpServers": {"arranger-midi-daw": {"command": helper}}}. */
std::string mcpConfig (const std::string& helper);

/** Gemini CLI's settings file (.gemini/settings.json in the work folder): the helper as a trusted MCP server. */
std::string geminiSettings (const std::string& helper);

/** Reads what a program writes, one line at a time. */
class OutputReader
{
public:
    explicit OutputReader (Program::Output);

    std::vector<Event> line (const std::string&);
    /** After the program stopped. Ends with done or error. */
    std::vector<Event> finish (int exitCode);

private:
    Program::Output output;
    std::string allText;           ///< text output, and other lines for errors
    std::vector<std::string> tail; ///< the last lines, for an error message
    bool gotText = false, failed = false, finished = false;
};

// ---- API keys ---------------------------------------------------------------------

struct ApiSettings
{
    enum class Provider
    {
        anthropic,
        openAi ///< any service with an OpenAI-style chat completions API
    };
    Provider provider = Provider::anthropic;
    std::string url;   ///< empty: the provider's own address
    std::string key;
    std::string model; ///< empty: the provider's default
};

struct HttpRequest
{
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

struct HttpResponse
{
    int status = 0;     ///< 0 when no answer came
    std::string body;
    std::string error;  ///< when no answer came
};

/**
    A conversation through an API: sends the message with the tools, runs the
    tools the model asks for, and repeats until the model answers.
*/
class ApiChat
{
public:
    using Send = std::function<HttpResponse (const HttpRequest&)>;
    /** Runs a tool. Returns the result text; throws std::exception on errors. */
    using RunTool = std::function<std::string (const std::string& name, const std::string& argumentsJson)>;
    using OnEvent = std::function<void (const Event&)>;

    /** `toolListJson` is mcp::Server::toolList(). */
    ApiChat (ApiSettings, const std::string& toolListJson);

    void ask (const std::string& message, const Send&, const RunTool&, const OnEvent&, const std::function<bool()>& cancelled = {});
    void clear();

    /** The request body for the conversation so far (tests). */
    std::string requestBody() const;
    HttpRequest request() const;

    static std::string defaultModel (ApiSettings::Provider);
    static std::string defaultUrl (ApiSettings::Provider);

private:
    ApiSettings settings;
    std::string tools;    ///< the provider's tool list as JSON
    std::string history;  ///< the messages as JSON
};

} // namespace arranger::assistant

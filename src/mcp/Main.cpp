// arranger-mcp: the program an AI assistant (Claude Desktop, Claude Code) starts
// to talk to Arranger MIDI DAW. It speaks MCP on standard input and output, one
// JSON-RPC message per line. It answers the handshake and the tool list itself,
// and sends each tool call to the running app over a local socket.

#include "arranger/core/McpServer.h"

#include <juce_core/juce_core.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <memory>
#include <string>

namespace
{
    using nlohmann::json;

    /** The same file the app writes (McpService::connectionFile). */
    juce::File connectionFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
            .getChildFile ("Application Support")
#endif
            .getChildFile ("Arranger MIDI DAW")
            .getChildFile ("mcp-connection.json");
    }

    /** A host with an empty song: enough to list the tools when the app is not open. */
    struct NoHost final : arranger::mcp::Host
    {
        arranger::Song empty;
        const arranger::Song& song() const override { return empty; }
        std::uint64_t songRevision() const override { return 0; }
        void edit (const std::string&, const std::function<void (arranger::Song&)>&) override {}
    };

    class AppLink
    {
    public:
        /** Sends one line to the app and returns its answer; empty when the app can't be reached. */
        std::string ask (const std::string& line)
        {
            for (int attempt = 0; attempt < 2; ++attempt)
            {
                if (socket == nullptr && ! connect())
                    return {};
                if (send (line))
                {
                    std::string reply;
                    if (receive (reply))
                        return reply;
                }
                socket.reset(); // the app closed: connect again once
                pending.clear();
            }
            return {};
        }

    private:
        bool connect()
        {
            const auto info = juce::JSON::parse (connectionFile());
            const int port = info.getProperty ("port", 0);
            const auto key = info.getProperty ("key", "").toString();
            if (port <= 0 || key.isEmpty())
                return false;
            auto s = std::make_unique<juce::StreamingSocket>();
            if (! s->connect ("127.0.0.1", port, 3000))
                return false;
            socket = std::move (s);
            if (! send (key.toStdString()))
            {
                socket.reset();
                return false;
            }
            return true;
        }

        bool send (const std::string& line)
        {
            const auto text = line + "\n";
            return socket->write (text.data(), static_cast<int> (text.size())) == static_cast<int> (text.size());
        }

        bool receive (std::string& line)
        {
            for (;;)
            {
                if (const auto n = pending.find ('\n'); n != std::string::npos)
                {
                    line = pending.substr (0, n);
                    pending.erase (0, n + 1);
                    return true;
                }
                // Wait for the answer: a tool can take a while (the app may show a dialog).
                const int ready = socket->waitUntilReady (true, 600000);
                if (ready <= 0)
                    return false;
                char buffer[8192];
                const int got = socket->read (buffer, static_cast<int> (sizeof (buffer)), false);
                if (got <= 0)
                    return false;
                pending.append (buffer, static_cast<std::size_t> (got));
            }
        }

        std::unique_ptr<juce::StreamingSocket> socket;
        std::string pending;
    };

    std::string toolError (const json& id, const std::string& text)
    {
        return json ({ { "jsonrpc", "2.0" }, { "id", id },
                       { "result", { { "content", { { { "type", "text" }, { "text", text } } } }, { "isError", true } } } })
            .dump();
    }

    constexpr const char* kNotOpen = "Arranger MIDI DAW is not open, or AI assistants are off. Open the app and select "
                                     "Options > Allow AI Assistants. Then try again.";
}

int main()
{
    std::ios::sync_with_stdio (false);

    NoHost none;
    arranger::mcp::Server local (none);
    AppLink app;

    std::string line;
    while (std::getline (std::cin, line))
    {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        json message;
        try
        {
            message = json::parse (line);
        }
        catch (const json::exception&)
        {
            std::cout << local.handle (line) << '\n' << std::flush;
            continue;
        }
        std::string reply;
        if (message.is_object() && message.value ("method", std::string()) == "tools/call" && message.contains ("id"))
        {
            reply = app.ask (line);
            if (reply.empty())
                reply = toolError (message["id"], kNotOpen);
        }
        else
            reply = local.handle (line); // the handshake, the tool list, ping
        if (! reply.empty())
            std::cout << reply << '\n' << std::flush;
    }
    return 0;
}

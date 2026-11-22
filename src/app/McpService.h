#pragma once

#include "Session.h"

#include "arranger/core/McpServer.h"

#include <juce_core/juce_core.h>

#include <memory>

namespace arranger
{

/** The tools of mcp::Server, working on the session. Use them on the message thread only. */
class SessionTools
{
public:
    explicit SessionTools (Session&);
    ~SessionTools();
    mcp::Server& server() noexcept;
    bool isAlive() const noexcept;

    /** Where the assistant looks for style files (Style > Choose Style Folder). */
    static void setStyleFolder (const juce::File&);
    static juce::File styleFolder();

    /** The device files (built-in and the user's) and how to save one; the main window sets these. */
    static std::function<std::vector<mcp::Host::DeviceFile>()> listDeviceFiles;
    static std::function<void (const std::string& name, const std::string& text)> saveDeviceFile;

private:
    struct Host;
    std::weak_ptr<std::atomic<bool>> sessionAlive;
    std::unique_ptr<Host> host;
    std::unique_ptr<mcp::Server> mcpServer;
};

/**
    Lets AI assistants change the song: a server on this computer only
    (127.0.0.1) that runs the tools of mcp::Server on the session. The
    arranger-mcp helper, which an assistant such as Claude Desktop starts,
    connects to it. Each connection must first send the key from the
    connection file (see connectionFile()), so other programs can't use it.

    Every tool runs on the message thread, as one undo step.
*/
class McpService final : private juce::Thread
{
public:
    explicit McpService (Session&);
    ~McpService() override;

    bool isRunning() const noexcept { return port > 0; }
    int getPort() const noexcept { return port; }

    /** Where the port and the key are written while the service runs: {"port": 47863, "key": "..."}. */
    static juce::File connectionFile();
    static void setConnectionFile (const juce::File&);
    /** The path of the helper program next to the app. */
    static juce::File helperProgram();

private:
    class Connection;

    void run() override;
    /** Runs one message on the message thread and returns the reply line. */
    std::string handleOnMessageThread (const std::string& line, juce::Thread& caller);

    Session& session;
    std::shared_ptr<SessionTools> tools; ///< shared with calls still waiting on the message thread
    juce::StreamingSocket listener;
    juce::String key;
    int port = 0;
    juce::OwnedArray<Connection> connections;
    juce::CriticalSection connectionLock;
};

} // namespace arranger

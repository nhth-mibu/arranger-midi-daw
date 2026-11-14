#include "McpService.h"

#if JUCE_MAC || JUCE_LINUX
 #include <sys/stat.h>
#endif

namespace arranger
{

namespace
{
    constexpr int kFirstPort = 47863;
    constexpr int kPortsToTry = 20;

    /** Reads one line (without the newline) from a socket. False when the socket closed. */
    bool readLine (juce::StreamingSocket& socket, std::string& pending, std::string& line, juce::Thread& thread)
    {
        for (;;)
        {
            if (const auto newline = pending.find ('\n'); newline != std::string::npos)
            {
                line = pending.substr (0, newline);
                pending.erase (0, newline + 1);
                if (! line.empty() && line.back() == '\r')
                    line.pop_back();
                return true;
            }
            if (thread.threadShouldExit())
                return false;
            const int ready = socket.waitUntilReady (true, 200);
            if (ready < 0)
                return false;
            if (ready == 0)
                continue;
            char buffer[4096];
            const int got = socket.read (buffer, static_cast<int> (sizeof (buffer)), false);
            if (got <= 0)
                return false;
            pending.append (buffer, static_cast<std::size_t> (got));
            if (pending.size() > 16 * 1024 * 1024)
                return false; // nobody sends a line this long
        }
    }

    bool writeLine (juce::StreamingSocket& socket, const std::string& text)
    {
        const auto line = text + "\n";
        return socket.write (line.data(), static_cast<int> (line.size())) == static_cast<int> (line.size());
    }
}

struct SessionTools::Host final : mcp::Host
{
    explicit Host (Session& s) : session (s) {}
    const Song& song() const override { return session.song(); }
    void edit (const std::string& d, const std::function<void (Song&)>& fn) override { session.edit (d, fn); }
    bool undo() override
    {
        if (! session.doc().canUndo())
            return false;
        session.undo();
        return true;
    }
    bool redo() override
    {
        if (! session.doc().canRedo())
            return false;
        session.redo();
        return true;
    }
    void play() override
    {
        if (! session.isPlaying())
            session.togglePlay();
    }
    void stop() override { session.stop(); }
    void locate (Tick t) override { session.locate (t); }
    bool isPlaying() const override { return session.isPlaying(); }
    double position() const override { return session.position(); }
    void launchSlot (TrackId t, int scene) override { session.launchSlot (t, scene); }
    void launchScene (int scene) override { session.launchScene (scene); }
    void stopSlots() override { session.stopAllSlots(); }

    Session& session;
};

SessionTools::SessionTools (Session& s) : host (std::make_unique<Host> (s)), mcpServer (std::make_unique<mcp::Server> (*host)) {}
SessionTools::~SessionTools() = default;
mcp::Server& SessionTools::server() noexcept { return *mcpServer; }

class McpService::Connection final : public juce::Thread
{
public:
    Connection (McpService& o, std::unique_ptr<juce::StreamingSocket> s) : juce::Thread ("MCP connection"), owner (o), socket (std::move (s)) {}
    ~Connection() override
    {
        socket->close();
        stopThread (2000);
    }

    bool finished() const noexcept { return done; }

    void run() override
    {
        std::string pending, line;
        // The first line is the key.
        if (! readLine (*socket, pending, line, *this) || juce::String (line).trim() != owner.key)
        {
            writeLine (*socket, R"({"jsonrpc":"2.0","id":null,"error":{"code":-32001,"message":"Wrong key"}})");
            done = true;
            return;
        }
        while (! threadShouldExit() && readLine (*socket, pending, line, *this))
        {
            if (line.empty())
                continue;
            const auto reply = owner.handleOnMessageThread (line, *this);
            if (! reply.empty() && ! writeLine (*socket, reply))
                break;
        }
        done = true;
    }

private:
    McpService& owner;
    std::unique_ptr<juce::StreamingSocket> socket;
    std::atomic<bool> done { false };
};

McpService::McpService (Session& s) : juce::Thread ("MCP service"), session (s)
{
    tools = std::make_shared<SessionTools> (session);
    key = juce::Uuid().toDashedString().removeCharacters ("-");

    for (int p = kFirstPort; p < kFirstPort + kPortsToTry; ++p)
        if (listener.createListener (p, "127.0.0.1"))
        {
            port = p;
            break;
        }
    if (port == 0)
        return;

    const auto file = connectionFile();
    file.getParentDirectory().createDirectory();
    juce::DynamicObject::Ptr info = new juce::DynamicObject();
    info->setProperty ("port", port);
    info->setProperty ("key", key);
    file.replaceWithText (juce::JSON::toString (juce::var (info.get())));
    // Only this user may read the key.
#if JUCE_MAC || JUCE_LINUX
    ::chmod (file.getFullPathName().toRawUTF8(), 0600);
#endif
    startThread();
}

McpService::~McpService()
{
    signalThreadShouldExit();
    listener.close();
    stopThread (3000);
    {
        const juce::ScopedLock lock (connectionLock);
        connections.clear();
    }
    if (port > 0)
        connectionFile().deleteFile();
}

juce::File McpService::connectionFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")
#endif
        .getChildFile ("Arranger MIDI DAW")
        .getChildFile ("mcp-connection.json");
}

juce::File McpService::helperProgram()
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
#if JUCE_WINDOWS
    return exe.getSiblingFile ("arranger-mcp.exe");
#else
    return exe.getSiblingFile ("arranger-mcp");
#endif
}

void McpService::run()
{
    while (! threadShouldExit())
    {
        std::unique_ptr<juce::StreamingSocket> client (listener.waitForNextConnection());
        if (client == nullptr)
            continue;
        const juce::ScopedLock lock (connectionLock);
        for (int i = connections.size(); --i >= 0;)
            if (connections[i]->finished())
                connections.remove (i);
        auto* c = connections.add (std::make_unique<Connection> (*this, std::move (client)));
        c->startThread();
    }
}

std::string McpService::handleOnMessageThread (const std::string& line, juce::Thread& caller)
{
    // Shared, so a late answer after the caller gave up does not write to freed memory.
    struct Answer
    {
        std::string reply;
        juce::WaitableEvent ready;
    };
    auto answer = std::make_shared<Answer>();
    juce::MessageManager::callAsync ([tools = tools, answer, line] {
        try
        {
            answer->reply = tools->server().handle (line);
        }
        catch (const std::exception& e)
        {
            answer->reply = std::string (R"({"jsonrpc":"2.0","id":null,"error":{"code":-32603,"message":")")
                          + juce::String (e.what()).replace ("\"", "'").toStdString() + "\"}}";
        }
        answer->ready.signal();
    });
    // The app may be busy (a dialog, a long load); the helper waits as long as it needs.
    while (! answer->ready.wait (100))
        if (caller.threadShouldExit())
            return {};
    return answer->reply;
}

} // namespace arranger

#include "McpService.h"
#include "Grooves.h"
#include "MessageThreadTask.h"
#include "MidiAiService.h"

#include "AudioAnalysis.h"
#include "StyleFromAudio.h"

#include "arranger/core/Style.h"

#if JUCE_MAC || JUCE_LINUX
 #include <fcntl.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

#include <cstring>

namespace arranger
{

namespace
{
    constexpr int kFirstPort = 47863;
    constexpr int kPortsToTry = 20;
    constexpr int kMaxConnections = 16;

    bool writePrivateText (const juce::File& file, const juce::String& text)
    {
        file.getParentDirectory().createDirectory();
#if JUCE_MAC || JUCE_LINUX
        const auto temp = file.getNonexistentSibling (false);
        const int fd = ::open (temp.getFullPathName().toRawUTF8(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0)
            return false;
        ::fchmod (fd, 0600);
        const auto* utf8 = text.toRawUTF8();
        const auto len = std::strlen (utf8);
        std::size_t written = 0;
        while (written < len)
        {
            const auto n = ::write (fd, utf8 + written, len - written);
            if (n <= 0)
            {
                ::close (fd);
                temp.deleteFile();
                return false;
            }
            written += static_cast<std::size_t> (n);
        }
        ::close (fd);
        if (::rename (temp.getFullPathName().toRawUTF8(), file.getFullPathName().toRawUTF8()) != 0)
        {
            temp.deleteFile();
            return false;
        }
        ::chmod (file.getFullPathName().toRawUTF8(), 0600);
        return true;
#else
        return file.replaceWithText (text);
#endif
    }

    /** Reads one line (without the newline) from a socket. False when the socket closed or timed out. */
    bool readLine (juce::StreamingSocket& socket, std::string& pending, std::string& line, juce::Thread& thread, int maxIdleMs = 0)
    {
        int idleMs = 0;
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
            {
                idleMs += 200;
                if (maxIdleMs > 0 && idleMs >= maxIdleMs)
                    return false;
                continue;
            }
            idleMs = 0;
            char buffer[4096];
            const int got = socket.read (buffer, static_cast<int> (sizeof (buffer)), false);
            if (got <= 0)
                return false;
            pending.append (buffer, static_cast<std::size_t> (got));
            if (pending.size() > 16 * 1024 * 1024)
                return false; // nobody sends a line this long
        }
    }

    bool writeLine (juce::StreamingSocket& socket, const std::string& text, juce::Thread& thread)
    {
        const auto line = text + "\n";
        std::size_t sent = 0;
        int idleMs = 0;
        while (sent < line.size())
        {
            if (thread.threadShouldExit())
                return false;
            const int ready = socket.waitUntilReady (false, 200);
            if (ready < 0)
                return false;
            if (ready == 0)
            {
                idleMs += 200;
                if (idleMs >= 5000)
                    return false;
                continue;
            }
            idleMs = 0;
            const int n = socket.write (line.data() + sent, static_cast<int> (line.size() - sent));
            if (n <= 0)
                return false;
            sent += static_cast<std::size_t> (n);
        }
        return true;
    }
}

namespace
{
    /** The style folder and the style files found in it, kept for a while: a folder on a NAS is slow to read. */
    struct StyleFiles
    {
        juce::CriticalSection lock;
        juce::File folder;
        std::vector<std::string> files;
        juce::uint32 scannedAt = 0;
        bool scanned = false;
    };

    StyleFiles& styleFileList()
    {
        static StyleFiles list;
        return list;
    }

    constexpr int kMostStyleFiles = 100000;
    constexpr juce::uint32 kRescanAfterMs = 10 * 60 * 1000;

    struct ConnectionFileConfig
    {
        juce::CriticalSection lock;
        juce::File overrideFile;
    };

    ConnectionFileConfig& connectionFileConfig()
    {
        static ConnectionFileConfig cfg;
        return cfg;
    }
}

std::function<std::vector<mcp::Host::DeviceFile>()> SessionTools::listDeviceFiles;
std::function<void (const std::string&, const std::string&)> SessionTools::saveDeviceFile;

void SessionTools::setStyleFolder (const juce::File& folder)
{
    auto& list = styleFileList();
    const juce::ScopedLock lock (list.lock);
    if (folder == list.folder)
        return;
    list.folder = folder;
    list.files.clear();
    list.scanned = false;
}

juce::File SessionTools::styleFolder()
{
    auto& list = styleFileList();
    const juce::ScopedLock lock (list.lock);
    return list.folder;
}

struct SessionTools::Host final : mcp::Host
{
    explicit Host (Session& s) : session (s) {}
    const Song& song() const override { return session.song(); }
    std::uint64_t songRevision() const override { return session.doc().revision(); }
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
    std::optional<KidsMode> kidsMode() const override
    {
        const auto& k = session.kidsModeSettings();
        return KidsMode { session.kidsMode(), k.pauseAfterSeconds, k.keysSound };
    }
    void setKidsMode (const KidsMode& k) override
    {
        session.setKidsModeSettings ({ k.pauseAfterSeconds, k.keysSound });
        session.setKidsMode (k.on);
    }
    void launchSlot (TrackId t, int scene) override { session.launchSlot (t, scene); }
    void launchScene (int scene) override { session.launchScene (scene); }
    void stopSlots() override { session.stopAllSlots(); }

    std::vector<std::string> midiOutputs() const override
    {
        std::vector<std::string> names;
        for (const auto& p : session.deviceHost().midiOutputs())
            names.push_back (p.name);
        return names;
    }

    const DeviceLibrary* devices() const override { return &session.deviceLibrary(); }
    bool sendMidiOutput (const std::string& port, const std::vector<std::uint8_t>& bytes) override
    {
        session.deviceHost().sendNow (port, bytes);
        return true;
    }
    std::string musicFinderFolder() const override
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("Music Finder").getFullPathName().toStdString();
    }

    std::vector<groove::Groove> grooves() const override { return grooves::load(); }
    void saveGroove (const groove::Groove& g) override { grooves::save (g); }
    std::vector<groove::Hit> audioClipHits (ClipId id) override { return grooves::hitsInClip (session.song(), id); }
    std::vector<groove::Hit> audioFileHits (const std::string& path) override { return grooves::hitsInFile (path); }
    std::shared_ptr<const Sample> audioSample (const std::string& path) override { return session.sampleFor (path); }
    void audition (const Song* version, Tick from, Tick to) override
    {
        if (version == nullptr)
            session.audition (std::nullopt);
        else
            session.audition (*version, from, to);
    }

    const arranging::Preferences* arrangingPreferences() const override { return &session.arrangingPreferences(); }
    void rememberArrangingChoice (const arranging::Proposal& p, int option) override { session.rememberArrangingChoice (p, option); }
    void setDeviceCapabilities (const std::string& id, const DeviceCapabilities& c) override { session.setDeviceCapabilities (id, c); }

    std::vector<MidiInput> midiInputs() const override
    {
        std::vector<MidiInput> list;
        for (const auto& p : session.deviceHost().midiInputs())
            list.push_back ({ p.name, p.enabled });
        return list;
    }

    std::vector<ControllerProfile> controllers() const override { return session.controllerPages(); }

    std::string skillsFolder() const override
    {
        auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
            .getChildFile ("Application Support")
#endif
            .getChildFile ("Arranger MIDI DAW")
            .getChildFile ("Skills");
        folder.createDirectory();
        return folder.getFullPathName().toStdString();
    }

    bool setMidiInputEnabled (const std::string& name, bool on) override
    {
        const bool done = session.deviceHost().setMidiInputEnabled (name, on);
        if (done)
            session.sendChangeMessage();
        return done;
    }

    std::string controllerPageFor (const std::string& input) const override
    {
        const auto* c = session.controllerFor (input);
        return c != nullptr ? c->id : std::string();
    }

    void assignDeviceProfile (const std::string& output, const std::string& id) override { session.assignDeviceProfile (output, id); }
    void assignControllerPage (const std::string& input, const std::string& id) override { session.assignControllerPage (input, id); }

    std::vector<DeviceFile> deviceFiles() const override
    {
        return listDeviceFiles ? listDeviceFiles() : std::vector<DeviceFile> {};
    }

    void writeDeviceFile (const std::string& name, const std::string& text) override
    {
        if (! saveDeviceFile)
            throw std::runtime_error ("device files can't be saved here");
        saveDeviceFile (name, text);
    }

    std::vector<ReceivedMidi> recentMidi (double seconds) const override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        std::vector<ReceivedMidi> list;
        for (const auto& m : session.recentMidi (seconds))
            list.push_back ({ (now - m.ms) / 1000.0, m.message.port, std::vector<std::uint8_t> (m.message.bytes.begin(), m.message.bytes.begin() + m.message.size) });
        return list;
    }

    bool sendSynthParameter (const std::string& profileId, const std::string& pageName, const std::string& paramName, int value) override
    {
        const auto* p = session.deviceLibrary().byId (profileId);
        if (p == nullptr)
            return false;
        for (const auto& pg : p->editor.pages)
            if (pg.name == pageName)
                for (const auto& param : pg.params)
                    if (param.name == paramName)
                        return session.sendSynthParameter (*p, pg, param, value);
        return false;
    }

    int synthParameter (const std::string& profileId, const std::string& page, const std::string& param) const override
    {
        return session.synthValue (profileId, page + "/" + param).value_or (-1);
    }

    std::string readSoundNames (const std::string& profileId) override
    {
        return session.readSoundNames (profileId).toStdString();
    }

    std::string startAnalysis (const AnalysisRequest& r) override
    {
        AnalysisService::Request req;
        req.clip = r.clip;
        req.stems = r.stems;
        req.notes = r.notes;
        req.chords = r.chords;
        req.instruments = r.instruments;
        req.lyrics = r.lyrics;
        req.language = juce::String::fromUTF8 (r.language.c_str());
        req.prompt = juce::String::fromUTF8 (r.prompt.c_str());
        return session.analysis().start (req).toStdString();
    }

    AnalysisStatus analysisStatus() const override
    {
        const auto st = session.analysis().status();
        AnalysisStatus out;
        using State = AnalysisService::Status::State;
        out.state = st.state == State::running ? "running" : st.state == State::done ? "done" : st.state == State::failed ? "failed" : "idle";
        out.clip = st.clip;
        out.progress = st.progress;
        out.step = st.step.toStdString();
        out.error = st.error.toStdString();
        out.language = st.language;
        out.applied = st.applied;
        out.warnings = st.warnings;
        out.chords = st.chords;
        out.lyricLines = st.lyricLines;
        out.instruments = st.instruments;
        return out;
    }

    TranscriptionQuestions transcriptionQuestions (ClipId clip) override
    {
        auto& analysis = session.analysis();
        TranscriptionQuestions out;
        out.clip = clip != 0 ? clip : analysis.questionClip();
        if (out.clip == 0)
        {
            out.why = "No analysed audio clip: run analyse_audio first (with notes and chords).";
            return out;
        }
        juce::String why;
        out.questions = analysis.questionsFor (out.clip, why);
        out.why = why.toStdString();
        return out;
    }

    clarify::Changes answerTranscriptionQuestion (ClipId clip, const clarify::Question& q, int choice, bool similar) override
    {
        juce::String error;
        auto changes = session.analysis().answer (clip, q, choice, similar, error);
        if (error.isNotEmpty())
            throw std::runtime_error (error.toStdString());
        return changes;
    }

    void playTranscriptionChoice (ClipId clip, const clarify::Question& q, int choice) override
    {
        if (choice < -1)
        {
            session.audition (std::nullopt);
            return;
        }
        const auto error = session.analysis().listen (clip, q, choice);
        if (error.isNotEmpty())
            throw std::runtime_error (error.toStdString());
    }

    std::string startStyleFromAudio (const StyleFromAudioRequest& r) override
    {
        StyleFromAudioService::Request req;
        req.clip = r.clip;
        req.bpm = r.bpm;
        req.mainBars = r.mainBars;
        req.vocals = r.vocals;
        req.name = r.name;
        req.link = r.link;
        return session.styleFromAudio().start (req).toStdString();
    }

    StyleFromAudioStatus styleFromAudioStatus() const override
    {
        const auto st = session.styleFromAudio().status();
        using State = StyleFromAudioService::Status::State;
        StyleFromAudioStatus out;
        out.state = st.state == State::downloading ? "downloading" : st.state == State::analysing ? "analysing" : st.state == State::making ? "making" : st.state == State::done ? "done" : st.state == State::failed ? "failed" : "idle";
        out.clip = st.clip;
        out.progress = st.progress;
        out.step = st.step.toStdString();
        out.error = st.error.toStdString();
        out.made = st.made;
        if (st.saved != juce::File())
            out.savedTo = st.saved.getFullPathName().toStdString();
        return out;
    }

    std::string saveStyleFromAudio (const std::string& path) override
    {
        if (path.find ('\0') != std::string::npos)
            throw std::runtime_error ("The style path must stay inside the Styles folder.");
        juce::File file = session.styleFromAudio().confinedStyleFile (juce::String::fromUTF8 (path.c_str()));
        const auto error = session.styleFromAudio().save (file);
        if (error.isNotEmpty())
            throw std::runtime_error (error.toStdString());
        return file.getFullPathName().toStdString();
    }

    void playStyleFromAudio (const std::string& listen, int fromBar, int toBar) override
    {
        auto& service = session.styleFromAudio();
        if (listen == "stop")
        {
            service.stopPlaying();
            return;
        }
        const auto how = listen == "original" ? style_from_audio::Listen::original : listen == "style" ? style_from_audio::Listen::style : style_from_audio::Listen::both;
        const auto error = service.play (how, fromBar, toBar);
        if (error.isNotEmpty())
            throw std::runtime_error (error.toStdString());
    }

    std::vector<std::string> styleFiles() const override
    {
        auto& list = styleFileList();
        const juce::ScopedLock lock (list.lock);
        const auto now = juce::Time::getMillisecondCounter();
        if (list.scanned && now - list.scannedAt < kRescanAfterMs)
            return list.files;
        list.files.clear();
        if (list.folder.isDirectory())
            for (const auto& entry : juce::RangedDirectoryIterator (list.folder, true, "*", juce::File::findFiles))
            {
                if (style_file::hasStyleExtension (entry.getFile().getFileName().toStdString()))
                    list.files.push_back (entry.getFile().getFullPathName().toStdString());
                if (static_cast<int> (list.files.size()) >= kMostStyleFiles)
                    break;
            }
        list.scanned = true;
        list.scannedAt = now;
        return list.files;
    }

    Session& session;
};

SessionTools::SessionTools (Session& s)
    : sessionAlive (s.lifetime()), host (std::make_unique<Host> (s)), mcpServer (std::make_unique<mcp::Server> (*host))
{
}
SessionTools::~SessionTools() = default;
mcp::Server& SessionTools::server() noexcept { return *mcpServer; }
bool SessionTools::isAlive() const noexcept
{
    const auto token = sessionAlive.lock();
    return token != nullptr && token->load();
}

class McpService::Connection final : public juce::Thread
{
public:
    Connection (McpService& o, std::unique_ptr<juce::StreamingSocket> s) : juce::Thread ("MCP connection"), owner (o), socket (std::move (s)) {}
    ~Connection() override
    {
        signalThreadShouldExit();
        stopThread (2000);
        if (socket != nullptr)
            socket->close();
    }

    bool finished() const noexcept { return done; }

    void run() override
    {
        std::string pending, line;
        // The first line is the key.
        if (! readLine (*socket, pending, line, *this, 5000) || juce::String (line).trim() != owner.key)
        {
            writeLine (*socket, R"({"jsonrpc":"2.0","id":null,"error":{"code":-32001,"message":"Wrong key"}})", *this);
            done = true;
            return;
        }
        while (! threadShouldExit() && readLine (*socket, pending, line, *this))
        {
            if (line.empty())
                continue;
            // AI tools run their helper on this thread, so the window goes on while the model works.
            auto reply = MidiAiService::runToolCall (owner.tools, line, this, [this] { return threadShouldExit(); });
            if (reply.empty())
                reply = owner.handleOnMessageThread (line, *this);
            if (! reply.empty() && ! writeLine (*socket, reply, *this))
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
    juce::DynamicObject::Ptr info = new juce::DynamicObject();
    info->setProperty ("port", port);
    info->setProperty ("key", key);
    // Created with mode 0600 from birth so no world-readable window exists.
    writePrivateText (file, juce::JSON::toString (juce::var (info.get())));
    startThread();
}

McpService::~McpService()
{
    // Stop accepting, then tell every connection thread to finish so they let go of
    // their sockets promptly instead of each waiting out the stop timeout.
    signalThreadShouldExit();
    listener.close();
    stopThread (3000);
    {
        const juce::ScopedLock lock (connectionLock);
        for (auto* c : connections)
            c->signalThreadShouldExit();
        connections.clear();
    }
    if (port > 0)
        connectionFile().deleteFile();
}

void McpService::setConnectionFile (const juce::File& file)
{
    auto& cfg = connectionFileConfig();
    const juce::ScopedLock lock (cfg.lock);
    cfg.overrideFile = file;
}

juce::File McpService::connectionFile()
{
    {
        auto& cfg = connectionFileConfig();
        const juce::ScopedLock lock (cfg.lock);
        if (cfg.overrideFile != juce::File())
            return cfg.overrideFile;
    }
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
        if (connections.size() >= kMaxConnections)
        {
            client->close();
            continue;
        }
        auto* c = connections.add (std::make_unique<Connection> (*this, std::move (client)));
        c->startThread();
    }
}

std::string McpService::handleOnMessageThread (const std::string& line, juce::Thread& caller)
{
    auto reply = std::make_shared<std::string>();
    const bool ran = onMessageThread (
        [tools = tools, reply, line] {
            if (! tools->isAlive())
                return;
            try
            {
                *reply = tools->server().handle (line);
            }
            catch (const std::exception& e)
            {
                *reply = std::string (R"({"jsonrpc":"2.0","id":null,"error":{"code":-32603,"message":")")
                       + juce::String (e.what()).replace ("\"", "'").toStdString() + "\"}}";
            }
        },
        [&caller] { return caller.threadShouldExit(); });
    return ran ? *reply : std::string();
}

} // namespace arranger

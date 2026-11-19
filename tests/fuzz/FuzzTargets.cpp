// The fuzz targets: each takes any bytes and must never crash, hang or show
// undefined behaviour. A parser may refuse bad input with an exception; that
// is fine. Running out of memory (a size read from the file used as is) is not.

#include "FuzzTargets.h"

#include "arranger/core/ControllerProfile.h"
#include "arranger/core/DeviceProfile.h"
#include "arranger/core/Document.h"
#include "arranger/core/McpServer.h"
#include "arranger/core/MidiFile.h"
#include "arranger/core/MusicXml.h"
#include "arranger/core/ProjectFile.h"
#include "arranger/core/RecorderImport.h"
#include "arranger/core/Sample.h"
#include "arranger/core/Style.h"
#include "arranger/core/SynthEditor.h"

#include <nlohmann/json.hpp>

#include <new>
#include <sstream>
#include <stdexcept>

namespace arranger::fuzz
{

namespace
{
    std::string text (Bytes b) { return std::string (reinterpret_cast<const char*> (b.data()), b.size()); }

    /** Runs `fn`; a parser's own refusal is fine, running out of memory is a finding. */
    template <typename Fn>
    void refusalIsFine (Fn&& fn)
    {
        try
        {
            fn();
            ++acceptedCount();
        }
        catch (const std::bad_alloc&)
        {
            throw Finding ("std::bad_alloc: a size from the input was trusted");
        }
        catch (const std::length_error& e)
        {
            throw Finding (std::string ("std::length_error: ") + e.what());
        }
        catch (const std::exception&)
        {
        }
    }

    /** After a successful read, the song must save and load again. */
    void saveAndLoad (const Song& song)
    {
        const auto again = project_file::fromJson (project_file::toJson (song));
        if (! (again == song))
            throw Finding ("a song read from the input did not survive a save and load");
    }

    void projectFile (Bytes b)
    {
        refusalIsFine ([&] { saveAndLoad (project_file::fromJson (text (b))); });
    }

    void midiFile (Bytes b)
    {
        refusalIsFine ([&] { saveAndLoad (midi_file::read (b)); });
    }

    void musicXml (Bytes b)
    {
        refusalIsFine ([&] { saveAndLoad (musicxml::read (text (b))); });
    }

    void styleFile (Bytes b)
    {
        refusalIsFine ([&] {
            const auto style = style_file::read (b);
            saveAndLoad (style_convert::toSong (style));
        });
    }

    void wavFile (Bytes b)
    {
        refusalIsFine ([&] { (void) wav::parse (std::vector<std::uint8_t> (b.begin(), b.end())); });
    }

    void deviceProfile (Bytes b)
    {
        refusalIsFine ([&] {
            const auto profile = device_profile::parse (text (b));
            // Use what was read: an editor that parsed must handle any message from the synth.
            synth_edit::Listener listener (profile.editor, 1);
            (void) listener.take ({ 0xF0, 0x41, 0x10, 0x00, 0x00, 0x00, 0x00, 0xF7 });
            (void) listener.take ({ 0xB0, 0x63, 0x01, 0xB0, 0x62, 0x02, 0xB0, 0x06, 0x40 });
            (void) synth_edit::readRequests (profile.editor, 1);
        });
        refusalIsFine ([&] { (void) device_profile::parseAddOn (text (b)); });
        refusalIsFine ([&] { (void) device_profile::parseCapabilitiesJson (text (b)); });
    }

    void controllerProfile (Bytes b)
    {
        refusalIsFine ([&] { (void) controller_profile::parse (text (b)); });
    }

    void l6maxFolder (Bytes b)
    {
        std::vector<std::string> names;
        std::istringstream in (text (b));
        for (std::string line; std::getline (in, line);)
            names.push_back (line);
        refusalIsFine ([&] { (void) recorder_import::zoomL6Project (names); });
    }

    struct FuzzHost final : mcp::Host
    {
        Document doc;
        const Song& song() const override { return doc.song(); }
        std::uint64_t songRevision() const override { return doc.revision(); }
        void edit (const std::string& d, const std::function<void (Song&)>& fn) override { doc.edit (d, fn); }
        bool undo() override { return doc.undo(); }
        bool redo() override { return doc.redo(); }
        void play() override {}
        void stop() override {}
        void locate (Tick) override {}
        bool isPlaying() const override { return false; }
        double position() const override { return 0.0; }
        void launchScene (int) override {}
    };

    void mcpMessages (Bytes b)
    {
        // One server for all inputs of a run would carry state between them; one per
        // input keeps a finding reproducible from its file alone.
        FuzzHost host;
        mcp::Server server (host);
        std::istringstream in (text (b));
        for (std::string line; std::getline (in, line);)
            refusalIsFine ([&] {
                const auto reply = server.handle (line);
                if (! reply.empty())
                    [[maybe_unused]] const auto json = nlohmann::json::parse (reply); // the reply is always valid JSON
            });
        refusalIsFine ([&] { saveAndLoad (host.song()); });
    }
}

std::size_t& acceptedCount()
{
    static std::size_t n = 0;
    return n;
}

const std::vector<Target>& targets()
{
    static const std::vector<Target> all {
        { "project", "Project file (.arranger JSON)", projectFile },
        { "midi", "Standard MIDI file", midiFile },
        { "musicxml", "MusicXML", musicXml },
        { "style", "Yamaha style (.sty/.prs, SFF1/SFF2, CASM)", styleFile },
        { "wav", "WAV sample", wavFile },
        { "device", "Device profile YAML (with sound editors), add-on and capabilities JSON", deviceProfile },
        { "controller", "Controller page YAML", controllerProfile },
        { "l6max", "L6max recording folder (file names)", l6maxFolder },
        { "mcp", "MCP JSON-RPC messages, one per line", mcpMessages },
    };
    return all;
}

} // namespace arranger::fuzz

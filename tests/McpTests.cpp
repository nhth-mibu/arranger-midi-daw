#include "arranger/core/Document.h"
#include "arranger/core/McpServer.h"
#include "arranger/core/SongEdit.h"
#include "arranger/core/Style.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

using namespace arranger;
using nlohmann::json;

namespace
{
    constexpr Tick kBeat = kTicksPerQuarter;
    constexpr Tick kBar = kBeat * 4;

    struct TestHost final : mcp::Host
    {
        Document doc;
        Tick at = 0;
        bool playing = false;
        int launchedScene = -1;

        const Song& song() const override { return doc.song(); }
        std::uint64_t songRevision() const override { return doc.revision(); }
        void edit (const std::string& d, const std::function<void (Song&)>& fn) override { doc.edit (d, fn); }
        bool undo() override { return doc.undo(); }
        bool redo() override { return doc.redo(); }
        void play() override { playing = true; }
        void stop() override { playing = false; }
        void locate (Tick t) override { at = t; }
        bool isPlaying() const override { return playing; }
        double position() const override { return static_cast<double> (at); }
        void launchScene (int scene) override { launchedScene = scene; }
        KidsMode kids;
        std::optional<KidsMode> kidsMode() const override { return kids; }
        void setKidsMode (const KidsMode& k) override { kids = k; }
    };

    /** Calls a tool through the protocol, as a client does. Returns the parsed result, or the error text in "error". */
    json callTool (mcp::Server& server, const std::string& name, json args)
    {
        static int id = 1;
        const auto reply = json::parse (server.handle (json ({ { "jsonrpc", "2.0" }, { "id", ++id }, { "method", "tools/call" },
                                                               { "params", { { "name", name }, { "arguments", std::move (args) } } } }).dump()));
        const auto& result = reply.at ("result");
        const auto text = result.at ("content").at (0).at ("text").get<std::string>();
        if (result.at ("isError").get<bool>())
            return { { "error", text } };
        return json::parse (text);
    }
}

TEST_CASE ("MCP: the protocol", "[mcp]")
{
    TestHost host;
    mcp::Server server (host);

    const auto init = json::parse (server.handle (R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})"));
    CHECK (init["result"]["protocolVersion"] == "2025-06-18");
    CHECK (init["result"]["capabilities"].contains ("tools"));
    CHECK (init["result"]["serverInfo"]["name"] == "arranger-midi-daw");

    CHECK (server.handle (R"({"jsonrpc":"2.0","method":"notifications/initialized"})").empty());

    const auto list = json::parse (server.handle (R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"));
    const auto& tools = list["result"]["tools"];
    REQUIRE (tools.size() >= 20);
    for (const auto& t : tools)
    {
        CHECK (t.contains ("description"));
        CHECK (t["inputSchema"]["type"] == "object");
        CHECK (t["inputSchema"]["properties"].is_object());
    }

    const auto unknown = json::parse (server.handle (R"({"jsonrpc":"2.0","id":3,"method":"resources/list"})"));
    CHECK (unknown["error"]["code"] == -32601);
    const auto bad = json::parse (server.handle ("not json"));
    CHECK (bad["error"]["code"] == -32700);

    // A wrong argument comes back as a tool error, not a protocol error.
    const auto err = callTool (server, "get_notes", { { "track", "Nothing" } });
    CHECK (err.contains ("error"));
}

TEST_CASE ("MCP: tracks, notes and clips", "[mcp]")
{
    TestHost host;
    mcp::Server server (host);

    auto added = callTool (server, "add_track", { { "name", "Bass" }, { "sound", "Fingered Bass" } });
    REQUIRE_FALSE (added.contains ("error"));
    CHECK (added["added"]["sound"]["program"] == 34);
    const auto bassId = added["added"]["id"].get<TrackId>();
    CHECK (host.doc.undoDescription() == "AI: Add Track");

    auto notes = callTool (server, "add_notes", { { "track", "bass" },
                                                  { "notes", json::array ({ { { "bar", 1 }, { "beat", 1 }, { "length", 1 }, { "pitch", "C2" } },
                                                                            { { "bar", 1 }, { "beat", 3 }, { "length", 1 }, { "pitch", 43 }, { "velocity", 110 } },
                                                                            { { "bar", 9 }, { "beat", 1 }, { "length", 4 }, { "pitch", "Bb1" } } }) } });
    CHECK (notes["added"] == 3);
    const auto& t = *edit::findTrack (host.song(), bassId);
    REQUIRE (t.clips.size() == 2); // far apart: two clips, not one long clip
    CHECK (t.clips[0]->notes[0].pitch == 36);
    CHECK (t.clips[1]->start == kBar * 8);

    auto read = callTool (server, "get_notes", { { "track", bassId }, { "from_bar", 1 }, { "to_bar", 1 } });
    REQUIRE (read["notes"].size() == 2);
    CHECK (read["notes"][1]["beat"] == 3.0);
    CHECK (read["notes"][1]["name"] == "G2");

    auto changed = callTool (server, "change_notes", { { "track", "Bass" }, { "transpose", 12 }, { "velocity", 80 } });
    CHECK (changed["changed"] == 3);
    CHECK (edit::findTrack (host.song(), bassId)->clips[0]->notes[0].pitch == 48);

    auto removed = callTool (server, "remove_notes", { { "track", "Bass" }, { "from_bar", 9 }, { "to_bar", 9 } });
    CHECK (removed["removed"] == 1);

    const auto firstClip = edit::findTrack (host.song(), bassId)->clips[0]->id;
    auto copied = callTool (server, "edit_clips", { { "action", "copy" }, { "clips", { firstClip } }, { "to_bar", 5 } });
    REQUIRE (copied["new_clips"].size() == 1);
    CHECK (edit::findClipPtr (host.song(), copied["new_clips"][0].get<ClipId>())->start == kBar * 4);

    auto song = callTool (server, "set_song", { { "tempo", 96 }, { "time_signature", "3/4" }, { "key", "D minor" } });
    CHECK (host.song().baseTempo() == 96.0);
    CHECK (host.song().timeSignature.numerator == 3);
    CHECK (callTool (server, "set_song", { { "time_signature", "7" } }).contains ("error"));

    auto info = callTool (server, "get_song", json::object());
    CHECK (info["tracks"][0]["name"] == "Bass");
    CHECK (info["key"] == "D minor");

    auto undone = callTool (server, "undo", { { "steps", 2 } });
    CHECK (undone["undone"] == 2);
    CHECK (host.song().baseTempo() == 120.0);

    callTool (server, "transport", { { "action", "go_to" }, { "bar", 3 } });
    CHECK (host.at == kBar * 2);
}

TEST_CASE ("MCP: score, beat and clip grid", "[mcp]")
{
    TestHost host;
    mcp::Server server (host);
    callTool (server, "add_track", { { "name", "Voice" }, { "sound", 53 } });
    callTool (server, "add_notes", { { "track", "Voice" },
                                     { "notes", json::array ({ { { "bar", 1 }, { "beat", 1 }, { "length", 1 }, { "pitch", "C4" } },
                                                               { { "bar", 1 }, { "beat", 2 }, { "length", 1 }, { "pitch", "C4" } } }) } });

    auto marks = callTool (server, "add_score_marks", { { "track", "Voice" },
                                                        { "marks", json::array ({ { { "bar", 1 }, { "type", "dynamic" }, { "text", "mf" } },
                                                                                  { { "bar", 1 }, { "type", "chord" }, { "text", "Cm7" } } }) } });
    CHECK (marks["marks"] == 2);
    CHECK (callTool (server, "add_score_marks", { { "track", "Voice" }, { "marks", json::array ({ { { "bar", 1 }, { "type", "dynamic" }, { "text", "loud" } } }) } })
               .contains ("error"));
    auto lyrics = callTool (server, "set_lyrics", { { "track", "Voice" }, { "bar", 1 }, { "text", "Twin-kle twin-kle" } });
    CHECK (lyrics["words_placed"] == 2);
    auto gone = callTool (server, "remove_score_marks", { { "track", "Voice" }, { "from_bar", 1 }, { "to_bar", 1 }, { "type", "lyric" } });
    CHECK (gone["removed"] == 2);

    callTool (server, "add_track", { { "name", "Drums" }, { "type", "drums" } });
    auto beat = callTool (server, "set_beat", { { "track", "Drums" }, { "bars", 1 },
                                                { "rows", json::array ({ { { "drum", "kick" }, { "steps", "x...x...x...x..." } },
                                                                         { { "drum", "snare" }, { "steps", "....X......." } },
                                                                         { { "drum", "hihat" }, { "steps", "xo" } } }) } });
    REQUIRE_FALSE (beat.contains ("error"));
    REQUIRE (beat["rows"].size() == 3);
    CHECK (beat["rows"][0]["drum"] == 36);
    CHECK (beat["rows"][0]["steps"] == "x...x...x...x...");
    CHECK (beat["rows"][2]["steps"] == "xoxoxoxoxoxoxoxo");
    const auto* drums = edit::findTrack (host.song(), callTool (server, "get_song", json::object())["tracks"][1]["id"].get<TrackId>());
    REQUIRE (drums->clips.size() == 1);
    CHECK (drums->clips[0]->loopLength == kBar);
    CHECK (drums->output.channel == 10);

    // Changing one row keeps the others.
    beat = callTool (server, "set_beat", { { "track", "Drums" }, { "rows", json::array ({ { { "drum", 36 }, { "steps", "x......." } } }) } });
    CHECK (beat["rows"][0]["steps"] == "x.......x.......");
    CHECK (beat["rows"].size() == 3);

    auto slot = callTool (server, "set_slot", { { "track", "Voice" }, { "scene", 2 }, { "name", "Hook" },
                                                { "notes", json::array ({ { { "bar", 2 }, { "beat", 1 }, { "length", 2 }, { "pitch", "E4" } } }) } });
    CHECK (slot["length_beats"] == 8.0);
    callTool (server, "name_scene", { { "scene", 2 }, { "name", "Chorus" } });
    auto grid = callTool (server, "get_clip_grid", json::object());
    CHECK (grid["scenes"][1]["name"] == "Chorus");
    CHECK (grid["tracks"][0]["slots"][0]["scene"] == 2);
    callTool (server, "launch", { { "scene", 2 } });
    CHECK (host.launchedScene == 1);
    callTool (server, "clear_slot", { { "track", "Voice" }, { "scene", 2 } });
    CHECK (callTool (server, "get_clip_grid", json::object())["tracks"][0]["slots"].empty());
}

TEST_CASE ("MCP tools: styles and hardware synths", "[mcp]")
{
    struct DeviceHost final : mcp::Host
    {
        Document doc;
        DeviceLibrary lib;
        std::vector<std::string> files;
        const Song& song() const override { return doc.song(); }
        std::uint64_t songRevision() const override { return doc.revision(); }
        void edit (const std::string& d, const std::function<void (Song&)>& fn) override { doc.edit (d, fn); }
        std::vector<std::string> midiOutputs() const override { return { "Roland D-05 MIDI 1", "IAC Driver Bus 1" }; }
        const DeviceLibrary* devices() const override { return &lib; }
        std::vector<std::string> styleFiles() const override { return files; }
    } host;

    const auto readProfile = [] (const char* name) {
        std::ifstream in (std::filesystem::path (ARRANGER_DEVICES_DIR) / name);
        std::stringstream text;
        text << in.rdbuf();
        return device_profile::parse (text.str());
    };
    host.lib.add (readProfile ("roland-d-05.yaml"));
    host.lib.add (readProfile ("novation-xiosynth.yaml"));

    // A small style file: drums and bass in Main A.
    Style style;
    style.name = "Test 16Beat";
    style.tempo = 100.0;
    StyleSection main;
    main.name = "Main A";
    main.length = kBar;
    Clip drums, bass;
    drums.length = bass.length = kBar;
    drums.notes = { { 0, 120, 36, 110 }, { kBeat, 120, 38, 100 }, { kBeat * 2, 120, 36, 110 }, { kBeat * 3, 120, 38, 100 } };
    bass.notes = { { 0, kBeat * 2, 36, 100 }, { kBeat * 2, kBeat * 2, 43, 90 } };
    main.parts[9] = drums;
    main.parts[11] = bass;
    style.sections = { main };
    const auto folder = std::filesystem::temp_directory_path() / "arranger-mcp-style-test";
    std::filesystem::create_directories (folder);
    const auto file = (folder / "Pop 16 Beat.sty").string();
    style_file::save (style, file);
    host.files = { file, (folder / "Ballad.sty").string(), (folder / "Std16Beat 1.S778.prs").string() };

    mcp::Server server (host);

    auto found = callTool (server, "find_styles", { { "name", "Beat16" } });
    REQUIRE (found["styles"].size() == 2);
    CHECK (found["styles"][0]["file"].get<std::string>().find ("16") != std::string::npos);
    CHECK (callTool (server, "find_styles", { { "name", "tango" } })["matches"] == 0);

    auto info = callTool (server, "get_style", { { "file", file } });
    CHECK (info["sections"][0]["name"] == "Main A");
    CHECK (info["sections"][0]["parts"].size() == 2);
    CHECK (callTool (server, "get_style", { { "file", "/etc/passwd" } }).contains ("error"));

    auto filled = callTool (server, "fill_from_style", { { "file", file }, { "chords", "D Bm G A" }, { "bars", 12 }, { "use_style_tempo", true } });
    REQUIRE (filled["added_tracks"].size() == 2);
    CHECK (host.song().tracks.size() == 2);
    CHECK (host.song().baseTempo() == 100.0);
    CHECK (callTool (server, "fill_from_style", { { "file", file }, { "section", "Intro Z" }, { "chords", "D" }, { "bars", 4 } })["error"].get<std::string>().find ("Main A") != std::string::npos);

    auto devices = callTool (server, "list_devices", json::object());
    REQUIRE (devices["outputs"].size() == 2);
    CHECK (devices["outputs"][0]["device"] == "Roland D-05");

    auto pads = callTool (server, "find_sounds", { { "device", "D-05" }, { "search", "warm pad" } });
    REQUIRE (! pads["sounds"].empty());
    CHECK (pads["sounds"][0]["name"] == "Fat Warm Pad");
    CHECK (callTool (server, "find_sounds", { { "device", "XioSynth" }, { "search", "analog bass" } }).contains ("note"));

    callTool (server, "add_track", { { "name", "Pad" } });
    auto changed = callTool (server, "set_track_output", { { "track", "Pad" }, { "output", "D-05" }, { "sound", "fat warm pad" }, { "channel", 3 } });
    const auto& pad = host.song().tracks.back();
    CHECK (pad.output.kind == TrackOutput::Kind::midiPort);
    CHECK (pad.output.port == "Roland D-05 MIDI 1");
    CHECK (pad.output.channel == 3);
    CHECK (pad.sound.name.find ("Fat Warm Pad") != std::string::npos);
    CHECK (pad.sound.program >= 0);
    CHECK (! changed.contains ("note"));

    // Without a channel, a monotimbral D-05 uses its receive channel; an explicit one wins.
    callTool (server, "set_track_output", { { "track", "Pad" }, { "output", "D-05" } });
    CHECK (host.song().tracks.back().output.channel == 1);
    callTool (server, "set_track_output", { { "track", "Pad" }, { "output", "D-05" }, { "channel", 5 } });
    CHECK (host.song().tracks.back().output.channel == 5);

    // A device that is not plugged in: its port name still works later.
    callTool (server, "set_track_output", { { "track", "Pad" }, { "output", "XioSynth" } });
    CHECK (host.song().tracks.back().output.port == "XioSynth");
    callTool (server, "set_track_output", { { "track", "Pad" }, { "output", "built-in" }, { "sound", "warm pad" } });
    CHECK (host.song().tracks.back().output.kind == TrackOutput::Kind::soundFont);

    std::filesystem::remove_all (folder);
}

TEST_CASE ("MCP tools: MIDI setup and device files", "[mcp]")
{
    struct SetupHost final : mcp::Host
    {
        Document doc;
        DeviceLibrary lib;
        std::vector<DeviceFile> files { { "roland-d-05.yaml", "", true } };
        std::vector<std::string> saved;
        const Song& song() const override { return doc.song(); }
        std::uint64_t songRevision() const override { return doc.revision(); }
        void edit (const std::string& d, const std::function<void (Song&)>& fn) override { doc.edit (d, fn); }
        std::vector<std::string> midiOutputs() const override { return { "Roland D-05" }; }
        std::vector<MidiInput> midiInputs() const override { return { { "Launchkey Mini MK3 MIDI Port", true }, { "Digital Keyboard-1", true } }; }
        const DeviceLibrary* devices() const override { return &lib; }
        std::vector<DeviceFile> deviceFiles() const override { return files; }
        void writeDeviceFile (const std::string& name, const std::string& text) override
        {
            saved.push_back (name);
            files.push_back ({ name, text, false });
        }
        std::vector<ReceivedMidi> recentMidi (double) const override
        {
            return { { 3.0, "Launchkey Mini MK3 MIDI Port", { 0xb0, 21, 64 } }, { 1.5, "Digital Keyboard-1", { 0x90, 60, 100 } }, { 1.0, "Digital Keyboard-1", { 0xc2, 4 } } };
        }
    } host;

    std::ifstream in (std::filesystem::path (ARRANGER_DEVICES_DIR) / "roland-d-05.yaml");
    std::stringstream d05;
    d05 << in.rdbuf();
    host.files[0].text = d05.str();
    host.lib.add (device_profile::parse (d05.str()));

    mcp::Server server (host);
    callTool (server, "add_track", { { "name", "Keys" } });

    auto setup = callTool (server, "get_midi_setup", json::object());
    CHECK (setup["inputs"].size() == 2);
    CHECK (setup["outputs"][0]["device"] == "Roland D-05");
    CHECK (setup["tracks"][0]["input"] == "all");

    callTool (server, "set_track_input", { { "track", "Keys" }, { "input", "Launchkey" } });
    CHECK (host.song().tracks[0].inputPort == "Launchkey Mini MK3 MIDI Port");
    callTool (server, "set_track_input", { { "track", "Keys" }, { "input", "all" } });
    CHECK (host.song().tracks[0].inputPort.empty());

    auto midi = callTool (server, "recent_midi", json::object());
    REQUIRE (midi["messages"].size() == 3);
    CHECK (midi["messages"][0]["type"] == "control_change");
    CHECK (midi["messages"][0]["controller"] == 21);
    CHECK (midi["messages"][1]["note"] == 60);
    CHECK (midi["messages"][2]["program"] == 5);
    CHECK (midi["messages"][2]["channel"] == 3);

    CHECK (callTool (server, "list_device_files", json::object())["files"][0]["kind"] == "device_profile");
    auto file = callTool (server, "read_device_file", { { "name", "D-05" } });
    CHECK (file["text"].get<std::string>().find ("Fat Warm Pad") != std::string::npos);

    // A mistake is caught before anything is saved.
    CHECK (callTool (server, "write_device_file", { { "name", "bad.yaml" }, { "text", "profile: 1\nid: [" } }).contains ("error"));
    CHECK (host.saved.empty());

    // A changed copy goes to the user's folder, under a safe name.
    auto text = file["text"].get<std::string>();
    text.replace (text.find ("Fat Warm Pad"), 12, "My Warm Pad");
    auto written = callTool (server, "write_device_file", { { "name", "../../roland-d-05" }, { "text", text } });
    CHECK (written["saved_as"] == "roland-d-05.yaml");
    REQUIRE (host.saved.size() == 1);
    CHECK (callTool (server, "read_device_file", { { "name", "roland-d-05" } })["built_in"] == false);
}

TEST_CASE ("MCP tools: synth sound editors and sound names", "[mcp][synth-editor]")
{
    struct SoundHost final : mcp::Host
    {
        Document doc;
        DeviceLibrary lib;
        std::vector<DeviceFile> files;
        std::map<std::string, int> sent;
        std::string readFrom;
        const Song& song() const override { return doc.song(); }
        std::uint64_t songRevision() const override { return doc.revision(); }
        void edit (const std::string& d, const std::function<void (Song&)>& fn) override { doc.edit (d, fn); }
        const DeviceLibrary* devices() const override { return &lib; }
        std::vector<DeviceFile> deviceFiles() const override { return files; }
        void writeDeviceFile (const std::string& name, const std::string& text) override
        {
            std::erase_if (files, [&] (const DeviceFile& f) { return f.name == name; });
            files.push_back ({ name, text, false });
            lib.addSounds (device_profile::parseAddOn (text));
        }
        bool sendSynthParameter (const std::string& id, const std::string& page, const std::string& param, int value) override
        {
            sent[id + ":" + page + "/" + param] = value;
            return true;
        }
        int synthParameter (const std::string& id, const std::string& page, const std::string& param) const override
        {
            const auto it = sent.find (id + ":" + page + "/" + param);
            return it == sent.end() ? -1 : it->second;
        }
        std::string readSoundNames (const std::string& id) override
        {
            readFrom = id;
            return {};
        }
    } host;

    for (const char* name : { "roland-d-05.yaml", "novation-xiosynth.yaml" })
    {
        std::ifstream in (std::filesystem::path (ARRANGER_DEVICES_DIR) / name);
        std::stringstream text;
        text << in.rdbuf();
        host.lib.add (device_profile::parse (text.str()));
    }
    mcp::Server server (host);

    auto pages = callTool (server, "get_sound_editor", { { "device", "XioSynth" } });
    CHECK (pages["pages"].size() == 10);

    // Values come in the scale the user sees: -64..63 settings take -10, choices take names.
    auto result = callTool (server, "set_sound", { { "device", "XioSynth" },
                                                   { "settings", { { { "setting", "Filter/Frequency" }, { "value", 90 } },
                                                                   { { "setting", "Filter Type" }, { "value", "high pass" } },
                                                                   { { "setting", "Filter/Mod Env Depth" }, { "value", -10 } },
                                                                   { { "setting", "Wobble" }, { "value", 3 } } } } });
    CHECK (result["set"].size() == 3);
    CHECK (result["problems"].size() == 1);
    CHECK (host.sent["novation-xiosynth:Filter/Frequency"] == 90);
    CHECK (host.sent["novation-xiosynth:Filter/Type"] == 2);
    CHECK (host.sent["novation-xiosynth:Filter/Mod Env Depth"] == 54);

    auto filter = callTool (server, "get_sound_editor", { { "device", "XioSynth" }, { "page", "filter" } });
    CHECK (filter["settings"][2]["name"] == "Frequency");
    CHECK (filter["settings"][2]["value"] == 90);
    CHECK (filter["settings"][0]["value"] == "High pass");

    callTool (server, "set_sound", { { "device", "D-05" }, { "settings", { { { "setting", "Upper Partial 1/Pitch Coarse" }, { "value", "C4" } } } } });
    CHECK (host.sent["roland-d-05:Upper Partial 1/Pitch Coarse"] == 36);

    CHECK (callTool (server, "read_sound_names", { { "device", "D-05" } })["started"] == true);
    CHECK (host.readFrom == "roland-d-05");
    CHECK (callTool (server, "read_sound_names", { { "device", "XioSynth" } }).contains ("error"));

    // Naming a numbered XioSynth sound keeps its bank.
    auto named = callTool (server, "name_sounds", { { "device", "XioSynth" }, { "group", "Bank A" },
                                                    { "sounds", { { { "name", "Analog Bass" }, { "program", 5 } } } } });
    CHECK (named["file"] == "novation-xiosynth.sounds.yaml");
    auto found = callTool (server, "find_sounds", { { "device", "XioSynth" }, { "search", "analog bass" } });
    REQUIRE (found["sounds"].size() >= 1);
    CHECK (found["sounds"][0]["name"] == "Analog Bass");
    CHECK (found["sounds"][0]["program"] == 5);
    CHECK (found["sounds"][0]["bank_lsb"] == 0);
    CHECK (callTool (server, "list_device_files", json::object())["files"][0]["kind"] == "sound_list");
}

TEST_CASE ("MCP tools: set up MIDI ports", "[mcp]")
{
    // "Use the Launchkey as my keyboard, record from the PSR on channel 1, put the X-Touch knobs on the mixer."
    struct PortHost final : mcp::Host
    {
        Document doc;
        DeviceLibrary lib;
        std::vector<ControllerProfile> pages;
        std::map<std::string, bool> enabled { { "Launchkey Mini MK3 MIDI Port", true }, { "Digital Keyboard-1", true }, { "USB MIDI Interface", false } };
        std::map<std::string, std::string> chosenPages;
        const Song& song() const override { return doc.song(); }
        std::uint64_t songRevision() const override { return doc.revision(); }
        void edit (const std::string& d, const std::function<void (Song&)>& fn) override { doc.edit (d, fn); }
        std::vector<std::string> midiOutputs() const override { return { "Digital Keyboard-1", "USB MIDI Interface" }; }
        std::vector<MidiInput> midiInputs() const override
        {
            std::vector<MidiInput> list;
            for (const auto& [name, on] : enabled)
                list.push_back ({ name, on });
            return list;
        }
        bool setMidiInputEnabled (const std::string& name, bool on) override
        {
            if (enabled.count (name) == 0)
                return false;
            enabled[name] = on;
            return true;
        }
        const DeviceLibrary* devices() const override { return &lib; }
        std::vector<ControllerProfile> controllers() const override { return pages; }
        std::string controllerPageFor (const std::string& input) const override
        {
            if (const auto it = chosenPages.find (input); it != chosenPages.end())
                return it->second == "none" ? std::string() : it->second;
            return mcp::Host::controllerPageFor (input);
        }
        void assignDeviceProfile (const std::string& output, const std::string& id) override { lib.assign (output, id); }
        void assignControllerPage (const std::string& input, const std::string& id) override
        {
            if (id.empty())
                chosenPages.erase (input);
            else
                chosenPages[input] = id;
        }
    } host;

    const auto read = [] (const std::filesystem::path& p) {
        std::ifstream in (p);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    };
    host.lib.add (device_profile::parse (read (std::filesystem::path (ARRANGER_DEVICES_DIR) / "roland-d-05.yaml")));
    host.lib.add (device_profile::parse (read (std::filesystem::path (ARRANGER_DEVICES_DIR) / "yamaha-psr-s970.yaml")));
    host.pages.push_back (controller_profile::parse (read (std::filesystem::path (ARRANGER_CONTROLLERS_DIR) / "behringer-x-touch-mini.yaml")));
    host.pages.push_back (controller_profile::parse (read (std::filesystem::path (ARRANGER_CONTROLLERS_DIR) / "novation-launchkey-mini-mk3.yaml")));

    mcp::Server server (host);
    callTool (server, "add_track", { { "name", "Piano" } });

    // The X-Touch hangs on a USB MIDI cable here, so its name doesn't say what it is.
    CHECK (callTool (server, "set_midi_input", { { "input", "usb midi" }, { "on", true } })["enabled"] == true);
    CHECK (host.enabled["USB MIDI Interface"]);
    auto page = callTool (server, "set_port_device", { { "port", "USB MIDI" }, { "kind", "input" }, { "device", "X-Touch" } });
    CHECK (page["controller_page"] == "Behringer X-Touch Mini");
    CHECK (host.chosenPages["USB MIDI Interface"] == "behringer-x-touch-mini");

    // The Launchkey is a plain keyboard, not a mixer controller.
    CHECK (callTool (server, "set_port_device", { { "port", "Launchkey" }, { "kind", "input" }, { "device", "none" } })["controller_page"] == "none");
    auto setup = callTool (server, "get_midi_setup", json::object());
    for (const auto& in : setup["inputs"])
        if (in["input"] == "Launchkey Mini MK3 MIDI Port")
            CHECK (in["controller_page"].get<std::string>().rfind ("none", 0) == 0);

    // The PSR's port is "Digital Keyboard": say what it is.
    CHECK (host.lib.forPort ("Digital Keyboard-1").id != "yamaha-psr-s970");
    auto out = callTool (server, "set_port_device", { { "port", "Digital Keyboard" }, { "kind", "output" }, { "device", "PSR-S970" } });
    CHECK (out["device"] == host.lib.byId ("yamaha-psr-s970")->name);
    CHECK (host.lib.forPort ("Digital Keyboard-1").id == "yamaha-psr-s970");
    callTool (server, "set_port_device", { { "port", "Digital Keyboard" }, { "kind", "output" }, { "device", "auto" } });
    CHECK (host.lib.forPort ("Digital Keyboard-1").id != "yamaha-psr-s970");

    // Record from the PSR on channel 1 only.
    auto input = callTool (server, "set_track_input", { { "track", "Piano" }, { "input", "Digital Keyboard" }, { "channel", 1 } });
    CHECK (input["track"]["input_channel"] == 1);
    CHECK (host.song().tracks[0].inputPort == "Digital Keyboard-1");
    CHECK (host.song().tracks[0].inputChannel == 1);
    // Changing the input alone keeps the channel; 0 is all channels again.
    callTool (server, "set_track_input", { { "track", "Piano" }, { "input", "Launchkey" } });
    CHECK (host.song().tracks[0].inputChannel == 1);
    callTool (server, "set_track_input", { { "track", "Piano" }, { "input", "all" }, { "channel", 0 } });
    CHECK (host.song().tracks[0].inputChannel == 0);
    CHECK (callTool (server, "set_track_input", { { "track", "Piano" }, { "input", "all" }, { "channel", 17 } }).contains ("error"));

    CHECK (callTool (server, "set_port_device", { { "port", "Nothing" }, { "kind", "input" }, { "device", "X-Touch" } }).contains ("error"));
    CHECK (callTool (server, "set_port_device", { { "port", "USB" }, { "kind", "input" }, { "device", "No Such Page" } }).contains ("error"));
}

TEST_CASE ("MCP: lyrics are read and corrected word by word", "[mcp][analysis]")
{
    TestHost host;
    mcp::Server server (host);
    TrackId id = 0;
    host.edit ("setup", [&] (Song& s) {
        id = edit::addTrack (s, "Vocals MIDI").id;
        Clip c;
        c.length = kBar * 4;
        c.notes = { { 0, kBeat, 60, 90 }, { kBeat, kBeat, 62, 90 }, { kBar * 2, kBeat, 64, 90 } };
        edit::addClip (s, id, c);
        edit::setMark (s, id, { ScoreMark::Kind::lyric, 0, "Yester" });
        edit::setMark (s, id, { ScoreMark::Kind::lyric, kBeat, "day" });
        edit::setMark (s, id, { ScoreMark::Kind::lyric, kBar * 2, "troubles" });
    });

    const auto got = callTool (server, "get_lyrics", { { "track", "Vocals" } });
    REQUIRE (got.at ("words").size() == 3);
    CHECK (got.at ("words")[2].at ("word") == 3);
    CHECK (got.at ("lines") == json::array ({ "Yester day", "troubles" }));

    const auto done = callTool (server, "edit_lyrics", { { "track", id },
                                                        { "changes", json::array ({ { { "word", 1 }, { "text", "Yes-" } }, { { "word", 2 }, { "text", "ter-" } } }) },
                                                        { "add", json::array ({ { { "bar", 3 }, { "beat", 2 }, { "text", "seemed" } } }) } });
    CHECK (done.at ("changed") == 2);
    CHECK (done.at ("added") == 1);
    const auto again = callTool (server, "get_lyrics", { { "track", id } });
    CHECK (again.at ("lines") == json::array ({ "Yes-ter-", "troubles seemed" }));

    CHECK (callTool (server, "edit_lyrics", { { "track", id }, { "changes", json::array ({ { { "word", 9 }, { "text", "x" } } }) } }).contains ("error"));
}

TEST_CASE ("MCP: audio analysis is started and followed", "[mcp][analysis]")
{
    struct AnalysisHost final : mcp::Host
    {
        Document doc;
        AnalysisRequest asked;
        AnalysisStatus now;
        const Song& song() const override { return doc.song(); }
        std::uint64_t songRevision() const override { return doc.revision(); }
        void edit (const std::string& d, const std::function<void (Song&)>& fn) override { doc.edit (d, fn); }
        std::string startAnalysis (const AnalysisRequest& r) override
        {
            asked = r;
            now.state = "running";
            now.progress = 0.25;
            now.step = "Splitting into stems";
            return {};
        }
        AnalysisStatus analysisStatus() const override { return now; }
    } host;
    mcp::Server server (host);

    CHECK (callTool (server, "analyse_audio", json::object()).at ("error").get<std::string>().find ("no audio clip") != std::string::npos);

    ClipId clip = 0;
    TrackId vocals = 0;
    host.edit ("setup", [&] (Song& s) {
        const auto t = edit::addAudioTrack (s, "Song").id;
        AudioClip a;
        a.file = "/music/song.wav";
        clip = edit::addAudioClip (s, t, a);
        vocals = edit::addTrack (s, "Vocals MIDI").id;
    });
    const auto started = callTool (server, "analyse_audio", { { "lyrics", true }, { "stems", false }, { "language", "da" } });
    CHECK (started.at ("started") == true);
    CHECK (host.asked.clip == clip);
    CHECK_FALSE (host.asked.stems);
    CHECK (host.asked.language == "da");

    auto st = callTool (server, "get_analysis", json::object());
    CHECK (st.at ("state") == "running");
    CHECK (st.at ("step") == "Splitting into stems");

    host.now.state = "done";
    host.now.applied.midiTracks = { vocals };
    host.now.applied.lyricsTrack = vocals;
    host.now.applied.words = 12;
    host.now.lyricLines = { "hello world" };
    st = callTool (server, "get_analysis", json::object());
    CHECK (st.at ("midi_tracks")[0].at ("name") == "Vocals MIDI");
    CHECK (st.at ("lyrics").at ("words") == 12);
    CHECK (st.at ("lyrics").at ("lines")[0] == "hello world");
}

TEST_CASE ("MCP tools: music theory", "[mcp][theory]")
{
    TestHost host;
    mcp::Server server (host);
    callTool (server, "add_track", { { "name", "Piano" } });
    // I IV V7 I in Eb major, one chord a bar, then D7 (V7/V).
    const auto chord = [] (int bar, std::initializer_list<const char*> pitches) {
        json out = json::array();
        for (const auto* p : pitches)
            out.push_back ({ { "bar", bar }, { "beat", 1 }, { "length", 4 }, { "pitch", p } });
        return out;
    };
    json notes = json::array();
    for (const auto& part : { chord (1, { "Eb3", "G3", "Bb3", "Eb4" }), chord (2, { "Ab3", "C4", "Eb4", "Ab4" }),
                              chord (3, { "Bb2", "D3", "F3", "Ab3" }), chord (4, { "Eb3", "G3", "Bb3", "Eb4" }) })
        for (const auto& n : part)
            notes.push_back (n);
    callTool (server, "add_notes", { { "track", "Piano" }, { "notes", notes } });

    const auto key = callTool (server, "analyze_key", { { "set_song_key", true } });
    CHECK (key.at ("key") == "Eb major");
    CHECK (key.at ("alternatives").size() == 3);
    CHECK (host.song().key == "Eb major");

    const auto harmony = callTool (server, "analyze_harmony", { { "step", "bar" } });
    REQUIRE (harmony.at ("chords").size() == 4);
    CHECK (harmony.at ("chords")[0].at ("roman") == "I");
    CHECK (harmony.at ("chords")[1].at ("chord") == "Ab");
    CHECK (harmony.at ("chords")[1].at ("roman") == "IV");
    CHECK (harmony.at ("chords")[2].at ("roman") == "V7");

    const auto scale = callTool (server, "get_scale", { { "key", "D minor" }, { "scale", "harmonic minor" } });
    CHECK (scale.at ("notes") == json ({ "D", "E", "F", "G", "A", "Bb", "C#" }));
    CHECK (scale.at ("midi")[0] == 62);
    CHECK (scale.at ("chords").size() == 14);

    const auto prog = callTool (server, "chords_from_roman", { { "progression", "ii7 V7/V V7 Imaj7" } });
    REQUIRE (prog.at ("chords").size() == 4);
    CHECK (prog.at ("chords")[0].at ("chord") == "Fm7");
    CHECK (prog.at ("chords")[1].at ("chord") == "F7");
    CHECK (prog.at ("chords")[3].at ("chord") == "EbM7");
    CHECK (callTool (server, "chords_from_roman", { { "progression", "I Q" } }).contains ("error"));

    // Roman numerals are taken by set_chords too.
    CHECK_FALSE (callTool (server, "set_chords", { { "track", "Piano" }, { "chords", { { { "bar", 5 }, { "chord", "bVII" } } } } }).contains ("error"));
    bool found = false;
    for (const auto& m : host.song().tracks.front().marks)
        found = found || (m.kind == ScoreMark::Kind::chord && m.text == "Db");
    CHECK (found);

    const auto skill = callTool (server, "find_skills", { { "tag", "progressions" } });
    CHECK (skill.at ("count") == 1);
    CHECK (skill.at ("warnings").empty());
    REQUIRE (skill.at ("skills").size() == 1);
    CHECK (skill.at ("skills")[0].at ("id") == "common-progressions");
    CHECK (skill.at ("skills")[0].at ("built_in") == true);

    const auto full = callTool (server, "get_skill", { { "id", "common-progressions" } });
    CHECK (full.at ("id") == "common-progressions");
    CHECK_FALSE (full.at ("instructions").get<std::string>().empty());
    REQUIRE (full.at ("data").at ("progressions").size() == 25);
    CHECK (full.at ("data").at ("progressions")[0].at ("roman") == "I V vi IV");
    CHECK (callTool (server, "get_skill", { { "id", "no-such-skill" } }).contains ("error"));

    const auto voices = callTool (server, "check_voice_leading", json::object());
    CHECK (voices.at ("voices").size() == 4);
    // The chords above move all voices up together: parallel octaves (Eb3-Eb4 to Ab3-Ab4).
    bool octaves = false;
    for (const auto& i : voices.at ("issues"))
        octaves = octaves || i.at ("kind") == "parallel octaves";
    CHECK (octaves);
}

TEST_CASE ("MCP tools: smart chord input", "[mcp][chordinput]")
{
    TestHost host;
    mcp::Server server (host);
    host.doc.edit ("setup", [] (Song& s) {
        s.key = "C major";
        Track piano;
        piano.id = 1;
        piano.name = "Piano";
        Clip c;
        c.id = 10;
        c.length = kBar * 4;
        for (int bar = 0; bar < 4; ++bar)
            for (const int p : { 60, 64, 67 })
                c.notes.push_back ({ bar * kBar, kBar, static_cast<std::uint8_t> (p), 90 });
        piano.clips.push_back (std::make_shared<const Clip> (std::move (c)));
        s.tracks.push_back (std::move (piano));
        s.nextId = 20;
    });

    auto r = callTool (server, "type_chords", { { "text", "I vi IV V" } });
    REQUIRE (! r.contains ("error"));
    CHECK (r["bars"] == 4);
    CHECK (r["chords"][1]["chord"] == "Am");
    CHECK (host.song().tracks[0].marks.size() == 4);

    r = callTool (server, "suggest_next_chords", { { "after", "Dm G" } });
    CHECK (r["suggestions"][0]["chord"] == "C");

    r = callTool (server, "chord_palette", json::object());
    CHECK (r["pads"].size() == 16);

    r = callTool (server, "reharmonize", { { "bar", 4 } });
    REQUIRE (! r.contains ("error"));
    REQUIRE (r["options"].size() >= 2);
    CHECK (r["options"][0]["label"].get<std::string>().find ("Tritone") != std::string::npos);

    r = callTool (server, "apply_reharmonization", { { "bar", 4 }, { "option", 1 } });
    REQUIRE (! r.contains ("error"));
    CHECK (host.song().tracks[0].marks.back().text == "Db7");
}

TEST_CASE ("MCP: kids mode", "[mcp]")
{
    TestHost host;
    mcp::Server server (host);
    auto state = callTool (server, "kids_mode", json::object());
    CHECK (state["on"] == false);
    state = callTool (server, "kids_mode", { { "on", true }, { "pause_after_seconds", 9 } });
    CHECK (state["on"] == true);
    CHECK (state["pause_after_seconds"].get<double>() == 5.0);
    CHECK (host.kids.on);
    CHECK (host.kids.pauseAfterSeconds == 5.0);
    state = callTool (server, "kids_mode", { { "keys_sound", true } });
    CHECK (state["on"] == true);
    CHECK (host.kids.keysSound);
}

TEST_CASE ("MCP: grooves from a part and from words, applied and compared", "[mcp]")
{
    TestHost host;
    mcp::Server server (host);
    // A swung, accented hi-hat part, and a stiff one.
    host.doc.edit ("setup", [] (Song& s) {
        auto& feel = edit::addTrack (s, "Feel");
        feel.output = { TrackOutput::Kind::soundFont, "", 10 };
        auto& stiff = edit::addTrack (s, "Stiff");
        stiff.output = { TrackOutput::Kind::soundFont, "", 10 };
        Clip a, b;
        a.length = b.length = 4 * kBar;
        for (int i = 0; i < 32; ++i)
        {
            const Tick straight = i * kBeat / 2;
            a.notes.push_back ({ straight + (i % 2 == 1 ? 150 : 0), 60, 42, static_cast<std::uint8_t> (i % 2 == 0 ? 100 : 60) });
            b.notes.push_back ({ straight, 60, 42, 90 });
        }
        edit::addClip (s, s.tracks[0].id, a);
        edit::addClip (s, s.tracks[1].id, b);
    });

    const auto made = callTool (server, "extract_groove", { { "name", "My swing" }, { "track", "Feel" } });
    REQUIRE_FALSE (made.contains ("error"));
    CHECK (made["swing"].get<double>() > 63.0);
    CHECK (made["feel"].get<std::string>().find ("wing") != std::string::npos);
    const auto list = callTool (server, "list_grooves", json::object());
    CHECK (list["grooves"].size() >= 9);

    const auto before = callTool (server, "compare_groove", { { "groove", "My swing" }, { "track", "Stiff" } });
    const auto applied = callTool (server, "apply_groove", { { "groove", "My swing" }, { "tracks", { "Stiff" } }, { "velocity", 100 } });
    REQUIRE_FALSE (applied.contains ("error"));
    CHECK (applied["notes_changed"].get<int>() > 20);
    const auto after = callTool (server, "compare_groove", { { "groove", "My swing" }, { "track", "Stiff" } });
    CHECK (after["match"]["score"].get<double>() > before["match"]["score"].get<double>() + 20.0);
    CHECK (after["match"]["timing_error_ms"].get<double>() < 2.0);
    CHECK (host.doc.undo());

    const auto words = callTool (server, "make_groove", { { "name", "Lazy" }, { "words", "lazy swing, behind the beat" } });
    CHECK (words["behind_ms"].get<double>() > 8.0);
    const auto numbers = callTool (server, "make_groove", { { "name", "Pushed" }, { "swing", 55 }, { "behind_ms", -10 } });
    CHECK (numbers["swing"].get<double>() == Catch::Approx (55.0).margin (0.5));
    CHECK (callTool (server, "apply_groove", { { "groove", "nothing like this" } }).contains ("error"));
    CHECK (callTool (server, "play_groove", { { "groove", "Lazy" }, { "listen", "after" } }).contains ("error")); // this host can't play
}

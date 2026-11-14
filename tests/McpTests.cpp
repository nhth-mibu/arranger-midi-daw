#include "arranger/core/Document.h"
#include "arranger/core/McpServer.h"
#include "arranger/core/SongEdit.h"
#include "arranger/core/Style.h"

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
        void edit (const std::string& d, const std::function<void (Song&)>& fn) override { doc.edit (d, fn); }
        bool undo() override { return doc.undo(); }
        bool redo() override { return doc.redo(); }
        void play() override { playing = true; }
        void stop() override { playing = false; }
        void locate (Tick t) override { at = t; }
        bool isPlaying() const override { return playing; }
        double position() const override { return static_cast<double> (at); }
        void launchScene (int scene) override { launchedScene = scene; }
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

// The MCP tools that had no test of their own: set_track, remove_track,
// find_chords, get_beat and the four Style from Audio tools.

#include "KitchenSink.h"

#include "arranger/core/Document.h"
#include "arranger/core/McpServer.h"
#include "arranger/core/SongEdit.h"
#include "arranger/core/StyleFromAudio.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace arranger;
using nlohmann::json;

namespace
{
    constexpr Tick kBeat = kTicksPerQuarter;
    constexpr Tick kBar = kBeat * 4;

    struct Host final : mcp::Host
    {
        Document doc;
        const Song& song() const override { return doc.song(); }
        std::uint64_t songRevision() const override { return doc.revision(); }
        void edit (const std::string& d, const std::function<void (Song&)>& fn) override { doc.edit (d, fn); }
        bool undo() override { return doc.undo(); }

        // Style from Audio, as the app's job runs it, without the job.
        std::optional<StyleFromAudioRequest> started;
        StyleFromAudioStatus status;
        std::string savedPath, playing;
        int playFrom = -1, playTo = -1;
        std::string startStyleFromAudio (const StyleFromAudioRequest& r) override
        {
            if (status.state == "analysing")
                return "a style is being made already";
            started = r;
            return {};
        }
        StyleFromAudioStatus styleFromAudioStatus() const override { return status; }
        std::string saveStyleFromAudio (const std::string& path) override
        {
            if (status.made == nullptr)
                throw std::runtime_error ("there is no style to save");
            savedPath = path.empty() ? "/Styles/Made.sty" : path;
            return savedPath;
        }
        void playStyleFromAudio (const std::string& listen, int from, int to) override
        {
            playing = listen;
            playFrom = from;
            playTo = to;
        }
    };

    json call (mcp::Server& server, const std::string& name, json args)
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

    Song twoTracks()
    {
        Song s;
        s.key = "C major";
        auto& piano = edit::addTrack (s, "Piano");
        piano.output = { TrackOutput::Kind::soundFont, "", 1 };
        Clip c;
        c.length = kBar * 4;
        // C, F, G, C: one chord a bar, whole notes.
        const std::vector<std::vector<int>> chords { { 48, 52, 55 }, { 53, 57, 60 }, { 55, 59, 62 }, { 48, 52, 55 } };
        for (std::size_t b = 0; b < chords.size(); ++b)
            for (const int p : chords[b])
                c.notes.push_back ({ static_cast<Tick> (b) * kBar, kBar, static_cast<std::uint8_t> (p), 90 });
        edit::addClip (s, piano.id, c);

        auto& drums = edit::addTrack (s, "Drums");
        drums.output = { TrackOutput::Kind::soundFont, "", 10 };
        Clip d;
        d.length = kBar;
        d.notes = { { 0, 60, 36, 127 }, { kBeat, 60, 38, 100 }, { kBeat * 2, 60, 36, 70 }, { kBeat * 3, 60, 38, 100 } };
        for (int i = 0; i < 8; ++i)
            d.notes.push_back ({ i * kBeat / 2, 30, 42, 90 });
        edit::normalise (d);
        edit::addClip (s, drums.id, d);
        return s;
    }
}

TEST_CASE ("MCP: set_track changes the name, sound, channel and mixer, in one undo step", "[mcp]")
{
    Host host;
    host.doc.reset (twoTracks());
    mcp::Server server (host);

    auto r = call (server, "set_track", { { "track", "Piano" }, { "name", "Keys" }, { "sound", "Electric Piano 1" }, { "channel", 3 },
                                          { "volume", 90 }, { "pan", 20 }, { "reverb", 10 }, { "chorus", 0 }, { "mute", true }, { "solo", false } });
    REQUIRE_FALSE (r.contains ("error"));
    const auto& t = host.song().tracks[0];
    CHECK (t.name == "Keys");
    CHECK (t.sound.program == 4);
    CHECK (t.output.channel == 3);
    CHECK (t.mixer.volume == 90);
    CHECK (t.mixer.pan == 20);
    CHECK (t.mixer.reverbSend == 10);
    CHECK (t.mixer.mute);
    CHECK (r["track"]["name"] == "Keys");

    // A sound by number (1-128), and the track by its new name.
    r = call (server, "set_track", { { "track", "keys" }, { "sound", 1 } });
    REQUIRE_FALSE (r.contains ("error"));
    CHECK (host.song().tracks[0].sound.program == 0);

    // Wrong values change nothing.
    const auto before = host.doc.revision();
    CHECK (call (server, "set_track", { { "track", "Keys" }, { "volume", 200 } }).contains ("error"));
    CHECK (call (server, "set_track", { { "track", "Keys" }, { "sound", "No Such Sound" } }).contains ("error"));
    CHECK (call (server, "set_track", { { "track", "Nobody" }, { "name", "x" } }).contains ("error"));
    CHECK (host.doc.revision() == before);

    // The first change undoes in one step.
    host.undo();
    host.undo();
    CHECK (host.song().tracks[0].name == "Piano");
}

TEST_CASE ("MCP: remove_track deletes the track and its clips", "[mcp]")
{
    Host host;
    host.doc.reset (twoTracks());
    mcp::Server server (host);

    auto r = call (server, "remove_track", { { "track", "Drums" } });
    CHECK (r["removed"] == "Drums");
    REQUIRE (host.song().tracks.size() == 1);
    CHECK (host.song().tracks[0].name == "Piano");

    // By id too; then there is nothing left to remove.
    r = call (server, "remove_track", { { "track", host.song().tracks[0].id } });
    CHECK (r["removed"] == "Piano");
    CHECK (host.song().tracks.empty());
    CHECK (call (server, "remove_track", { { "track", "Piano" } }).contains ("error"));
    CHECK (call (server, "remove_track", json::object()).contains ("error"));

    host.undo();
    CHECK (host.song().tracks.size() == 1);
}

TEST_CASE ("MCP: find_chords names the chords and writes them on a track", "[mcp]")
{
    Host host;
    host.doc.reset (twoTracks());
    mcp::Server server (host);

    auto r = call (server, "find_chords", { { "step", "bar" } });
    REQUIRE_FALSE (r.contains ("error"));
    CHECK (r["key"] == "C major");
    REQUIRE (r["chords"].size() == 4);
    CHECK (r["chords"][0]["chord"] == "C");
    CHECK (r["chords"][1]["chord"] == "F");
    CHECK (r["chords"][2]["chord"] == "G");
    CHECK (r["chords"][1]["roman"] == "IV");
    CHECK (r["chords"][2]["roman"] == "V");

    // The symbols are on the first track; the drums are not read as notes.
    int written = 0;
    for (const auto& m : host.song().tracks[0].marks)
        written += m.kind == ScoreMark::Kind::chord ? 1 : 0;
    CHECK (written == 4);

    // A range and another track; running it again replaces, not adds.
    r = call (server, "find_chords", { { "step", "bar" }, { "from_bar", 2 }, { "to_bar", 3 }, { "track", "Drums" } });
    CHECK (r["chords"].size() == 2);
    call (server, "find_chords", { { "step", "bar" }, { "from_bar", 2 }, { "to_bar", 3 }, { "track", "Drums" } });
    int onDrums = 0;
    for (const auto& m : host.song().tracks[1].marks)
        onDrums += m.kind == ScoreMark::Kind::chord ? 1 : 0;
    CHECK (onDrums == 2);

    CHECK (call (server, "find_chords", { { "step", "fortnight" } }).contains ("error"));
    CHECK (call (server, "find_chords", { { "from_bar", 3 }, { "to_bar", 2 } }).contains ("error"));
}

TEST_CASE ("MCP: get_beat shows a drum pattern as a step grid", "[mcp]")
{
    Host host;
    host.doc.reset (twoTracks());
    mcp::Server server (host);

    auto r = call (server, "get_beat", { { "track", "Drums" } });
    REQUIRE_FALSE (r.contains ("error"));
    CHECK (r["bars"] == 1);
    CHECK (r["steps_per_beat"] == 4);
    std::map<int, std::string> rows;
    for (const auto& row : r["rows"])
        rows[row["drum"].get<int>()] = row["steps"].get<std::string>();
    CHECK (rows[36] == "X.......o.......");
    CHECK (rows[38] == "....x.......x...");
    CHECK (rows[42] == "x.x.x.x.x.x.x.x.");

    r = call (server, "get_beat", { { "track", "Drums" }, { "steps_per_beat", 2 } });
    for (const auto& row : r["rows"])
        if (row["drum"] == 42)
            CHECK (row["steps"] == "xxxxxxxx");

    // A track without a pattern says so; the step count is kept in range.
    host.doc.reset ([] {
        auto s = twoTracks();
        s.tracks[1].clips.clear();
        return s;
    }());
    r = call (server, "get_beat", { { "track", "Drums" }, { "steps_per_beat", 1000 } });
    CHECK (r["pattern"].is_null());
    CHECK (call (server, "get_beat", { { "track", "Nobody" } }).contains ("error"));
}

TEST_CASE ("MCP: Style from Audio starts, reports, plays and saves", "[mcp][stylefromaudio]")
{
    Host host;
    Song s = twoTracks();
    auto& audio = edit::addAudioTrack (s, "Recording");
    AudioClip a;
    a.file = "song.wav";
    a.length = kBar * 8;
    edit::addAudioClip (s, audio.id, a);
    host.doc.reset (s);
    const auto clipId = host.song().tracks[2].audio[0]->id;
    mcp::Server server (host);

    // Before anything: idle; nothing to save or play by section.
    CHECK (call (server, "get_style_from_audio", json::object())["state"] == "idle");
    CHECK (call (server, "save_style_from_audio", json::object()).contains ("error"));
    CHECK (call (server, "play_style_from_audio", { { "listen", "style" }, { "section", "Main A" } }).contains ("error"));

    // Start from the first audio clip, with options.
    auto r = call (server, "style_from_audio", { { "bpm", 100 }, { "main_bars", 2 }, { "vocals", true }, { "name", "My Style" } });
    REQUIRE_FALSE (r.contains ("error"));
    CHECK (r["started"] == true);
    REQUIRE (host.started.has_value());
    CHECK (host.started->clip == clipId);
    CHECK (host.started->bpm == 100.0);
    CHECK (host.started->mainBars == 2);
    CHECK (host.started->vocals);
    CHECK (host.started->name == "My Style");

    // From a link; a clip that is not there; a song without audio.
    r = call (server, "style_from_audio", { { "link", "https://example.com/v" } });
    CHECK (host.started->link == "https://example.com/v");
    CHECK (call (server, "style_from_audio", { { "clip", 999999 } }).contains ("error"));
    {
        Host empty;
        empty.doc.reset (twoTracks());
        mcp::Server other (empty);
        CHECK (call (other, "style_from_audio", json::object()).contains ("error"));
    }

    // While it works: the state, the step and the progress.
    host.status.state = "analysing";
    host.status.clip = clipId;
    host.status.progress = 0.4567;
    host.status.step = "Separating the stems";
    r = call (server, "get_style_from_audio", json::object());
    CHECK (r["state"] == "analysing");
    CHECK (r["progress"] == 0.457);
    CHECK (r["step"] == "Separating the stems");
    CHECK (call (server, "style_from_audio", json::object()).contains ("error")); // busy

    // Failed: the reason.
    host.status.state = "failed";
    host.status.error = "the clip has no drums";
    CHECK (call (server, "get_style_from_audio", json::object())["error"] == "the clip has no drums");

    // Done: the made style, from the MIDI of the kitchen-sink song as if it were analysed.
    style_from_audio::Options o;
    o.maxRounds = 0;
    o.name = "Made";
    host.status = {};
    host.status.state = "done";
    host.status.made = std::make_shared<const style_from_audio::Made> (style_from_audio::make (style_from_audio::resultFromSong (twoTracks()), o));
    r = call (server, "get_style_from_audio", json::object());
    CHECK (r["state"] == "done");
    CHECK (r["style"] == "Made");
    CHECK (r["parts"].is_array());
    REQUIRE (r["sections"].is_array());
    REQUIRE_FALSE (r["sections"].empty());

    // Play: a section by name, bars, both, and stop.
    const auto section = r["sections"][0]["name"].get<std::string>();
    r = call (server, "play_style_from_audio", { { "listen", "both" }, { "section", section } });
    REQUIRE_FALSE (r.contains ("error"));
    CHECK (host.playing == "both");
    CHECK (host.playTo > host.playFrom);
    call (server, "play_style_from_audio", { { "listen", "original" }, { "from_bar", 3 }, { "to_bar", 5 } });
    CHECK (host.playFrom == 2);
    CHECK (host.playTo == 5);
    r = call (server, "play_style_from_audio", { { "listen", "stop" } });
    CHECK (r["playing"] == false);
    CHECK (call (server, "play_style_from_audio", { { "listen", "loud" } }).contains ("error"));
    CHECK (call (server, "play_style_from_audio", { { "listen", "style" }, { "section", "No Such Part" } }).contains ("error"));

    // Save: the style folder, or a path.
    CHECK (call (server, "save_style_from_audio", json::object())["saved_to"] == "/Styles/Made.sty");
    CHECK (call (server, "save_style_from_audio", { { "path", "/tmp/x.sty" } })["saved_to"] == "/tmp/x.sty");
}

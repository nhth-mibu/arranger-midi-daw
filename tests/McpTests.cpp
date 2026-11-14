#include "arranger/core/Document.h"
#include "arranger/core/McpServer.h"
#include "arranger/core/SongEdit.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

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

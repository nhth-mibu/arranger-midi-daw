#include "arranger/core/Document.h"
#include "arranger/core/McpServer.h"
#include "arranger/core/MidiAi.h"
#include "arranger/core/Notation.h"
#include "arranger/core/SongEdit.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace arranger;
using nlohmann::json;

namespace
{
    constexpr Tick kBeat = kTicksPerQuarter;
    constexpr Tick kBar = kBeat * 4;

    /** A host whose helper answers with canned notes and keeps the last request. */
    struct AiHost final : mcp::Host
    {
        Document doc;
        std::string lastHelper;
        json lastRequest;
        json answer = { { "notes", json::array() } };

        const Song& song() const override { return doc.song(); }
        std::uint64_t songRevision() const override { return doc.revision(); }
        void edit (const std::string& d, const std::function<void (Song&)>& fn) override { doc.edit (d, fn); }
        bool undo() override { return doc.undo(); }
        std::string runAiHelper (const std::string& helper, const std::string& request) override
        {
            lastHelper = helper;
            lastRequest = json::parse (request);
            return answer.dump();
        }
    };

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

    /** A song with a piano (8 bars of quarter notes), a bass and a drum track with a hi-hat pattern. */
    Song makeSong()
    {
        Song s;
        auto& piano = edit::addTrack (s, "Piano");
        piano.sound = { "Acoustic Grand Piano", -1, -1, 0 };
        const auto pianoId = piano.id;
        auto& bass = edit::addTrack (s, "Bass");
        bass.sound = { "Fingered Bass", -1, -1, 33 };
        const auto bassId = bass.id;
        auto& drums = edit::addTrack (s, "Drums");
        drums.output.channel = 10;
        const auto drumsId = drums.id;

        Clip p;
        p.start = 0;
        p.length = 8 * kBar;
        for (Tick t = 0; t < 8 * kBar; t += kBeat)
            p.notes.push_back ({ t, kBeat, static_cast<std::uint8_t> (60 + (t / kBeat) % 5), 80 });
        edit::addClip (s, pianoId, p);

        Clip b;
        b.start = 0;
        b.length = 4 * kBar;
        for (Tick t = 0; t < 4 * kBar; t += kBar)
            b.notes.push_back ({ t, kBar / 2, 36, 90 });
        edit::addClip (s, bassId, b);

        Clip d;
        d.start = 0;
        d.length = 4 * kBar;
        d.loopLength = kBar;
        for (Tick t = 0; t < kBar; t += kBeat / 2)
            d.notes.push_back ({ t, kBeat / 4, 42, 100 });
        d.notes.push_back ({ 0, kBeat / 4, 36, 100 });
        d.notes.push_back ({ 2 * kBeat, kBeat / 4, 38, 100 });
        edit::addClip (s, drumsId, d);
        return s;
    }

    std::vector<Note> notesIn (const Song& s, const std::string& track, Tick from, Tick to)
    {
        std::vector<Note> out;
        for (const auto& t : s.tracks)
            if (t.name == track)
                for (const auto& n : notation::timelineNotes (t))
                    if (n.start >= from && n.start < to)
                        out.push_back (n);
        return out;
    }
} // namespace

TEST_CASE ("AI tools: listed by the MCP server", "[ai]")
{
    AiHost host;
    mcp::Server server (host);
    const auto tools = json::parse (server.toolList());
    int found = 0;
    for (const auto& t : tools)
        if (ai::isTool (t.at ("name").get<std::string>()))
            ++found;
    CHECK (found == 5);
}

TEST_CASE ("AI tools: simple humanize needs no helper and is one undo step", "[ai]")
{
    AiHost host;
    host.doc.reset (makeSong());
    mcp::Server server (host);
    const auto before = notesIn (host.song(), "Piano", 0, 2 * kBar);
    const auto r = callTool (server, "ai_humanize", { { "track", "Piano" }, { "from_bar", 1 }, { "to_bar", 2 }, { "amount", 1.0 } });
    REQUIRE_FALSE (r.contains ("error"));
    CHECK (r["notes"] == 8);
    CHECK (host.lastHelper.empty());
    const auto after = notesIn (host.song(), "Piano", 0, 2 * kBar);
    REQUIRE (after.size() == before.size());
    bool changed = false;
    for (std::size_t i = 0; i < after.size(); ++i)
    {
        CHECK (after[i].pitch == before[i].pitch);
        CHECK (std::abs (after[i].start - before[i].start) <= kBeat / 16 + 1);
        changed = changed || after[i].start != before[i].start || after[i].velocity != before[i].velocity;
    }
    CHECK (changed);
    // Bars 3-8 are as they were.
    CHECK (notesIn (host.song(), "Piano", 2 * kBar, 8 * kBar).size() == 24);
    CHECK (host.undo());
    CHECK (notesIn (host.song(), "Piano", 0, 2 * kBar) == before);
}

TEST_CASE ("AI tools: simple humanize is the same for the same seed", "[ai]")
{
    std::vector<Note> notes;
    for (Tick t = 0; t < kBar; t += kBeat / 4)
        notes.push_back ({ t, kBeat / 4, 42, 90 });
    const auto a = ai::humanize (notes, {}, { 0.7, true, 5 });
    const auto b = ai::humanize (notes, {}, { 0.7, true, 5 });
    CHECK (a == b);
    // Off-beat 16ths come out softer than beats on average.
    int onBeat = 0, off = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        (i % 4 == 0 ? onBeat : off) += a[i].velocity;
    CHECK (onBeat / 4 > off / 12);
    CHECK (ai::humanize (notes, {}, { 0.0, true, 5 }) == notes);
}

TEST_CASE ("AI tools: Magenta groove sends the drum notes and writes the answer back", "[ai]")
{
    AiHost host;
    host.doc.reset (makeSong());
    mcp::Server server (host);
    host.answer = { { "notes", { { { "start", 0.02 }, { "length", 0.25 }, { "pitch", 36 }, { "velocity", 110 } },
                                 { { "start", 1.97 }, { "length", 0.25 }, { "pitch", 38 }, { "velocity", 95 } },
                                 { { "start", 9.0 }, { "length", 0.25 }, { "pitch", 38 }, { "velocity", 95 } } } } }; // 9.0: after bar 2, dropped
    const auto r = callTool (server, "ai_humanize", { { "track", "Drums" }, { "from_bar", 1 }, { "to_bar", 2 } });
    REQUIRE_FALSE (r.contains ("error"));
    CHECK (host.lastHelper == ai::kMagenta);
    CHECK (host.lastRequest["op"] == "groove");
    CHECK (host.lastRequest["quarters"] == 8.0);
    CHECK (host.lastRequest["notes"].size() == 20); // 2 bars of 8 hats, a kick and a snare
    CHECK (host.lastRequest["notes"][0]["drum"] == true);
    const auto notes = notesIn (host.song(), "Drums", 0, 2 * kBar);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].start == 19); // 0.02 quarters
    CHECK (notes[1].velocity == 95);
    // The loop goes on after the range.
    CHECK (notesIn (host.song(), "Drums", 2 * kBar, 4 * kBar).size() == 20);
}

TEST_CASE ("AI tools: Magenta groove is for drums in 4/4", "[ai]")
{
    AiHost host;
    host.doc.reset (makeSong());
    mcp::Server server (host);
    CHECK (callTool (server, "ai_humanize", { { "track", "Piano" }, { "from_bar", 1 }, { "engine", "magenta" } }).contains ("error"));
    host.doc.edit ("3/4", [] (Song& s) { s.timeSignature = { 3, 4 }; });
    CHECK (callTool (server, "ai_humanize", { { "track", "Drums" }, { "from_bar", 1 }, { "engine", "magenta" } }).contains ("error"));
}

TEST_CASE ("AI tools: drumify makes a new drum track", "[ai]")
{
    AiHost host;
    host.doc.reset (makeSong());
    mcp::Server server (host);
    host.answer = { { "notes", { { { "start", 0.0 }, { "length", 0.25 }, { "pitch", 36 }, { "velocity", 100 }, { "drum", true } } } } };
    const auto r = callTool (server, "ai_drumify", { { "track", "Bass" }, { "from_bar", 1 }, { "to_bar", 4 } });
    REQUIRE_FALSE (r.contains ("error"));
    CHECK (r["new_track"] == true);
    CHECK (host.lastRequest["op"] == "drumify");
    CHECK (host.lastRequest["notes"].size() == 4);
    REQUIRE (host.song().tracks.size() == 4);
    const auto& t = host.song().tracks.back();
    CHECK (t.name == "AI Drums");
    CHECK (notation::isDrumTrack (t));
    CHECK (notesIn (host.song(), "AI Drums", 0, 4 * kBar).size() == 1);
}

TEST_CASE ("AI tools: Magenta continue sends the bars before as a primer", "[ai]")
{
    AiHost host;
    host.doc.reset (makeSong());
    mcp::Server server (host);
    host.answer = { { "notes", { { { "start", 0.0 }, { "length", 1.0 }, { "pitch", 67 }, { "velocity", 80 } } } } };
    const auto r = callTool (server, "ai_continue", { { "track", "Bass" }, { "from_bar", 5 }, { "to_bar", 8 }, { "engine", "magenta" } });
    REQUIRE_FALSE (r.contains ("error"));
    CHECK (host.lastRequest["op"] == "continue");
    CHECK (host.lastRequest["kind"] == "melody");
    CHECK (host.lastRequest["primer"].size() == 4);
    CHECK (host.lastRequest["primer"][0]["start"] == -16.0);
    const auto notes = notesIn (host.song(), "Bass", 4 * kBar, 8 * kBar);
    REQUIRE (notes.size() == 1);
    CHECK (notes[0].start == 4 * kBar);
    CHECK (notes[0].pitch == 67);
    // Nothing before the range: an error the AI can read.
    CHECK (callTool (server, "ai_continue", { { "track", "Bass" }, { "from_bar", 1 }, { "engine", "magenta" } }).contains ("error"));
}

TEST_CASE ("AI tools: write a part under the others", "[ai]")
{
    AiHost host;
    host.doc.reset (makeSong());
    mcp::Server server (host);
    host.answer = { { "notes", { { { "start", 0.03 }, { "length", 0.9 }, { "pitch", 40 }, { "program", 33 } },
                                 { { "start", 1.0 }, { "length", 1.0 }, { "pitch", 64 }, { "program", 0 } } } } }; // a piano note: not asked for
    const auto r = callTool (server, "ai_write_part", { { "sound", "Fretless Bass" }, { "from_bar", 5 }, { "to_bar", 8 } });
    REQUIRE_FALSE (r.contains ("error"));
    CHECK (host.lastHelper == ai::kAnticipation);
    CHECK (host.lastRequest["op"] == "generate");
    CHECK (host.lastRequest["instruments"] == json ({ 35 }));
    CHECK (host.lastRequest["tempo"] == 120.0);
    CHECK (host.lastRequest["quarters"] == 16.0);
    // History: the bars before; controls: the piano in the range (the bass and drums stop at bar 4).
    CHECK (host.lastRequest["inputs"].size() > 0);
    CHECK (host.lastRequest["controls"].size() == 16);
    REQUIRE (host.song().tracks.size() == 4);
    CHECK (host.song().tracks.back().name == "AI Fretless Bass");
    CHECK (host.song().tracks.back().sound.program == 35);
    // The answer was for program 33 though: the note for another instrument isn't used.
    CHECK (notesIn (host.song(), "AI Fretless Bass", 0, 8 * kBar).empty());

    host.answer["notes"][0]["program"] = 35;
    const auto r2 = callTool (server, "ai_write_part", { { "track", "AI Fretless Bass" }, { "from_bar", 5 }, { "to_bar", 8 } });
    REQUIRE_FALSE (r2.contains ("error"));
    const auto notes = notesIn (host.song(), "AI Fretless Bass", 0, 8 * kBar);
    REQUIRE (notes.size() == 1);
    CHECK (notes[0].start == 4 * kBar); // snapped to 1/16
    CHECK (notes[0].length == kBeat);
}

TEST_CASE ("AI tools: write part needs other music in the bars", "[ai]")
{
    AiHost host;
    host.doc.reset (makeSong());
    mcp::Server server (host);
    CHECK (callTool (server, "ai_write_part", { { "sound", "drums" }, { "from_bar", 20 }, { "to_bar", 24 } }).contains ("error"));
}

TEST_CASE ("AI tools: continue all tracks fills each instrument's track", "[ai]")
{
    AiHost host;
    host.doc.reset (makeSong());
    mcp::Server server (host);
    host.answer = { { "notes", { { { "start", 0.0 }, { "length", 1.0 }, { "pitch", 40 }, { "program", 33 } },
                                 { { "start", 0.0 }, { "length", 0.25 }, { "pitch", 36 }, { "drum", true } },
                                 { { "start", 2.0 }, { "length", 1.0 }, { "pitch", 72 }, { "program", 0 } } } } };
    const auto r = callTool (server, "ai_continue", { { "from_bar", 9 }, { "to_bar", 10 } });
    REQUIRE_FALSE (r.contains ("error"));
    CHECK (host.lastRequest["instruments"].size() == 3);
    CHECK (r["tracks"].size() == 3);
    CHECK (notesIn (host.song(), "Bass", 8 * kBar, 10 * kBar).size() == 1);
    CHECK (notesIn (host.song(), "Drums", 8 * kBar, 10 * kBar).size() == 1);
    CHECK (notesIn (host.song(), "Piano", 8 * kBar, 10 * kBar).size() == 1);
}

TEST_CASE ("AI tools: melody over chords needs chord symbols", "[ai]")
{
    AiHost host;
    host.doc.reset (makeSong());
    mcp::Server server (host);
    CHECK (callTool (server, "ai_melody", { { "from_bar", 1 }, { "to_bar", 4 } }).contains ("error"));
    REQUIRE_FALSE (callTool (server, "set_chords", { { "track", "Piano" }, { "chords", { { { "bar", 1 }, { "chord", "C" } }, { { "bar", 3 }, { "chord", "Am7" } } } } }).contains ("error"));
    host.answer = { { "notes", { { { "start", 0.0 }, { "length", 0.5 }, { "pitch", 72 }, { "velocity", 90 } } } } };
    const auto r = callTool (server, "ai_melody", { { "from_bar", 2 }, { "to_bar", 4 }, { "sound", "Alto Sax" } });
    REQUIRE_FALSE (r.contains ("error"));
    REQUIRE (host.lastRequest["chords"].size() == 2);
    CHECK (host.lastRequest["chords"][0] == json ({ { "start", 0.0 }, { "chord", "C" } })); // the chord from before the range
    CHECK (host.lastRequest["chords"][1]["start"] == 4.0);
    CHECK (host.song().tracks.back().sound.program == 65);
}

TEST_CASE ("AI tools: a helper error comes back as a tool error, and nothing changes", "[ai]")
{
    AiHost host;
    host.doc.reset (makeSong());
    mcp::Server server (host);
    host.answer = { { "error", "the model is not downloaded" } };
    const auto r = callTool (server, "ai_drumify", { { "track", "Bass" }, { "from_bar", 1 } });
    REQUIRE (r.contains ("error"));
    CHECK (r["error"].get<std::string>().find ("not downloaded") != std::string::npos);
    CHECK (host.song().tracks.size() == 3);
}

TEST_CASE ("AI tools: the app's two-step call", "[ai]")
{
    AiHost host;
    host.doc.reset (makeSong());
    mcp::Server server (host);
    std::string reply;
    const auto message = json ({ { "jsonrpc", "2.0" }, { "id", 7 }, { "method", "tools/call" },
                                 { "params", { { "name", "ai_drumify" }, { "arguments", { { "track", "Bass" }, { "from_bar", 1 } } } } } }).dump();
    const auto call = server.startAiCall (message, reply);
    REQUIRE (call.has_value());
    CHECK (reply.empty());
    CHECK (call->job.helper == ai::kMagenta);
    CHECK (host.song().tracks.size() == 3); // nothing yet
    const auto done = json::parse (server.finishAiCall (*call, R"({"notes":[{"start":0,"length":0.25,"pitch":36,"drum":true}]})", {}));
    CHECK (done["id"] == 7);
    CHECK (done["result"]["isError"] == false);
    CHECK (host.song().tracks.size() == 4);

    // A failed helper.
    const auto failed = json::parse (server.finishAiCall (*call, {}, "Node is missing"));
    CHECK (failed["result"]["isError"] == true);

    // Other tools and the simple humanize don't need the two steps.
    CHECK_FALSE (server.startAiCall (R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"get_song","arguments":{}}})", reply).has_value());
    CHECK (reply.empty());
    const auto simple = json ({ { "jsonrpc", "2.0" }, { "id", 9 }, { "method", "tools/call" },
                                { "params", { { "name", "ai_humanize" }, { "arguments", { { "track", "Piano" }, { "from_bar", 1 } } } } } }).dump();
    CHECK_FALSE (server.startAiCall (simple, reply).has_value());
    CHECK (json::parse (reply)["result"]["isError"] == false);
}

TEST_CASE ("AI tools: snap", "[ai]")
{
    const std::vector<Note> notes { { 10, 100, 60, 90 }, { 235, 10, 62, 90 }, { 250, 300, 62, 90 } };
    const auto s = ai::snap (notes, kBeat / 4);
    REQUIRE (s.size() == 2); // the two 62s land on one step
    CHECK (s[0].start == 0);
    CHECK (s[0].length == 240);
    CHECK (s[1].start == 240);
}

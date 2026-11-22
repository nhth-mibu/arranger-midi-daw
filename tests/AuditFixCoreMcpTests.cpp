#include "arranger/core/DeviceProfile.h"
#include "arranger/core/Document.h"
#include "arranger/core/Groove.h"
#include "arranger/core/McpServer.h"
#include "arranger/core/MidiFile.h"
#include "arranger/core/SongEdit.h"
#include "arranger/core/StyleChord.h"
#include "arranger/core/StyleFromAudio.h"
#include "arranger/engine/Engine.h"
#include "arranger/engine/Playback.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <set>

using namespace arranger;
using nlohmann::json;

namespace
{
    constexpr Tick kBeat = kTicksPerQuarter;
    constexpr Tick kBar = kBeat * 4;

    struct AuditMcpHost final : mcp::Host
    {
        Document doc;
        DeviceLibrary deviceLib;
        std::string savedStylePath;
        StyleFromAudioStatus sfaStatus;

        const Song& song() const override { return doc.song(); }
        std::uint64_t songRevision() const override { return doc.revision(); }
        void edit (const std::string& title, const std::function<void (Song&)>& fn) override { doc.edit (title, fn); }
        const DeviceLibrary* devices() const override { return &deviceLib; }
        StyleFromAudioStatus styleFromAudioStatus() const override { return sfaStatus; }
        std::string saveStyleFromAudio (const std::string& path) override
        {
            savedStylePath = path.empty() ? "/Styles/Default.sty" : path;
            return savedStylePath;
        }
    };

    json callMcpTool (mcp::Server& server, const std::string& name, json args)
    {
        static int id = 1000;
        const auto raw = server.handle (json ({ { "jsonrpc", "2.0" },
                                                { "id", ++id },
                                                { "method", "tools/call" },
                                                { "params", { { "name", name }, { "arguments", std::move (args) } } } })
                                            .dump());
        const auto reply = json::parse (raw);
        const auto& result = reply.at ("result");
        const auto text = result.at ("content").at (0).at ("text").get<std::string>();
        if (result.at ("isError").get<bool>())
            return { { "error", text } };
        return json::parse (text);
    }
}

TEST_CASE ("C6 & H11: MCP handle and startAiCall guard non-string method and protocolVersion", "[audit][mcp]")
{
    AuditMcpHost host;
    mcp::Server server (host);

    for (const json& badMethod : { json (nullptr), json (123), json (true), json::array ({ "tools/call" }), json::object() })
    {
        const auto msg = json ({ { "jsonrpc", "2.0" },
                                 { "id", 1 },
                                 { "method", badMethod },
                                 { "params", { { "name", "ai_humanize" } } } })
                             .dump();
        std::string aiReply;
        CHECK_NOTHROW (
            [&] {
                const auto call = server.startAiCall (msg, aiReply);
                CHECK_FALSE (call.has_value());
                CHECK (aiReply.empty());
            }());

        std::string handled;
        CHECK_NOTHROW (handled = server.handle (msg));
        REQUIRE_FALSE (handled.empty());
        const auto parsed = json::parse (handled);
        CHECK (parsed.at ("error").at ("code") == -32600);
    }

    for (const json& badProto : { json (nullptr), json (20250618), json (false), json::array(), json::object() })
    {
        const auto initMsg = json ({ { "jsonrpc", "2.0" },
                                     { "id", 2 },
                                     { "method", "initialize" },
                                     { "params", { { "protocolVersion", badProto } } } })
                                 .dump();
        std::string reply;
        CHECK_NOTHROW (reply = server.handle (initMsg));
        const auto parsed = json::parse (reply);
        CHECK (parsed.at ("result").at ("protocolVersion") == "2025-06-18");
    }
}

TEST_CASE ("H12: extract_groove computes barRange on loaded MIDI song and validates paths", "[audit][mcp]")
{
    AuditMcpHost host;
    // Open song is empty (1 bar in 4/4), while the MIDI file on disk has 4 bars in 4/4
    // where bars 1-2 are straight and bars 3-4 are heavily swung.
    mcp::Server server (host);

    Song midi;
    midi.tempo = TempoMap (100.0);
    auto& drums = edit::addTrack (midi, "Drums");
    drums.output = { TrackOutput::Kind::soundFont, "", 10 };
    Clip c;
    c.length = 4 * kBar;
    for (int bar = 0; bar < 4; ++bar)
    {
        const Tick base = bar * kBar;
        for (int e = 0; e < 8; ++e)
        {
            const Tick swingOffset = (bar >= 2 && e % 2 == 1) ? 160 : 0;
            c.notes.push_back ({ base + e * (kBeat / 2) + swingOffset, 60, 42, 100 });
        }
    }
    edit::addClip (midi, drums.id, c);

    const auto tmpMid = std::filesystem::temp_directory_path() / "arranger_audit_h12_groove.mid";
    midi_file::save (midi, tmpMid);

    const auto res = callMcpTool (server, "extract_groove", { { "name", "SwungTail" }, { "file", tmpMid.string() }, { "from_bar", 3 }, { "to_bar", 4 } });
    // Also test omitting to_bar when from_bar (3) > open song lastBar (1): must default to midi's lastBar (4), not s's lastBar (1).
    const auto resFromOnly = callMcpTool (server, "extract_groove", { { "name", "SwungTailFromOnly" }, { "file", tmpMid.string() }, { "from_bar", 3 } });
    std::filesystem::remove (tmpMid);
    REQUIRE_FALSE (res.contains ("error"));
    CHECK (res.at ("swing").get<double>() > 62.0);
    REQUIRE_FALSE (resFromOnly.contains ("error"));
    CHECK (resFromOnly.at ("swing").get<double>() > 62.0);

    // Reject traversal, empty paths, and non-media files in extract_groove.
    CHECK (callMcpTool (server, "extract_groove", { { "file", "" } }).contains ("error"));
    CHECK (callMcpTool (server, "extract_groove", { { "file", "../outside.mid" } }).contains ("error"));
    CHECK (callMcpTool (server, "extract_groove", { { "file", "sub/../../outside.wav" } }).contains ("error"));
    CHECK (callMcpTool (server, "extract_groove", { { "file", "/etc/passwd" } }).contains ("error"));

    // Reject empty paths, traversal, and non-style extensions in save_style_from_audio.
    CHECK (callMcpTool (server, "save_style_from_audio", { { "path", "" } }).contains ("error"));
    CHECK (callMcpTool (server, "save_style_from_audio", { { "path", "../escape.sty" } }).contains ("error"));
    CHECK (callMcpTool (server, "save_style_from_audio", { { "path", "a/../../escape.sty" } }).contains ("error"));
    CHECK (callMcpTool (server, "save_style_from_audio", { { "path", "/tmp/not_a_style.txt" } }).contains ("error"));
    CHECK (host.savedStylePath.empty());
}

TEST_CASE ("H13: find_sounds handles empty first sound group without crashing", "[audit][mcp]")
{
    AuditMcpHost host;
    const auto profile = device_profile::parse (R"(profile: 1
id: test-empty-first-group
name: Empty First Group Synth
sounds:
  - group: Empty Group
    items: []
  - group: Numeric Sounds
    items:
      - name: "001"
        program: 1
        bank_msb: 0
        bank_lsb: 0
)");
    host.deviceLib.add (profile);
    mcp::Server server (host);

    const auto res = callMcpTool (server, "find_sounds", { { "device", "test-empty-first-group" }, { "search", "nonexistent_voice_xyz" } });
    REQUIRE_FALSE (res.contains ("error"));
    CHECK (res.at ("matches") == 0);
    REQUIRE (res.contains ("note"));
    CHECK (res.at ("note").get<std::string>().find ("001") != std::string::npos);
}

TEST_CASE ("H14: edit_clips move rejects slot clips and incompatible target tracks without deleting clips", "[audit][mcp]")
{
    AuditMcpHost host;
    ClipId midiTimelineId = 0, midiSlotId = 0, audioTimelineId = 0, audioSlotId = 0;
    host.doc.edit ("setup", [&] (Song& s) {
        const auto midiTrackId = edit::addTrack (s, "Keys").id;
        const auto audioTrackId = edit::addAudioTrack (s, "Vocals").id;

        Clip mc;
        mc.length = kBar;
        mc.notes = { { 0, kBeat, 60, 100 } };
        midiTimelineId = edit::addClip (s, midiTrackId, mc);

        Clip slotMc = mc;
        slotMc.id = 0;
        midiSlotId = edit::setSlot (s, midiTrackId, 0, slotMc);

        AudioClip ac;
        ac.file = "vox.wav";
        ac.length = kBar;
        audioTimelineId = edit::addAudioClip (s, audioTrackId, ac);

        AudioClip slotAc = ac;
        slotAc.id = 0;
        audioSlotId = edit::setAudioSlot (s, audioTrackId, 0, slotAc);
    });

    mcp::Server server (host);

    // 1. Moving a MIDI slot clip or audio slot clip is rejected and does not delete the slot.
    CHECK (callMcpTool (server, "edit_clips", { { "action", "move" }, { "clips", { midiSlotId } }, { "to_bar", 2 } }).contains ("error"));
    CHECK (edit::findClipPtr (host.song(), midiSlotId) != nullptr);
    CHECK (callMcpTool (server, "edit_clips", { { "action", "move" }, { "clips", { audioSlotId } }, { "to_bar", 2 } }).contains ("error"));
    CHECK (edit::findAudioClip (host.song(), audioSlotId) != nullptr);

    // 2. Moving a MIDI timeline clip to an audio track is rejected and preserves the MIDI clip.
    CHECK (callMcpTool (server, "edit_clips", { { "action", "move" }, { "clips", { midiTimelineId } }, { "to_track", "Vocals" } }).contains ("error"));
    CHECK (edit::findClipPtr (host.song(), midiTimelineId) != nullptr);

    // 3. Moving an audio timeline clip to a MIDI track is rejected and preserves the audio clip.
    CHECK (callMcpTool (server, "edit_clips", { { "action", "move" }, { "clips", { audioTimelineId } }, { "to_track", "Keys" } }).contains ("error"));
    CHECK (edit::findAudioClip (host.song(), audioTimelineId) != nullptr);

    // 4. Moving multi-track clips past the last track is rejected and preserves all clips.
    ClipId secondMidiId = 0;
    host.doc.edit ("add second midi track", [&] (Song& s) {
        auto& t3 = edit::addTrack (s, "Lead");
        Clip mc;
        mc.length = kBar;
        mc.notes = { { 0, kBeat, 67, 100 } };
        secondMidiId = edit::addClip (s, t3.id, mc);
    });
    // Keys is track index 0, Lead is track index 2 (offset +2); moving both to Vocals (index 1) or Lead (index 2) overflows track list.
    CHECK (callMcpTool (server, "edit_clips", { { "action", "move" }, { "clips", { midiTimelineId, secondMidiId } }, { "to_track", "Lead" } }).contains ("error"));
    CHECK (edit::findClipPtr (host.song(), midiTimelineId) != nullptr);
    CHECK (edit::findClipPtr (host.song(), secondMidiId) != nullptr);
}

TEST_CASE ("H21: style_chord::transpose avoids octave-wrap discontinuity when sourceRoot != 0", "[audit][style]")
{
    StyleChannelRule rule;
    rule.sourceRoot = 7; // Recorded in G
    rule.sourceChordType = static_cast<int> (ChordType::major);
    rule.transpose.ntr = StyleTranspose::Ntr::rootTransposition;
    rule.transpose.ntt = StyleTranspose::Ntt::melody;
    rule.transpose.highKey = 6; // F#
    rule.transpose.lowLimit = 0;
    rule.transpose.highLimit = 127;

    // Playing the source chord G itself must produce zero transposition (pitch 43 stays 43).
    const auto atSource = style_chord::transpose (43, rule, { 7, ChordType::major, -1 });
    REQUIRE (atSource.has_value());
    CHECK (*atSource == 43);

    // Across all 12 target roots, the transposed root note must form one contiguous 12-semitone octave
    // [43..54] with the single wrap occurring between highKey (F# = 6 -> 54) and G (7 -> 43).
    std::set<int> outputs;
    for (int root = 0; root < 12; ++root)
    {
        const auto moved = style_chord::transpose (43, rule, { root, ChordType::major, -1 });
        REQUIRE (moved.has_value());
        CHECK (*moved % 12 == root);
        CHECK (*moved >= 43);
        CHECK (*moved <= 54);
        outputs.insert (*moved);
    }
    CHECK (outputs.size() == 12);
    CHECK (*style_chord::transpose (43, rule, { 6, ChordType::major, -1 }) == 54); // F# (highKey)
    CHECK (*style_chord::transpose (43, rule, { 7, ChordType::major, -1 }) == 43); // G (wraps 1 octave below F#, not 23 semitones)
}

TEST_CASE ("H22: SongEdit clearRange, audioSlot move/split, and looping splitClip invariants", "[audit][editing]")
{
    SECTION ("clearRange with to <= from is a no-op")
    {
        Song song;
        const auto track = edit::addTrack (song, "Keys").id;
        Clip c;
        c.start = 0;
        c.length = kBar * 2;
        c.notes = { { 0, kBeat, 60, 100 }, { kBar, kBeat, 64, 100 } };
        const auto id = edit::addClip (song, track, c);

        edit::clearRange (song, track, kBar, kBar);
        edit::clearRange (song, track, kBar, kBeat);
        REQUIRE (song.tracks.front().clips.size() == 1);
        CHECK (song.tracks.front().clips.front()->id == id);
        CHECK (song.tracks.front().clips.front()->length == kBar * 2);
        CHECK (song.tracks.front().clips.front()->notes.size() == 2);
    }

    SECTION ("moveClip and splitClip ignore Clip Grid audioSlots")
    {
        Song song;
        const auto track = edit::addAudioTrack (song, "Audio").id;
        AudioClip ac;
        ac.file = "loop.wav";
        ac.length = kBar;
        const auto slotId = edit::setAudioSlot (song, track, 0, ac);
        REQUIRE (slotId != 0);

        edit::moveClip (song, slotId, kBar, track);
        CHECK (song.tracks.front().audio.empty());
        REQUIRE (song.tracks.front().audioSlots.size() == 1);
        CHECK (song.tracks.front().audioSlots[0]->id == slotId);

        CHECK (edit::splitClip (song, slotId, kBeat * 2) == 0);
        CHECK (song.tracks.front().audio.empty());
        CHECK (song.tracks.front().audioSlots[0]->length == kBar);
    }

    SECTION ("splitClip on looping MIDI clips clamps right notes/controls and cycle lengths")
    {
        Song song;
        const auto track = edit::addTrack (song, "LoopKeys").id;
        Clip c;
        c.start = 0;
        c.length = kBar + kBeat; // 1.25 bars (4800 ticks)
        c.loopLength = kBar;     // 1-bar loop (3840 ticks)
        // Note 1 spans past the 1-Beat tail; Note 2 starts at beat 3 (1920 >= 960); Note 3 is a ghost note past loopLength.
        c.notes = {
            { 0, kBeat * 2, 60, 100 },
            { kBeat * 2, kBeat * 3, 64, 100 }, // extends past loopLength (1920 + 2880 = 4800 > 3840)
            { kBar + 100, kBeat, 67, 100 }     // hidden ghost note past loopLength
        };
        c.controls = {
            { 0, ControlEvent::Kind::controller, 1, 64 },
            { kBeat * 2, ControlEvent::Kind::controller, 1, 100 },
            { kBar + 50, ControlEvent::Kind::controller, 1, 127 }
        };
        const auto expectedPlayed = edit::playedClip (c);

        // Split on exact loop boundary (cut = kBar, so cut % loop == 0 and right.length = kBeat < loop)
        Song s1 = song;
        const auto id1 = edit::addClip (s1, track, c);
        const auto rightId1 = edit::splitClip (s1, id1, kBar);
        REQUIRE (rightId1 != 0);
        const auto right1 = edit::findClipPtr (s1, rightId1);
        REQUIRE (right1 != nullptr);
        CHECK (right1->length == kBeat);
        CHECK (right1->loopLength == 0);
        for (const auto& n : right1->notes)
        {
            CHECK (n.start < right1->length);
            CHECK (n.end() <= right1->length);
        }
        for (const auto& ctrl : right1->controls)
            CHECK (ctrl.at < right1->length);

        // Split off-boundary (cut = kBeat, so cut % loop != 0); note at kBeat*2 has length 3*kBeat which must clamp at loop end kBar.
        Song s2 = song;
        Clip c2 = c;
        c2.length = kBar * 2;
        c2.notes[0].length = kBeat; // ends at cut = kBeat so it is not bisected by the cut itself
        const auto expectedPlayed2 = edit::playedClip (c2);
        const auto id2 = edit::addClip (s2, track, c2);
        const auto rightId2 = edit::splitClip (s2, id2, kBeat);
        REQUIRE (rightId2 != 0);
        const auto right2 = edit::findClipPtr (s2, rightId2);
        REQUIRE (right2 != nullptr);
        REQUIRE (! right2->notes.empty());
        CHECK (right2->notes.front().length == kBeat * 2); // clamped to base + loop (kBar - 2*kBeat = 2*kBeat)
        edit::joinClips (s2, { id2, rightId2 });
        REQUIRE (s2.tracks.front().clips.size() == 1);
        CHECK (s2.tracks.front().clips.front()->notes == expectedPlayed2.notes);
    }
}

TEST_CASE ("H23: Groove 1/32 meter, StyleFromAudio empty beatChords preview, and Engine playAudio guards", "[audit][core][engine]")
{
    SECTION ("groove::fromNotes in 1/32 meter does not divide by zero")
    {
        const TimeSignature sig { 1, 32 };
        std::vector<groove::TimedNote> notes;
        for (int i = 0; i < 8; ++i)
            notes.push_back ({ static_cast<double> (i * 120), 42, 100 });

        groove::ExtractOptions o;
        o.bars = 1;
        o.grid = kTicksPerQuarter / 4; // 240 > bar (120)
        o.drums = true;
        const auto g = groove::fromNotes (notes, sig, o);
        CHECK (g.steps() >= 1);
        CHECK (g.grid == 120);
    }

    SECTION ("style_from_audio::preview guards empty beatChords before std::clamp")
    {
        style_from_audio::Made made;
        made.style.name = "EmptyChords";
        made.style.tempo = 120.0;
        made.style.timeSignature = { 4, 4 };
        StyleSection sec;
        sec.name = "Main A";
        sec.length = kBar;
        Clip part;
        part.length = kBar;
        part.notes = { { 0, kBeat, 60, 100 } };
        sec.parts[11] = part;
        made.style.sections.push_back (sec);

        StyleRuleSet rules;
        rules.sections = { "Main A" };
        StyleChannelRule rule;
        rule.sourceChannel = 11;
        rule.destChannel = 11;
        rules.rules.push_back (rule);
        made.style.ruleSets.push_back (rules);

        made.grid.bpm = 120.0;
        made.grid.beatsPerBar = 4;
        made.plan.bars = 1;
        made.plan.barSection = { "Main A" };
        made.plan.beatChords.clear(); // explicitly empty!

        made.parts.push_back ({ "bass", 11, false, {} });

        Song previewed;
        CHECK_NOTHROW (previewed = style_from_audio::preview (made, style_from_audio::Listen::style));
        REQUIRE (previewed.tracks.size() == 1);
        REQUIRE (previewed.tracks[0].clips.size() == 1);
        CHECK (previewed.tracks[0].clips[0]->notes.size() == 1);
    }

    SECTION ("Engine::playAudio handles boundary start times and zero offset without negative index or gain")
    {
        auto sample = std::make_shared<Sample>();
        sample->sampleRate = 48000.0;
        sample->channels.resize (2, std::vector<float> (48000, 0.5f));

        Song song;
        song.tempo = TempoMap (127.0); // non-integer tick/second ratio to exercise floating-point boundary rounding
        auto& track = edit::addAudioTrack (song, "Audio");
        AudioClip clip;
        clip.file = "test.wav";
        clip.start = 137;
        clip.length = 1920;
        clip.offset = 0.0;
        clip.fadeIn = 240;
        clip.fadeOut = 240;
        edit::addAudioClip (song, track.id, clip);

        AudioClip slotClip = clip;
        slotClip.id = 0;
        slotClip.start = 0;
        edit::setAudioSlot (song, track.id, 0, slotClip);

        Engine engine;
        engine.prepare (48000.0, 64);
        engine.setSnapshot (std::make_shared<const PlaybackSnapshot> (buildSnapshot (song, {}, [&] (const std::string&) { return sample; })));
        engine.play();

        float left[64] {}, right[64] {};
        float* channels[] { left, right };
        AudioBlock block { channels, 2, 64 };
        MidiEventBuffer in, out;
        for (int b = 0; b < 40; ++b)
        {
            if (b == 20)
                engine.launchSlot (track.id, 0, 0);
            std::fill (std::begin (left), std::end (left), 0.0f);
            std::fill (std::begin (right), std::end (right), 0.0f);
            engine.process (block, in, out);
            for (int i = 0; i < 64; ++i)
            {
                CHECK (std::isfinite (left[i]));
                CHECK (left[i] >= -1.0f);
                CHECK (left[i] <= 1.0f);
            }
        }
    }
}

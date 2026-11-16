#include "TestRender.h"

#include "arranger/core/SongEdit.h"

#include <catch2/catch_test_macros.hpp>

using namespace arranger;
using test::Renderer;

namespace
{
    /** A song with one hardware track playing quarter notes C4, D4, E4, F4 in bar 1. */
    Song quarterNotes (double bpm = 120.0)
    {
        Song s;
        s.tempo = TempoMap (bpm);
        s.metronome.enabled = false;
        auto& t = edit::addTrack (s, "Keys");
        t.output = { TrackOutput::Kind::midiPort, "Keyboard", 1 };
        const auto id = t.id;
        Clip c;
        c.length = 3840;
        c.notes = { { 0, 480, 60, 100 }, { 960, 480, 62, 100 }, { 1920, 480, 64, 100 }, { 2880, 480, 65, 100 } };
        edit::addClip (s, id, c);
        return s;
    }

    Engine& playing (Engine& e, const Song& s, double rate = 48000.0)
    {
        e.prepare (rate, 1024);
        e.loadSong (s);
        e.play();
        return e;
    }
}

TEST_CASE ("Notes go to their hardware port on exact samples")
{
    Engine e;
    Renderer r (playing (e, quarterNotes()));
    r.runRandom (48000 * 2, 7);

    const auto ons = r.noteOns();
    REQUIRE (ons.size() == 4);
    for (std::size_t i = 0; i < 4; ++i)
        CHECK (ons[i].first == static_cast<std::int64_t> (i) * 24000);
    CHECK (ons[1].second == 62);
    for (const auto& s : r.sent)
        CHECK (s.event.port == 1);

    // Each note-off arrives 480 ticks (12000 samples) after its note-on.
    int offs = 0;
    for (const auto& s : r.sent)
        if (s.event.isNoteOff())
        {
            CHECK (s.sample % 24000 == 12000);
            ++offs;
        }
    CHECK (offs == 4);
}

TEST_CASE ("Setup messages (bank, program, volume) are sent when play starts")
{
    auto song = quarterNotes();
    song.tracks[0].sound = { "Strings", 0, 117, 48 };
    Engine e;
    Renderer r (playing (e, song));
    r.runFixed (256, 256);

    REQUIRE (r.sent.size() >= 7);
    CHECK (r.sent[0].event.bytes[0] == 0xb0);
    CHECK (r.sent[0].event.bytes[1] == 0);
    CHECK (r.sent[1].event.bytes[1] == 32);
    CHECK (r.sent[1].event.bytes[2] == 117);
    CHECK (r.sent[2].event.bytes[0] == 0xc0);
    CHECK (r.sent[2].event.size == 2);
    CHECK (r.sent[2].event.bytes[1] == 48);
}

TEST_CASE ("Loop plays the same bar again, sample-exact across the wrap")
{
    auto song = quarterNotes();
    song.loop = { true, 0, 3840 };
    Engine e;
    Renderer r (playing (e, song));
    r.runRandom (48000 * 6, 3); // three passes of a 2-second bar

    const auto ons = r.noteOns();
    REQUIRE (ons.size() == 12);
    for (std::size_t i = 0; i < ons.size(); ++i)
        CHECK (ons[i].first == static_cast<std::int64_t> (i) * 24000);
}

TEST_CASE ("A loop that ends on a block boundary wraps")
{
    SECTION ("one bar with 1000-sample blocks")
    {
        // 1 bar at 120 BPM is 96000 samples: exactly 96 blocks of 1000 samples.
        auto song = quarterNotes();
        song.loop = { true, 0, 3840 };
        Engine e;
        Renderer r (playing (e, song), 1000);
        r.runFixed (48000 * 4, 1000); // two passes of the bar

        const auto ons = r.noteOns();
        REQUIRE (ons.size() == 8);
        for (std::size_t i = 0; i < ons.size(); ++i)
            CHECK (ons[i].first == static_cast<std::int64_t> (i) * 24000);
    }

    SECTION ("four bars with 512-sample blocks")
    {
        // 4 bars at 120 BPM is 384000 samples: exactly 750 blocks of 512 samples.
        auto song = quarterNotes();
        song.loop = { true, 0, 15360 };
        Engine e;
        Renderer r (playing (e, song), 512);
        r.runFixed (48000 * 10, 512); // into the second pass of the four bars

        const auto ons = r.noteOns();
        REQUIRE (ons.size() == 8);
        constexpr std::int64_t pass = 384000;
        for (std::size_t i = 0; i < ons.size(); ++i)
            CHECK (ons[i].first == static_cast<std::int64_t> (i % 4) * 24000 + (i < 4 ? 0 : pass));
    }
}

TEST_CASE ("Tempo changes inside the song are followed")
{
    auto song = quarterNotes();
    song.tempo.setPoints ({ { 0, 120.0 }, { 1920, 60.0 } }); // half-time from beat 3
    Engine e;
    Renderer r (playing (e, song));
    r.runFixed (48000 * 4, 333);

    const auto ons = r.noteOns();
    REQUIRE (ons.size() == 4);
    CHECK (ons[2].first == 48000);
    CHECK (ons[3].first == 48000 + 48000); // a beat at 60 BPM is a full second
}

TEST_CASE ("Mute and solo")
{
    auto song = quarterNotes();
    auto& second = edit::addTrack (song, "Bass");
    second.output = { TrackOutput::Kind::midiPort, "Keyboard", 2 };
    const auto bassId = second.id;
    Clip c;
    c.notes = { { 0, 480, 36, 100 } };
    edit::addClip (song, bassId, c);

    SECTION ("muted track sends no notes")
    {
        song.tracks[0].mixer.mute = true;
        Engine e;
        Renderer r (playing (e, song));
        r.runFixed (48000 * 2, 512);
        const auto ons = r.noteOns();
        REQUIRE (ons.size() == 1);
        CHECK (ons[0].second == 36);
    }
    SECTION ("solo silences the others")
    {
        song.tracks[0].mixer.solo = true;
        Engine e;
        Renderer r (playing (e, song));
        r.runFixed (48000 * 2, 512);
        CHECK (r.noteOns().size() == 4);
    }
}

TEST_CASE ("Stopping ends held notes")
{
    Engine e;
    Renderer r (playing (e, quarterNotes()));
    r.runFixed (6000, 500); // inside the first note
    e.stop();
    r.runFixed (1000, 500);

    REQUIRE_FALSE (r.sent.empty());
    CHECK (r.sent.back().event.isNoteOff());
    CHECK (r.sent.back().event.bytes[1] == 60);
    CHECK (r.sent.back().sample == 6000);
}

TEST_CASE ("Editing during playback keeps unchanged tracks' notes, ends changed ones")
{
    auto song = quarterNotes();
    Engine e;
    Renderer r (playing (e, song));
    r.runFixed (6000, 500);

    // Same content (only a mixer change): the held note continues.
    song.tracks[0].mixer.volume = 80;
    e.loadSong (song);
    r.runFixed (500, 500);
    for (const auto& s : r.sent)
        CHECK_FALSE (s.event.isNoteOff());

    // Content changed: the held note is released right away.
    edit::modifyClip (song, song.tracks[0].clips[0]->id, [] (Clip& c) { c.notes.pop_back(); });
    e.loadSong (song);
    r.runFixed (500, 500);
    REQUIRE_FALSE (r.sent.empty());
    CHECK (r.sent.back().event.isNoteOff());
}

TEST_CASE ("Recording captures input at its song position")
{
    auto song = quarterNotes();
    Engine e;
    playing (e, song);
    e.setRecording (true);
    Renderer r (e, 480);

    r.runFixed (24000, 480);                                     // one beat
    r.input.add (MidiEvent::make (240, 0x90, 72, 99, 0));        // halfway into the next 480-sample block
    r.runFixed (480, 480);

    auto rec = e.popRecorded();
    REQUIRE (rec.has_value());
    CHECK (rec->bytes[1] == 72);
    // 24000 + 240 samples at 120 BPM / 48 kHz = 1.01 beats
    CHECK (rec->tick == 960.0 + 240.0 * 960.0 / 24000.0);
    CHECK_FALSE (e.popRecorded().has_value());
}

TEST_CASE ("MIDI thru goes to the chosen port and channel")
{
    Engine e;
    e.prepare (48000.0, 512);
    e.loadSong ({});
    e.setThru (3, 5);
    Renderer r (e, 512);
    r.runFixed (512, 512);
    r.input.add (MidiEvent::make (10, 0x90, 64, 80, 0));
    r.runFixed (512, 512);

    REQUIRE (r.sent.size() == 1);
    CHECK (r.sent[0].event.port == 3);
    CHECK (r.sent[0].event.bytes[0] == 0x94); // channel 5
    CHECK (r.sent[0].sample == 512 + 10);
}

TEST_CASE ("Snapshots are handed over and freed safely")
{
    Engine e;
    e.prepare (48000.0, 256);
    Renderer r (e, 256);
    for (int i = 0; i < 100; ++i)
    {
        e.loadSong (quarterNotes (100.0 + i));
        r.runFixed (256, 256);
        e.collectGarbage();
    }
    SUCCEED();
}

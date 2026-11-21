#include "TestRender.h"

#include "arranger/core/SongEdit.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

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

    const auto off = std::find_if (r.sent.begin(), r.sent.end(), [] (const test::Sent& x) { return x.event.isNoteOff(); });
    REQUIRE (off != r.sent.end());
    CHECK (off->event.bytes[1] == 60);
    CHECK (off->sample == 6000);
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

// ---- hanging notes ---------------------------------------------------------------------

namespace
{
    bool isControl (const test::Sent& x, int number, int value)
    {
        return (x.event.bytes[0] & 0xf0) == 0xb0 && x.event.bytes[1] == number && x.event.bytes[2] == value;
    }
}

TEST_CASE ("Stop sends All Notes Off once to each port and channel the song uses")
{
    auto song = quarterNotes();
    auto& bass = edit::addTrack (song, "Bass");
    bass.output = { TrackOutput::Kind::midiPort, "Keyboard", 2 };
    auto& keys2 = edit::addTrack (song, "Keys 2");
    keys2.output = { TrackOutput::Kind::midiPort, "Keyboard", 1 }; // the same channel as Keys
    Engine e;
    Renderer r (playing (e, song));
    r.runFixed (6000, 500);
    r.sent.clear();
    e.stop();
    r.runFixed (500, 500);

    int channel1 = 0, channel2 = 0;
    for (const auto& x : r.sent)
        if (isControl (x, 123, 0))
            ++((x.event.bytes[0] & 0x0f) == 0 ? channel1 : channel2);
    CHECK (channel1 == 1);
    CHECK (channel2 == 1);
    // The held note ends before the safety net.
    REQUIRE_FALSE (r.sent.empty());
    CHECK (r.sent.front().event.isNoteOff());
}

TEST_CASE ("Stop and jumps lift a sustain pedal the song pressed")
{
    auto song = quarterNotes();
    edit::modifyClip (song, song.tracks[0].clips[0]->id, [] (Clip& c) {
        c.controls.push_back ({ 0, ControlEvent::Kind::controller, 64, 127 });
    });
    Engine e;

    SECTION ("stop")
    {
        Renderer r (playing (e, song));
        r.runFixed (6000, 500);
        e.stop();
        r.runFixed (500, 500);
        CHECK (std::any_of (r.sent.begin(), r.sent.end(), [] (const test::Sent& x) { return isControl (x, 64, 0); }));
    }
    SECTION ("locate")
    {
        Renderer r (playing (e, song));
        r.runFixed (6000, 500);
        e.locate (3000);
        r.runFixed (500, 500);
        CHECK (std::any_of (r.sent.begin(), r.sent.end(), [] (const test::Sent& x) { return isControl (x, 64, 0); }));
    }
}

TEST_CASE ("Changed notes in a clip at the same address still end the held notes")
{
    // A style moved to a new chord gets new clips, and the memory allocator can put them where
    // the last chord's clips were. The engine must still see that the notes changed.
    Song song;
    song.metronome.enabled = false;
    auto& t = edit::addTrack (song, "Pad");
    t.output = { TrackOutput::Kind::midiPort, "Keyboard", 1 };
    const auto id = t.id;
    Clip c;
    c.length = 3840;
    c.notes = { { 0, 3840, 60, 100 } }; // a whole bar of C
    edit::setSlot (song, id, 0, c);

    Engine e;
    e.prepare (48000.0, 1024);
    e.loadSong (song);
    e.launchSlot (id, 0, 0);
    Renderer r (e, 500);
    r.runFixed (6000, 500);
    REQUIRE (r.noteOns().size() == 1);

    // The same clip object, now playing D (as a chord change would make it).
    auto& clip = const_cast<Clip&> (*song.tracks[0].slots[0]);
    clip.notes[0].pitch = 62;
    e.loadSong (song);
    r.runFixed (500, 500);

    const auto off = std::find_if (r.sent.begin(), r.sent.end(), [] (const test::Sent& x) { return x.event.isNoteOff(); });
    REQUIRE (off != r.sent.end());
    CHECK (off->event.bytes[1] == 60);
    CHECK (off->sample == 6000);
}

TEST_CASE ("A hardware port keeps its number when the song changes")
{
    Song song;
    auto& a = edit::addTrack (song, "A");
    a.output = { TrackOutput::Kind::midiPort, "PSR", 1 };
    auto& b = edit::addTrack (song, "B");
    b.output = { TrackOutput::Kind::midiPort, "D-05", 1 };
    const auto first = buildSnapshot (song);
    REQUIRE (first.ports == std::vector<std::string> { "PSR", "D-05" });

    // Track A is gone: without the known ports, D-05 would become port 1.
    song.tracks.erase (song.tracks.begin());
    const auto next = buildSnapshot (song, {}, {}, {}, first.ports);
    CHECK (next.ports == first.ports);
    REQUIRE (next.tracks.size() == 1);
    CHECK (next.tracks[0].port == 2);
}

TEST_CASE ("A live key's note-off follows its note-on after the thru target changes")
{
    Engine e;
    e.prepare (48000.0, 512);
    e.loadSong ({});
    e.setThru (3, 5);
    Renderer r (e, 512);
    r.runFixed (512, 512);
    r.input.add (MidiEvent::make (10, 0x90, 64, 80, 0));
    r.input.add (MidiEvent::make (11, 0xb0, 64, 127, 0)); // pedal down
    r.runFixed (512, 512);

    e.setThru (4, 2); // another track is chosen while the key and pedal are down
    r.runFixed (512, 512);
    r.input.add (MidiEvent::make (20, 0x80, 64, 0, 0));
    r.input.add (MidiEvent::make (21, 0xb0, 64, 0, 0));
    r.runFixed (512, 512);

    const auto off = std::find_if (r.sent.begin(), r.sent.end(), [] (const test::Sent& x) { return x.event.isNoteOff(); });
    REQUIRE (off != r.sent.end());
    CHECK (off->event.port == 3);
    CHECK (off->event.bytes[0] == 0x84);
    // The pedal goes up on the old target, and on the new one.
    CHECK (std::any_of (r.sent.begin(), r.sent.end(), [] (const test::Sent& x) { return x.event.port == 3 && x.event.bytes[0] == 0xb4 && isControl (x, 64, 0); }));
    CHECK (std::any_of (r.sent.begin(), r.sent.end(), [] (const test::Sent& x) { return x.event.port == 4 && x.event.bytes[0] == 0xb1 && isControl (x, 64, 0); }));
}

TEST_CASE ("Panic silences every channel the song uses and live keys still held")
{
    Engine e;
    Renderer r (playing (e, quarterNotes()));
    e.setThru (3, 5);
    r.runFixed (512, 512);
    r.input.add (MidiEvent::make (10, 0x90, 64, 80, 0));
    r.runFixed (5500, 500);
    r.sent.clear();

    e.panic();
    r.runFixed (500, 500);
    const auto has = [&] (std::uint16_t port, std::uint8_t status, int number, int value) {
        return std::any_of (r.sent.begin(), r.sent.end(), [&] (const test::Sent& x) {
            return x.event.port == port && x.event.bytes[0] == status && x.event.bytes[1] == number && x.event.bytes[2] == value;
        });
    };
    CHECK (has (1, 0x80, 60, 0));  // the song's held note
    CHECK (has (1, 0xb0, 64, 0));
    CHECK (has (1, 0xb0, 120, 0));
    CHECK (has (1, 0xb0, 123, 0));
    CHECK (has (3, 0x84, 64, 0));  // the live key
    CHECK_FALSE (e.isPlaying());
}

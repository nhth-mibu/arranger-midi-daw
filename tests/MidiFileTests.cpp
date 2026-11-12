#include "arranger/core/MidiFile.h"
#include "arranger/core/SongEdit.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace arranger;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace
{
    Song exampleSong()
    {
        Song s;
        s.title = "Export Test";
        s.tempo.setPoints ({ { 0, 100.0 }, { 3840 * 2, 140.0 } });
        s.timeSignature = { 3, 4 };

        auto& piano = edit::addTrack (s, "Piano");
        piano.output.channel = 1;
        piano.sound = { "Grand", 0, 0, 0 };
        const auto pianoId = piano.id;
        auto& drums = edit::addTrack (s, "Drums");
        drums.output.channel = 10;
        const auto drumsId = drums.id;

        Clip c;
        c.start = 2880;
        c.length = 2880;
        c.notes = { { 0, 480, 60, 100 }, { 0, 480, 64, 90 }, { 960, 2000, 67, 80 } }; // last note runs past the clip end
        c.controls = { { 100, ControlEvent::Kind::controller, 64, 127 }, { 200, ControlEvent::Kind::pitchBend, 0, 4096 } };
        edit::addClip (s, pianoId, c);

        Clip d;
        d.length = 2880;
        d.notes = { { 0, 120, 36, 120 }, { 960, 120, 38, 110 } };
        edit::addClip (s, drumsId, d);
        return s;
    }
}

TEST_CASE ("Export then import keeps notes, tempo and sounds")
{
    const auto original = exampleSong();
    const auto bytes = midi_file::write (original);
    const auto back = midi_file::read (bytes);

    CHECK (back.title == "Export Test");
    CHECK (back.timeSignature == TimeSignature { 3, 4 });
    REQUIRE (back.tempo.getPoints().size() == 2);
    CHECK_THAT (back.tempo.getPoints()[1].bpm, WithinAbs (140.0, 0.01));
    CHECK (back.tempo.getPoints()[1].at == 7680);

    REQUIRE (back.tracks.size() == 2);
    const auto& piano = back.tracks[0];
    CHECK (piano.name == "Piano");
    CHECK (piano.output.channel == 1);
    CHECK (piano.sound.program == 0);
    CHECK (piano.sound.bankMsb == 0);
    REQUIRE (piano.clips.size() == 1);
    const auto& notes = piano.clips[0]->notes;
    REQUIRE (notes.size() == 3);
    CHECK (notes[0].start == 2880); // imported clips start at 0, notes at their song position
    CHECK (notes[0].pitch == 60);
    CHECK (notes[2].length == 2880 - 960); // trimmed at the original clip end
    CHECK (piano.clips[0]->controls.size() == 2);
    CHECK (piano.clips[0]->controls[1].value == 4096);

    const auto& drums = back.tracks[1];
    CHECK (drums.output.channel == 10);
    CHECK (drums.colour == "coral");
    CHECK (drums.clips[0]->notes.size() == 2);
}

TEST_CASE ("Format 0 files split by channel, at any resolution")
{
    // One track, 96 ticks per quarter, running status, note-on velocity 0 as note-off.
    const std::vector<std::uint8_t> file = {
        'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96,
        'M', 'T', 'r', 'k', 0, 0, 0, 29,
        0x00, 0xc0, 5,             // program 6 on channel 1
        0x00, 0x90, 60, 100,       // ch1 note on
        0x00, 0x99, 36, 110,       // ch10 note on
        0x30, 0x89, 36, 0,         // 48 ticks later: ch10 note off
        0x30, 0x90, 60, 0,         // 48 later: ch1 note off (velocity 0)
        0x00, 62, 90,              // running status: ch1 note on
        0x60, 62, 0,               // 96 later: off
        0x00, 0xff, 0x2f, 0x00,
    };
    const auto song = midi_file::read (file);
    REQUIRE (song.tracks.size() == 2);
    const auto& ch1 = song.tracks[0];
    CHECK (ch1.sound.program == 5);
    REQUIRE (ch1.clips[0]->notes.size() == 2);
    CHECK (ch1.clips[0]->notes[0].length == 960);   // 96 file ticks = one quarter
    CHECK (ch1.clips[0]->notes[1].start == 960);
    CHECK (song.tracks[1].output.channel == 10);
    CHECK (song.tracks[1].clips[0]->notes[0].length == 480);
}

TEST_CASE ("Broken MIDI files are refused clearly")
{
    using midi_file::Error;
    CHECK_THROWS_WITH (midi_file::read (std::vector<std::uint8_t> { 'n', 'o', 'p', 'e' }), ContainsSubstring ("Not a MIDI file"));
    const std::vector<std::uint8_t> truncated = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 1, 0, 96, 'M', 'T', 'r', 'k', 0, 0, 0, 50, 0, 0x90 };
    CHECK_THROWS_AS (midi_file::read (truncated), Error);
    const std::vector<std::uint8_t> smpte = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0xe7, 0x28 };
    CHECK_THROWS_WITH (midi_file::read (smpte), ContainsSubstring ("SMPTE"));
}

#include "arranger/core/ChordInput.h"
#include "arranger/core/SongEdit.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace arranger;
using namespace arranger::chord_input;

namespace
{
    constexpr Tick kBeat = kTicksPerQuarter;
    constexpr Tick kBar = kBeat * 4;

    theory::Key key (const char* text) { return *theory::parseKey (text); }
    Chord chord (const char* text) { return *style_chord::parse (text); }

    std::vector<std::string> texts (const std::vector<ScoreMark>& marks)
    {
        std::vector<std::string> out;
        for (const auto& m : marks)
            out.push_back (m.text);
        return out;
    }

    Track midiTrack (TrackId id, const char* name, std::vector<Note> notes, int channel = 1)
    {
        Track t;
        t.id = id;
        t.name = name;
        t.output.channel = channel;
        Clip c;
        c.id = id * 100;
        c.start = 0;
        c.length = kBar * 8;
        c.notes = std::move (notes);
        std::sort (c.notes.begin(), c.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
        t.clips.push_back (std::make_shared<const Clip> (std::move (c)));
        return t;
    }

    void block (std::vector<Note>& notes, Tick at, std::initializer_list<int> pitches, Tick length = kBar)
    {
        for (const int p : pitches)
            notes.push_back ({ at, length, static_cast<std::uint8_t> (p), 90 });
    }

    bool has (const std::vector<Suggestion>& list, const char* name)
    {
        return std::any_of (list.begin(), list.end(), [&] (const Suggestion& s) { return s.name == name; });
    }
}

TEST_CASE ("Typed chords: one a bar without bar lines, shared bars with them", "[chordinput]")
{
    const auto k = key ("C major");
    const TimeSignature ts;
    auto t = typeChords ("Am F C G", k, ts, kBar * 2);
    REQUIRE (texts (t.marks) == std::vector<std::string> { "Am", "F", "C", "G" });
    CHECK (t.marks[0].at == kBar * 2);
    CHECK (t.marks[3].at == kBar * 5);
    CHECK (t.bars == 4);
    CHECK (t.end == kBar * 6);

    t = typeChords ("Am F | C G |", k, ts, 0);
    REQUIRE (t.marks.size() == 4);
    CHECK (t.marks[1].at == kBeat * 2);
    CHECK (t.marks[2].at == kBar);
    CHECK (t.bars == 2);

    // Roman numerals, holds and repeats.
    t = typeChords ("I / / V | % | vi IV", k, ts, 0);
    REQUIRE (texts (t.marks) == std::vector<std::string> { "C", "G", "C", "G", "Am", "F" });
    CHECK (t.marks[1].at == kBeat * 3);
    CHECK (t.marks[2].at == kBar);
    CHECK (t.bars == 3);

    // Without bar lines, a "/" holds the chord for another bar.
    t = typeChords ("C / F", k, ts, 0);
    REQUIRE (texts (t.marks) == std::vector<std::string> { "C", "F" });
    CHECK (t.marks[1].at == kBar * 2);
    CHECK (t.bars == 3);

    t = typeChords ("C xyz G", k, ts, 0);
    CHECK (t.unknown == std::vector<std::string> { "xyz" });

    // Spelled in the key.
    t = typeChords ("IV V7/V", key ("Eb major"), ts, 0);
    CHECK (texts (t.marks) == std::vector<std::string> { "Ab", "F7" });
}

TEST_CASE ("The chord lane is one track's chord symbols", "[chordinput]")
{
    Song s;
    s.tracks.push_back (midiTrack (1, "Drums", {}, 10));
    s.tracks.push_back (midiTrack (2, "Piano", {}));
    s.tracks.push_back (midiTrack (3, "Bass", {}));
    CHECK (chordTrack (s) == 2); // not the drums

    s.tracks[2].marks.push_back ({ ScoreMark::Kind::chord, 0, "C" });
    CHECK (chordTrack (s) == 3); // the track that has chords

    const auto typed = typeChords ("F G", key ("C major"), s.timeSignature, kBar);
    writeChords (s, kBar, typed.end, typed.marks);
    const auto lane = laneChords (s);
    REQUIRE (lane.size() == 3);
    CHECK (lane[0].end == kBar);
    CHECK (lane[1].text == "F");
    REQUIRE (chordAt (lane, kBar * 2 + 10) != nullptr);
    CHECK (chordAt (lane, kBar * 2 + 10)->text == "G");
}

TEST_CASE ("Next chord suggestions follow the key", "[chordinput]")
{
    const auto c = key ("C major");
    auto s = suggestNext ({}, c);
    REQUIRE (! s.empty());
    CHECK (s.front().name == "C");

    s = suggestNext ({ chord ("G") }, c);
    REQUIRE (! s.empty());
    CHECK (s.front().name == "C");
    CHECK (s.front().why.find ("home") != std::string::npos);
    CHECK (has (s, "Am"));

    // Dm then G: home to C, as in ii-V-I.
    s = suggestNext ({ chord ("Dm"), chord ("G") }, c);
    CHECK (s.front().name == "C");

    // C G Am: F comes next, as in I-V-vi-IV.
    s = suggestNext ({ chord ("C"), chord ("G"), chord ("Am") }, c);
    CHECK (s.front().name == "F");
    CHECK (s.front().why.find ("progression") != std::string::npos);

    // A secondary dominant resolves.
    s = suggestNext ({ chord ("E7") }, c);
    CHECK (s.front().name == "Am");

    // Seventh chords before: seventh chords after.
    s = suggestNext ({ chord ("Dm7"), chord ("G7") }, c);
    CHECK (s.front().name == theory::chordName (chord ("Cmaj7"), c));

    // Never the same chord again.
    s = suggestNext ({ chord ("F") }, c);
    CHECK (! has (s, "F"));

    // Minor keys.
    s = suggestNext ({ chord ("E7") }, key ("A minor"));
    CHECK (s.front().name == "Am");
}

TEST_CASE ("The palette has sixteen chords in three groups", "[chordinput]")
{
    const auto p = palette (key ("C major"));
    REQUIRE (p.size() == 16);
    CHECK (p[0].name == "C");
    CHECK (p[4].name == "G");
    CHECK (p[6].roman == "vii°");
    CHECK (p[7].name == "G7");
    CHECK (p[8].name == "Fm");
    CHECK (p[10].name == "Bb");
    CHECK (p[14].name == "D7");
    CHECK (p[0].group == "diatonic");
    CHECK (p[9].group == "borrowed");
    CHECK (p[15].group == "secondary");

    const auto m = palette (key ("A minor"));
    REQUIRE (m.size() == 16);
    CHECK (m[0].name == "Am");
    CHECK (m[7].name == "E7");
    CHECK (m[9].name == "Bb");
}

TEST_CASE ("Voicings", "[chordinput]")
{
    VoiceOptions o;
    o.bass = false;
    auto v = voice (chord ("C"), o);
    CHECK (v == std::vector<int> { 60, 64, 67 });

    o.inversion = 1;
    v = voice (chord ("C"), o);
    CHECK (v == std::vector<int> { 64, 67, 72 });

    o.inversion = 0;
    o.voicing = Voicing::open;
    v = voice (chord ("C"), o);
    CHECK (v == std::vector<int> { 60, 67, 76 });

    o.voicing = Voicing::drop2;
    v = voice (chord ("Cmaj7"), o);
    REQUIRE (v.size() == 4);
    CHECK (v[0] % 12 == 7); // the G dropped below

    // The bass: the slash note under the chord.
    o = {};
    v = voice (chord ("C/E"), o);
    CHECK (v.front() % 12 == 4);
    CHECK (v[1] - v.front() >= 5);

    // Smooth: F after C keeps the common C and moves little.
    o = {};
    o.bass = false;
    o.voicing = Voicing::smooth;
    v = voice (chord ("F"), o, { 60, 64, 67 });
    CHECK (v == std::vector<int> { 60, 65, 69 });
}

TEST_CASE ("Chords played on a keyboard", "[chordinput]")
{
    ChordListener l;
    CHECK (! l.noteOn (60));
    CHECK (! l.noteOn (64));
    auto c = l.noteOn (67);
    REQUIRE (c.has_value());
    CHECK (style_chord::name (*c) == "C");
    l.noteOff (60);
    l.noteOff (64);
    l.noteOff (67);
    l.noteOn (57);
    l.noteOn (60);
    c = l.noteOn (64);
    REQUIRE (c.has_value());
    CHECK (style_chord::name (*c) == "Am");

    // Quantised to the nearest bar; the last chord on a line wins; repeats go.
    const auto k = key ("C major");
    std::vector<ChordHit> hits { { kBar * 0 + 30, chord ("C") },
                                 { kBar - 100, chord ("F") },   // early for bar 2
                                 { kBar + 80, chord ("Dm") },   // a quick correction: Dm wins
                                 { kBar * 2 + 20, chord ("Dm") }, // the same again
                                 { kBar * 3 - 10, chord ("G") } };
    const auto marks = quantiseHits (hits, kBar, k);
    REQUIRE (texts (marks) == std::vector<std::string> { "C", "Dm", "G" });
    CHECK (marks[1].at == kBar);
    CHECK (marks[2].at == kBar * 3);
}

TEST_CASE ("Reharmonizing alternatives", "[chordinput]")
{
    const auto c = key ("C major");
    auto alts = alternatives (chord ("G7"), c, chord ("C"));
    REQUIRE (! alts.empty());
    CHECK (alts.front().chords.front() == chord ("Db7"));
    CHECK (alts.front().label.find ("Tritone") != std::string::npos);

    alts = alternatives (chord ("F"), c, chord ("G"), 9);
    const auto find = [&] (const char* word) {
        return std::find_if (alts.begin(), alts.end(), [&] (const Alternative& a) { return a.label.find (word) != std::string::npos; });
    };
    REQUIRE (find ("Borrowed") != alts.end());
    CHECK (find ("Borrowed")->chords.front() == chord ("Fm"));
    REQUIRE (find ("Lead into") != alts.end());
    CHECK (find ("Lead into")->chords == std::vector<Chord> { chord ("F"), chord ("D7") });
    REQUIRE (find ("Passing") != alts.end());
    CHECK (find ("Passing")->chords.back() == chord ("F#dim7"));
    REQUIRE (find ("Relative") != alts.end());
    CHECK (find ("Relative")->chords.front() == chord ("Dm"));
    for (const auto& a : alts)
        CHECK (! (a.chords.size() == 1 && a.chords.front() == chord ("F")));
}

TEST_CASE ("Notes move to the new chord by their job", "[chordinput]")
{
    const auto k = key ("C major");
    // C to Am: the third E goes to the third C, the fifth G to the fifth E.
    CHECK (moveNote (64, chord ("C"), chord ("Am"), k, false) == 60);
    CHECK (moveNote (67, chord ("C"), chord ("Am"), k, false) == 64);
    CHECK (moveNote (60, chord ("C"), chord ("Am"), k, false) == 57);
    // C to Cm: only the third moves.
    CHECK (moveNote (64, chord ("C"), chord ("Cm"), k, false) == 63);
    CHECK (moveNote (67, chord ("C"), chord ("Cm"), k, false) == 67);
    // A bass part's root goes to the slash bass.
    CHECK (moveNote (48, chord ("C"), chord ("C/E"), k, true) == 52);
    // A passing D in the key stays over F (no rub); a B goes to a chord note.
    CHECK (moveNote (62, chord ("C"), chord ("F"), k, false) == 62);
    CHECK (moveNote (71, chord ("C"), chord ("F"), k, false) % 12 != 11);
}

TEST_CASE ("Reharmonize makes a proposal that moves the parts", "[chordinput]")
{
    Song s;
    s.key = "C major";
    std::vector<Note> piano, bass, melody;
    block (piano, 0, { 60, 64, 67 });
    block (piano, kBar, { 60, 65, 69 });
    block (bass, 0, { 36 });
    block (bass, kBar, { 41 });
    for (int i = 0; i < 8; ++i)
        melody.push_back ({ kBeat * i, kBeat, static_cast<std::uint8_t> (72 + (i % 3) * 2), 90 });
    s.tracks.push_back (midiTrack (1, "Piano", piano));
    s.tracks.push_back (midiTrack (2, "Bass", bass));
    s.tracks.push_back (midiTrack (3, "Melody", melody));
    s.tracks[0].marks = { { ScoreMark::Kind::chord, 0, "C" }, { ScoreMark::Kind::chord, kBar, "F" } };

    const auto p = reharmonize (s, kBar / 2);
    CHECK (p.from == 0);
    CHECK (p.to == kBar);
    REQUIRE (p.options.size() >= 3);
    // The melody never changes.
    for (const auto& id : p.tracks)
        CHECK (id != 3);

    // The relative minor: Am in place of C.
    const auto it = std::find_if (p.options.begin(), p.options.end(), [] (const arranging::Option& o) { return o.label.find ("Relative") != std::string::npos; });
    REQUIRE (it != p.options.end());
    const auto after = arranging::withOption (s, *it);
    const auto lane = laneChords (after);
    REQUIRE (! lane.empty());
    CHECK (lane.front().text == "Am");
    CHECK (lane[1].text == "F");
    const auto pianoNotes = arranging::playedNotes (after.tracks[0], 0, kBar);
    std::vector<int> pitches;
    for (const auto& n : pianoNotes)
        pitches.push_back (n.pitch);
    std::sort (pitches.begin(), pitches.end());
    CHECK (pitches == std::vector<int> { 57, 60, 64 });
    CHECK (arranging::playedNotes (after.tracks[1], 0, kBar).front().pitch == 33);
    // Bar 2 stays.
    CHECK (arranging::playedNotes (after.tracks[0], kBar, kBar * 2).size() == 3);
    CHECK (arranging::playedNotes (after.tracks[0], kBar, kBar * 2).front().pitch == 60);

    // Taking it is one edit that keeps the melody.
    Song done = s;
    arranging::apply (done, p, static_cast<int> (it - p.options.begin()));
    CHECK (done.tracks[2] == s.tracks[2]);

    // No chord: no options.
    Song empty = s;
    empty.tracks[0].marks.clear();
    CHECK (reharmonize (empty, 0).options.empty());
}

#include "arranger/core/StyleFromAudio.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <random>
#include <set>

using namespace arranger;
using namespace arranger::style_from_audio;

namespace
{
    /**
        A made-up analysed song at 100 BPM, bar 1 at 0.37 s: intro (4 bars, hi-hat),
        verse (8 bars: kick on 1 and 3, bass on the root), chorus (8 bars: four on
        the floor, 16th hats, 8th bass), verse, chorus, ending (4 bars).
        Snare fills in the last bar of each verse and chorus. Notes are a little
        early or late, and the "other" stem has weak stray notes, as Basic Pitch gives.
    */
    analysis::Result song (double bpm = 100.0, double first = 0.37)
    {
        analysis::Result r;
        r.source = "/music/Test Song.wav";
        const double beat = 60.0 / bpm;
        std::mt19937 random (7);
        std::uniform_real_distribution<double> jitter (-0.012, 0.012);
        const auto at = [&] (int bar, double beatInBar) { return first + (bar * 4 + beatInBar) * beat + jitter (random); };

        analysis::Stem drums { "drums", "drums.wav", false, {}, {} };
        analysis::Stem bass { "bass", "bass.wav", true, {}, { { "Bass guitar", 0.9f } } };
        analysis::Stem other { "other", "other.wav", true, {}, { { "Piano", 0.8f } } };

        enum Part { intro, verse, chorus, ending };
        std::vector<Part> parts;
        for (int i = 0; i < 4; ++i) parts.push_back (intro);
        for (int k = 0; k < 2; ++k)
        {
            for (int i = 0; i < 8; ++i) parts.push_back (verse);
            for (int i = 0; i < 8; ++i) parts.push_back (chorus);
        }
        for (int i = 0; i < 4; ++i) parts.push_back (ending);

        const std::vector<Chord> verseChords { { 9, ChordType::minor }, { 5, ChordType::major }, { 0, ChordType::major }, { 7, ChordType::major } };
        const std::vector<Chord> chorusChords { { 5, ChordType::major }, { 7, ChordType::major }, { 0, ChordType::major }, { 9, ChordType::minor } };

        for (int bar = 0; bar < static_cast<int> (parts.size()); ++bar)
        {
            const auto part = parts[static_cast<std::size_t> (bar)];
            const auto& chords = part == chorus ? chorusChords : verseChords;
            const auto chord = chords[static_cast<std::size_t> (bar % 4)];
            r.chords.push_back ({ first + bar * 4 * beat, first + (bar + 1) * 4 * beat, chord });

            const bool fill = (part == verse || part == chorus) && bar % 8 == 3;
            // Drums
            if (part == intro)
                for (int e = 0; e < 8; ++e)
                    drums.notes.push_back ({ at (bar, e * 0.5), 0.05, 42, 70 });
            else if (fill)
            {
                drums.notes.push_back ({ at (bar, 0), 0.1, 36, 110 });
                for (int s = 4; s < 16; ++s)
                    drums.notes.push_back ({ at (bar, s * 0.25), 0.05, 38, 80 + s });
            }
            else if (part == verse || part == ending)
            {
                for (const double b : { 0.0, 2.0 })
                    drums.notes.push_back ({ at (bar, b), 0.1, 36, 110 });
                for (const double b : { 1.0, 3.0 })
                    drums.notes.push_back ({ at (bar, b), 0.1, 38, 100 });
                for (int e = 0; e < 8; ++e)
                    drums.notes.push_back ({ at (bar, e * 0.5), 0.05, 42, 75 });
            }
            else
            {
                for (int b = 0; b < 4; ++b)
                    drums.notes.push_back ({ at (bar, b), 0.1, 36, 115 });
                for (const double b : { 1.0, 3.0 })
                    drums.notes.push_back ({ at (bar, b), 0.1, 38, 110 });
                for (int s = 0; s < 16; ++s)
                    drums.notes.push_back ({ at (bar, s * 0.25), 0.05, 42, s % 2 == 0 ? 85 : 60 });
            }
            // Bass: the root.
            const int root = 36 + chord.root;
            if (part == verse || part == ending)
                for (const double b : { 0.0, 2.0, 3.5 })
                    bass.notes.push_back ({ at (bar, b), b == 3.5 ? 0.25 : 0.9, root, 100 });
            else if (part == chorus)
                for (int e = 0; e < 8; ++e)
                    bass.notes.push_back ({ at (bar, e * 0.5), 0.25, root + (e == 7 ? 7 : 0), 100 });
            // Piano: the chord on every beat (whole notes in the intro).
            const auto tones = style_chord::tones (chord.type);
            if (part == intro)
                for (const int t : tones)
                    other.notes.push_back ({ at (bar, 0), 2.2, 60 + chord.root + t > 72 ? 48 + chord.root + t : 60 + chord.root + t, 70 });
            else
                for (int b = 0; b < 4; ++b)
                    for (const int t : tones)
                    {
                        int p = 60 + chord.root + t;
                        if (p > 72)
                            p -= 12;
                        other.notes.push_back ({ at (bar, b), 0.4, p, part == chorus ? 95 : 80 });
                    }
            // Stray notes.
            other.notes.push_back ({ at (bar, 1.75), 0.03, 61 + bar % 5, 12 });
        }
        for (auto* s : { &drums, &bass, &other })
            std::sort (s->notes.begin(), s->notes.end(), [] (const auto& a, const auto& b) { return a.start < b.start; });
        r.stems = { drums, bass, other };
        return r;
    }
}

TEST_CASE ("Style from Audio moves notes to the CMaj7 source chord", "[stylefromaudio]")
{
    const Chord am { 9, ChordType::minor };
    CHECK (toSourceChord (57, am) == 60); // A -> C
    CHECK (toSourceChord (60, am) == 64); // C (the minor 3rd) -> E
    CHECK (toSourceChord (64, am) == 67); // E -> G
    const Chord g7 { 7, ChordType::seventh };
    CHECK (toSourceChord (67, g7) == 72);
    CHECK (toSourceChord (65, g7) == 71); // F (the minor 7th) -> B
    const Chord cOverE { 0, ChordType::major, 4 };
    CHECK (toSourceChord (40, cOverE, true) == 36); // the bass plays the bass note: it becomes C
    CHECK (toSourceChord (60, Chord {}) == 60);
}

TEST_CASE ("Style from Audio finds the tempo and bar 1", "[stylefromaudio]")
{
    const auto r = song();
    const auto g = findGrid (onsets (r));
    CHECK (g.bpm == Catch::Approx (100.0).margin (0.5));
    CHECK (g.firstDownbeat == Catch::Approx (0.37).margin (0.03));
    CHECK (g.confidence > 0.8);

    for (const auto& [bpm, first] : { std::pair { 128.0, 1.1 }, std::pair { 84.0, 0.05 }, std::pair { 140.0, 2.3 } })
    {
        const auto other = findGrid (onsets (song (bpm, first)));
        CHECK (other.bpm == Catch::Approx (bpm).margin (0.5));
        CHECK (other.firstDownbeat == Catch::Approx (first).margin (0.03));
    }

    const auto known = findGrid (onsets (r), 4, 0.0, 100.0);
    CHECK (known.bpm == 100.0);
    CHECK (known.firstDownbeat == Catch::Approx (0.37).margin (0.03));
}

TEST_CASE ("Style from Audio maps stems to style parts", "[stylefromaudio]")
{
    const auto parts = partsFor (song());
    REQUIRE (parts.size() == 3);
    CHECK (parts[0].stem == "drums");
    CHECK (parts[0].channel == 10);
    CHECK (parts[0].drums);
    CHECK (parts[1].stem == "bass");
    CHECK (parts[1].channel == 11);
    CHECK (parts[2].stem == "other");
    CHECK (parts[2].channel == 12);
}

TEST_CASE ("Style from Audio makes sections, refines and writes a style", "[stylefromaudio]")
{
    const auto r = song();
    Options o;
    int calls = 0;
    o.progress = [&] (double, const std::string&) { ++calls; return true; };
    const auto made = make (r, o);
    CHECK (calls > 3);

    CHECK (made.style.name == "Test Song");
    CHECK (made.style.tempo == Catch::Approx (100.0));
    CHECK (made.plan.bars == 40);
    CHECK (made.plan.phraseBars == 4);

    // Sections where the song has them.
    const auto& at = made.plan.barSection;
    CHECK (at[0] == "Intro A");
    CHECK (at[3] == "Intro A");
    CHECK (at[4] == "Main A");   // verse: the quieter one
    CHECK (at[12] == "Main B");  // chorus
    CHECK (at[11] == "Fill In BB"); // the fill into the chorus
    CHECK (at[19] == "Fill In AA"); // the fill back into the verse
    CHECK (at[39] == "Ending A");
    CHECK (barsOf (made.plan, "Intro A") == "1-4");

    std::set<std::string> names;
    for (const auto& s : made.style.sections)
        names.insert (s.name);
    for (const char* n : { "Intro A", "Main A", "Main B", "Fill In AA", "Fill In BB", "Ending A" })
        CHECK (names.count (n) == 1);

    // The refine loop never makes it worse, and the result is close to the song.
    CHECK (made.best.total >= made.first.total);
    CHECK (made.best.total > 0.8);
    CHECK (made.tried > 10);

    // The bass of Main A plays C (the root) only: it was moved from Am, F, C and G.
    const auto* mainA = made.style.section ("Main A");
    REQUIRE (mainA != nullptr);
    REQUIRE (mainA->parts.count (11) == 1);
    for (const auto& n : mainA->parts.at (11).notes)
        CHECK (n.pitch % 12 == 0);
    // The piano plays the notes of C major only.
    REQUIRE (mainA->parts.count (12) == 1);
    for (const auto& n : mainA->parts.at (12).notes)
        CHECK ((n.pitch % 12 == 0 || n.pitch % 12 == 4 || n.pitch % 12 == 7));
    // The kick of the verse is on 1 and 3.
    int kicks = 0;
    for (const auto& n : mainA->parts.at (10).notes)
        if (n.pitch == 36)
        {
            ++kicks;
            const auto off = n.start % (kTicksPerQuarter * 2);
            CHECK (std::min (off, kTicksPerQuarter * 2 - off) < 60);
        }
    CHECK (kicks >= 2);

    // The chorus pattern has more notes than the verse.
    const auto* mainB = made.style.section ("Main B");
    REQUIRE (mainB != nullptr);
    CHECK (mainB->parts.at (10).notes.size() * static_cast<std::size_t> (mainA->length) > mainA->parts.at (10).notes.size() * static_cast<std::size_t> (mainB->length));

    // A fresh compare gives the same score as the loop's last one.
    const auto again = compare (made.style, r, made.grid, made.plan, made.parts);
    CHECK (again.total == Catch::Approx (made.best.total).margin (0.02));

    // The style file reads back as it was written.
    const auto bytes = style_file::write (made.style);
    const auto back = style_file::read (bytes);
    CHECK (back.sections.size() == made.style.sections.size());
    REQUIRE (back.section ("Main B") != nullptr);
    CHECK (back.section ("Main B")->parts.size() == mainB->parts.size());
}

TEST_CASE ("Style from Audio stops when asked and keeps the best so far", "[stylefromaudio]")
{
    Options o;
    int calls = 0;
    o.progress = [&] (double, const std::string&) { return ++calls < 4; };
    const auto made = make (song(), o);
    CHECK (! made.style.sections.empty());
    CHECK (made.best.total >= made.first.total);
}

TEST_CASE ("Style from Audio previews the recording against the style", "[stylefromaudio]")
{
    Options o;
    o.maxRounds = 1;
    const auto made = make (song(), o);

    const auto both = preview (made, Listen::both);
    CHECK (both.baseTempo() == Catch::Approx (made.grid.bpm));
    REQUIRE (both.tracks.size() == 4);
    CHECK (both.tracks[0].output.kind == TrackOutput::Kind::audio);
    REQUIRE (both.tracks[0].audio.size() == 1);
    CHECK (both.tracks[0].audio[0]->offset == Catch::Approx (made.grid.firstDownbeat));
    CHECK (both.tracks[1].output.channel == 10);
    CHECK (! both.tracks[2].clips.empty());
    CHECK (! both.tracks[1].marks.empty()); // chord symbols

    // The bass plays A in bar 5 (Am): the pattern in C moved back.
    const auto& bassClip = *both.tracks[2].clips.front();
    bool sawA = false;
    for (const auto& n : bassClip.notes)
        if (n.start >= kTicksPerQuarter * 16 && n.start < kTicksPerQuarter * 17)
            sawA = sawA || n.pitch % 12 == 9;
    CHECK (sawA);

    CHECK (preview (made, Listen::original).tracks.size() == 1);
    CHECK (preview (made, Listen::style).tracks.size() == 3);
}

TEST_CASE ("Style from Audio needs notes", "[stylefromaudio]")
{
    analysis::Result empty;
    CHECK_THROWS (make (empty));
}


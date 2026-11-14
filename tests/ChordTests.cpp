#include "arranger/core/ChordFinder.h"
#include "arranger/core/SongEdit.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace arranger;

namespace
{
    constexpr Tick kBeat = kTicksPerQuarter;
    constexpr Tick kBar = kBeat * 4;

    void addChord (Clip& c, Tick at, Tick length, std::initializer_list<int> pitches)
    {
        for (const int p : pitches)
            c.notes.push_back ({ at, length, static_cast<std::uint8_t> (p), 90 });
    }
}

TEST_CASE ("Chords are found from the notes of tracks", "[chords]")
{
    Song song;
    const auto pianoId = edit::addTrack (song, "Piano").id;
    Clip c;
    c.length = kBar * 4;
    addChord (c, 0, kBar, { 60, 64, 67 });                 // C
    addChord (c, kBar, kBar, { 57, 60, 64 });              // Am
    addChord (c, kBar * 2, kBar / 2, { 53, 57, 60, 64 });  // Fmaj7
    addChord (c, kBar * 2 + kBar / 2, kBar / 2, { 55, 59, 62, 65 }); // G7
    addChord (c, kBar * 3, kBar, { 64, 67, 72 });          // C/E
    edit::addClip (song, pianoId, c);
    // A drum track must not change the chords.
    auto& drums = edit::addTrack (song, "Drums");
    drums.output.channel = 10;
    const auto drumsId = drums.id;
    Clip d;
    d.length = kBar * 4;
    addChord (d, 0, kBeat, { 37, 42, 49 });
    edit::addClip (song, drumsId, d);

    const std::vector<const Track*> tracks { edit::findTrack (song, pianoId), edit::findTrack (song, drumsId) };

    SECTION ("half bars")
    {
        auto marks = chords::find (tracks, song.timeSignature, 0, kBar * 4, kBar / 2);
        REQUIRE (marks.size() == 5);
        CHECK (marks[0].text == "C");
        CHECK (marks[1].text == "Am");
        CHECK (marks[1].at == kBar);
        CHECK (marks[2].text == "FM7");
        CHECK (marks[3].text == "G7");
        CHECK (marks[3].at == kBar * 2 + kBar / 2);
        CHECK (marks[4].text == "C/E");
    }
    SECTION ("whole bars give one chord a bar")
    {
        auto marks = chords::find (tracks, song.timeSignature, 0, kBar * 4, kBar);
        CHECK (marks.size() == 4);
    }
    SECTION ("replacing keeps the other marks")
    {
        auto& t = *edit::findTrack (song, pianoId);
        t.marks = { { ScoreMark::Kind::chord, 0, "D" }, { ScoreMark::Kind::lyric, 0, "la" }, { ScoreMark::Kind::chord, kBar * 9, "E" } };
        chords::replaceChords (t, 0, kBar * 4, chords::find (tracks, song.timeSignature, 0, kBar * 4, kBar / 2));
        CHECK (t.marks.size() == 7);
        CHECK (t.marks.back().text == "E");
    }
}

TEST_CASE ("A melody's passing notes do not change the chord", "[chords]")
{
    Song song;
    const auto pianoId = edit::addTrack (song, "Piano").id;
    const auto voiceId = edit::addTrack (song, "Voice").id;
    Clip c;
    c.length = kBar;
    addChord (c, 0, kBar, { 48, 64, 67 }); // C
    edit::addClip (song, pianoId, c);
    Clip m;
    m.length = kBar;
    for (int i = 0; i < 4; ++i)
        addChord (m, kBeat * i, kBeat, { i % 2 == 0 ? 72 : 74 }); // C D C D
    edit::addClip (song, voiceId, m);
    const std::vector<const Track*> tracks { edit::findTrack (song, pianoId), edit::findTrack (song, voiceId) };
    auto marks = chords::find (tracks, song.timeSignature, 0, kBar, kBar);
    REQUIRE (marks.size() == 1);
    CHECK (marks[0].text == "C");
}

TEST_CASE ("Every guitar shape plays the notes of its chord", "[chords]")
{
    int shapes = 0;
    for (int root = 0; root < 12; ++root)
        for (int type = 0; type < kChordTypeCount; ++type)
        {
            const Chord chord { root, static_cast<ChordType> (type), -1 };
            if (chord.type == ChordType::cancel)
            {
                CHECK_FALSE (guitar::shape (chord).has_value());
                continue;
            }
            const auto s = guitar::shape (chord);
            REQUIRE (s.has_value());
            ++shapes;
            std::set<int> allowed;
            for (const int t : style_chord::tones (chord.type))
                allowed.insert ((root + t) % 12);
            const auto played = guitar::notes (*s);
            REQUIRE (played.size() >= 2);
            CHECK (std::find (played.begin(), played.end(), root) != played.end());
            for (const int n : played)
            {
                INFO (style_chord::name (chord) << " plays note " << n);
                // Types without a shape fall back to a plainer chord; its notes are all in the full chord.
                CHECK (allowed.count (n) == 1);
            }
            for (const int f : s->frets)
                CHECK (f <= 15);
        }
    CHECK (shapes == 12 * (kChordTypeCount - 1));
}

TEST_CASE ("Usual chords get their usual shapes", "[chords]")
{
    const auto frets = [] (const char* name) { return guitar::shape (*style_chord::parse (name))->frets; };
    constexpr int x = guitar::kMuted;
    CHECK (frets ("C") == std::array<int, 6> { x, 3, 2, 0, 1, 0 });
    CHECK (frets ("G") == std::array<int, 6> { 3, 2, 0, 0, 0, 3 });
    CHECK (frets ("Am") == std::array<int, 6> { x, 0, 2, 2, 1, 0 });
    CHECK (frets ("E") == std::array<int, 6> { 0, 2, 2, 1, 0, 0 });
    CHECK (frets ("F") == std::array<int, 6> { 1, 3, 3, 2, 1, 1 });
    const auto bm = guitar::shape (*style_chord::parse ("Bbm"));
    CHECK (bm->frets == std::array<int, 6> { x, 1, 3, 3, 2, 1 });
    const auto high = guitar::shape (*style_chord::parse ("C#m7"));
    CHECK (high->firstFret == 4);
}

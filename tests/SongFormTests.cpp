#include "arranger/core/ProjectFile.h"
#include "arranger/core/SongEdit.h"
#include "arranger/core/SongForm.h"
#include "arranger/core/Arranging.h"

#include <catch2/catch_test_macros.hpp>

using namespace arranger;

namespace
{
    constexpr Tick kQ = kTicksPerQuarter;
    constexpr Tick kBar = 4 * kQ;

    /** Verse (4 bars, quiet) and chorus (4 bars, busy), twice, with drums, bass and chords. Bars 1-4 intro. */
    Song popSong()
    {
        Song s;
        auto& drums = edit::addTrack (s, "Drums");
        drums.output = { TrackOutput::Kind::soundFont, "", 10 };
        auto& bass = edit::addTrack (s, "Bass");
        bass.output.channel = 2;
        bass.sound.program = 33;
        auto& keys = edit::addTrack (s, "Piano");
        keys.output.channel = 3;
        const TrackId d = s.tracks[0].id, b = s.tracks[1].id, k = s.tracks[2].id;

        // intro (4), verse (4), chorus (4), verse (4), chorus (4)
        const char* plan[] = { "intro", "verse", "chorus", "verse", "chorus" };
        Clip dc, bc, kc;
        for (auto* c : { &dc, &bc, &kc })
            c->length = 20 * kBar;
        for (int part = 0; part < 5; ++part)
            for (int bar = 0; bar < 4; ++bar)
            {
                const Tick at = (part * 4 + bar) * kBar;
                const std::string what = plan[part];
                const bool chorus = what == "chorus";
                if (what != "intro")
                    for (int e = 0; e < (chorus ? 16 : 8); ++e)
                        dc.notes.push_back ({ at + e * (chorus ? kQ / 4 : kQ / 2), 60, 42, 90 });
                if (what != "intro")
                {
                    dc.notes.push_back ({ at, 60, 36, 100 });
                    dc.notes.push_back ({ at + kQ, 60, 38, 100 });
                    dc.notes.push_back ({ at + 3 * kQ, 60, 38, 100 });
                }
                const int root = chorus ? 41 : 36;
                for (int q = 0; q < 4; ++q)
                    bc.notes.push_back ({ at + q * kQ, kQ - 40, static_cast<std::uint8_t> (root), 100 });
                for (int p : { 60, 64, 67 })
                    kc.notes.push_back ({ at, kBar - 20, static_cast<std::uint8_t> (p + (chorus ? 5 : 0) + (what == "intro" ? 12 : 0)), 80 });
            }
        for (auto* c : { &dc, &bc, &kc })
            edit::normalise (*c);
        edit::addClip (s, d, dc);
        edit::addClip (s, b, bc);
        edit::addClip (s, k, kc);
        return s;
    }

    int notesBetween (const Song& s, std::size_t track, Tick from, Tick to)
    {
        return static_cast<int> (arranging::playedNotes (s.tracks[track], from, to).size());
    }
}

TEST_CASE ("Song forms: sections are found from the music")
{
    auto s = popSong();
    const auto found = song_form::findSections (s);
    REQUIRE (found.size() == 5);
    CHECK (found[0].name == "Intro");
    CHECK (found[1].name == "Verse 1");
    CHECK (found[2].name == "Chorus 1");
    CHECK (found[3].name == "Verse 2");
    CHECK (found[4].name == "Chorus 2");
    CHECK (found[2].start == 8 * kBar);
    CHECK (found[2].end == 12 * kBar);
}

TEST_CASE ("Song forms: a form from names, and the built song")
{
    auto s = popSong();
    song_form::findSections (s);
    std::vector<std::string> unknown;
    auto f = song_form::formFromNames (s, "Short", "Verse 1, Chorus 1 x2, Ending?", &unknown);
    REQUIRE (f.parts.size() == 3);
    CHECK (unknown == std::vector<std::string> { "Ending?" });
    CHECK (song_form::partsText (s, f) == "Verse 1, Chorus 1 x2");
    CHECK (f.transitions.size() == 2);

    const auto built = song_form::build (s, f);
    CHECK (built.contentEnd() == 12 * kBar);
    REQUIRE (built.sections.size() == 3);
    CHECK (built.sections[1].start == 4 * kBar);
    // The chorus plays twice: busy hi-hats in bars 5-12.
    CHECK (notesBetween (built, 0, 4 * kBar, 5 * kBar) == notesBetween (s, 0, 8 * kBar, 9 * kBar));
    CHECK (notesBetween (built, 0, 8 * kBar, 9 * kBar) == notesBetween (s, 0, 8 * kBar, 9 * kBar));
    // The source song is not changed.
    CHECK (s.contentEnd() == 20 * kBar);
}

TEST_CASE ("Song forms: new joins get a suggested transition that fits both sides")
{
    auto s = popSong();
    song_form::findSections (s);
    // Intro straight into the chorus (new, busier), then chorus into verse 2 (in the song already? no: chorus 1 -> verse 2 is), then verse 2 into intro (quieter).
    auto f = song_form::formFromNames (s, "Test", "Intro, Chorus 1, Verse 2, Intro");
    const auto joins = song_form::boundaries (s, f);
    REQUIRE (joins.size() == 3);
    CHECK (joins[0].isNew);
    CHECK (joins[0].suggested == Transition::Kind::fill);
    CHECK_FALSE (joins[1].isNew);
    CHECK (joins[1].suggested == Transition::Kind::none);
    CHECK (joins[2].isNew);
    CHECK (joins[2].suggested == Transition::Kind::hold);
    CHECK_FALSE (joins[0].because.empty());
}

TEST_CASE ("Song forms: the transitions change only the end of the part")
{
    auto s = popSong();
    song_form::findSections (s);
    auto f = song_form::formFromNames (s, "Test", "Verse 1, Chorus 1");
    const Tick join = 4 * kBar;
    const auto plain = song_form::build (s, f);

    f.transitions[0] = { Transition::Kind::fill, 2 };
    const auto filled = song_form::build (s, f);
    // Toms in the last two beats of the verse, a crash on the chorus.
    bool toms = false, crash = false;
    for (const auto& n : arranging::playedNotes (filled.tracks[0], join - 2 * kQ, join + 10))
    {
        toms = toms || (n.pitch >= 41 && n.pitch <= 50 && n.on < join);
        crash = crash || (n.pitch == 49 && n.on == join);
    }
    CHECK (toms);
    CHECK (crash);
    CHECK (notesBetween (filled, 0, 0, join - 2 * kQ) == notesBetween (plain, 0, 0, join - 2 * kQ));
    CHECK (notesBetween (filled, 1, 0, 2 * join) == notesBetween (plain, 1, 0, 2 * join)); // the bass is the same

    f.transitions[0] = { Transition::Kind::pickup, 1 };
    const auto pickup = song_form::build (s, f);
    const auto run = arranging::playedNotes (pickup.tracks[1], join - kQ, join);
    REQUIRE (run.size() == 2);
    CHECK (run.back().pitch == 41 - 1); // a half step under the chorus's first bass note
    CHECK (run.front().pitch == 41 - 2);

    f.transitions[0] = { Transition::Kind::hold, 2 };
    const auto hold = song_form::build (s, f);
    CHECK (notesBetween (hold, 1, join - 2 * kQ + 1, join) == 0); // no new bass notes in the hold
    CHECK (notesBetween (hold, 0, join - 2 * kQ + 1, join) == 0);

    f.transitions[0] = { Transition::Kind::rest, 1 };
    const auto rest = song_form::build (s, f);
    CHECK (notesBetween (rest, 2, join - kQ, join) == 0);
    for (const auto& n : arranging::playedNotes (rest.tracks[2], 0, join))
        CHECK (n.off <= join - kQ);
}

TEST_CASE ("Song forms: using a form changes the Timeline, and other forms follow")
{
    auto s = popSong();
    song_form::findSections (s);
    auto shortForm = song_form::formFromNames (s, "Short", "Verse 1, Chorus 1, Chorus 1");
    auto other = song_form::formFromNames (s, "Chorus only", "Chorus 1");
    auto gone = song_form::formFromNames (s, "With intro", "Intro, Chorus 1");
    s.forms = { shortForm, other, gone };
    song_form::use (s, shortForm);
    CHECK (s.contentEnd() == 12 * kBar);
    REQUIRE (s.sections.size() == 3);
    CHECK (s.sections[2].name == "Chorus 1 (2)");
    REQUIRE (s.forms.size() == 1); // "With intro" has no intro now; "Short" is the Timeline
    CHECK (s.forms[0].name == "Chorus only");
    CHECK (song_form::problemWith (s, s.forms[0]).empty());
    CHECK_THROWS (song_form::use (s, SongForm {}));
}

TEST_CASE ("Song forms are saved in the project")
{
    auto s = popSong();
    song_form::findSections (s);
    auto f = song_form::formFromNames (s, "Short", "Verse 1, Chorus 1");
    f.transitions[0] = { Transition::Kind::pickup, 2 };
    s.forms.push_back (f);
    const auto back = project_file::fromJson (project_file::toJson (s));
    CHECK (back.sections == s.sections);
    CHECK (back.forms == s.forms);
}

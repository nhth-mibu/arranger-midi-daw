#include "arranger/core/Document.h"
#include "arranger/core/Quantise.h"
#include "arranger/core/SongEdit.h"
#include "arranger/core/TempoMap.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace arranger;
using Catch::Matchers::WithinAbs;

namespace
{
    Clip clipWith (std::vector<Note> notes, Tick start = 0, Tick length = 3840 * 2)
    {
        Clip c;
        c.start = start;
        c.length = length;
        c.notes = std::move (notes);
        return c;
    }
}

TEST_CASE ("Tempo map converts ticks and seconds both ways")
{
    TempoMap map;
    map.setPoints ({ { 0, 120.0 }, { 3840, 60.0 } }); // one bar at 120, then 60

    CHECK_THAT (map.secondsAt (960), WithinAbs (0.5, 1e-12));
    CHECK_THAT (map.secondsAt (3840), WithinAbs (2.0, 1e-12));
    CHECK_THAT (map.secondsAt (3840 + 960), WithinAbs (3.0, 1e-12));
    CHECK_THAT (map.tickAtSeconds (3.0), WithinAbs (4800.0, 1e-9));
    CHECK_THAT (map.tickAtSeconds (1.0), WithinAbs (1920.0, 1e-9));
    CHECK (map.tempoAt (3839) == 120.0);
    CHECK (map.tempoAt (3840) == 60.0);

    map.set (3840, 90.0);
    CHECK (map.getPoints().size() == 2);
    map.remove (3840);
    CHECK (map.getPoints().size() == 1);
    map.remove (0); // can't remove the first point
    CHECK (map.getPoints().size() == 1);
}

TEST_CASE ("New tracks get distinct colours and skip the drum channel")
{
    Song s;
    for (int i = 0; i < 12; ++i)
        edit::addTrack (s, "T" + std::to_string (i));
    CHECK (s.tracks[0].colour != s.tracks[1].colour);
    for (const auto& t : s.tracks)
        CHECK (t.output.channel != 10);
    CHECK (s.tracks[8].output.channel == 9);
    CHECK (s.tracks[9].output.channel == 11);
    CHECK (s.tracks[0].id != s.tracks[1].id);
}

TEST_CASE ("Editing a clip never changes older versions")
{
    Song s;
    const auto t = edit::addTrack (s, "Piano").id;
    const auto id = edit::addClip (s, t, clipWith ({ { 0, 480, 60, 100 } }));

    const Song before = s;
    edit::modifyClip (s, id, [] (Clip& c) { c.notes.push_back ({ 480, 480, 62, 90 }); });

    CHECK (edit::findClipPtr (before, id)->notes.size() == 1);
    CHECK (edit::findClipPtr (s, id)->notes.size() == 2);
    CHECK_FALSE (before == s);
}

TEST_CASE ("Undo and redo restore exact versions")
{
    Document doc;
    TrackId track = 0;
    doc.edit ("Add Track", [&] (Song& s) { track = edit::addTrack (s, "Bass").id; });
    ClipId clip = 0;
    doc.edit ("Add Clip", [&] (Song& s) { clip = edit::addClip (s, track, clipWith ({ { 0, 240, 40, 100 } })); });
    const auto withClip = doc.song();
    doc.edit ("Move Clip", [&] (Song& s) { edit::moveClip (s, clip, 3840, track); });

    CHECK (doc.undoDescription() == "Move Clip");
    REQUIRE (doc.undo());
    CHECK (doc.song() == withClip);
    CHECK (doc.redoDescription() == "Move Clip");
    REQUIRE (doc.redo());
    CHECK (edit::findClipPtr (doc.song(), clip)->start == 3840);

    REQUIRE (doc.undo());
    REQUIRE (doc.undo());
    REQUIRE (doc.undo());
    CHECK (doc.song().tracks.empty());
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("Fader drags merge into one undo step")
{
    Document doc;
    doc.edit ("Add Track", [] (Song& s) { edit::addTrack (s, "Lead"); });
    for (int v = 90; v <= 110; ++v)
        doc.edit ("Volume", [v] (Song& s) { s.tracks[0].mixer.volume = v; }, "volume-1");
    CHECK (doc.song().tracks[0].mixer.volume == 110);
    REQUIRE (doc.undo());
    CHECK (doc.song().tracks[0].mixer.volume == 100);
}

TEST_CASE ("Edits that change nothing make no undo step")
{
    Document doc;
    doc.edit ("Nothing", [] (Song&) {});
    CHECK_FALSE (doc.canUndo());
    CHECK_FALSE (doc.isModified());
}

TEST_CASE ("Modified flag follows save and undo")
{
    Document doc;
    doc.edit ("Rename", [] (Song& s) { s.title = "A"; });
    CHECK (doc.isModified());
    doc.markSaved();
    CHECK_FALSE (doc.isModified());
    doc.edit ("Rename", [] (Song& s) { s.title = "B"; });
    CHECK (doc.isModified());
    doc.undo();
    CHECK_FALSE (doc.isModified());
}

TEST_CASE ("Split and join clips")
{
    Song s;
    const auto t = edit::addTrack (s, "Keys").id;
    const auto id = edit::addClip (s, t, clipWith ({ { 0, 960, 60, 100 }, { 3000, 1500, 64, 100 } }, 3840, 7680));

    const auto right = edit::splitClip (s, id, 3840 + 3840);
    REQUIRE (right != 0);
    const auto left = edit::findClipPtr (s, id);
    const auto r = edit::findClipPtr (s, right);
    CHECK (left->length == 3840);
    CHECK (r->start == 7680);
    REQUIRE (left->notes.size() == 2);
    CHECK (left->notes[1].length == 840); // cut at the split
    CHECK (r->notes.empty());

    edit::joinClips (s, { id, right });
    REQUIRE (s.tracks[0].clips.size() == 1);
    CHECK (s.tracks[0].clips[0]->length == 7680);
}

TEST_CASE ("Overlapping notes of the same pitch are cleaned up")
{
    auto c = clipWith ({ { 0, 960, 60, 100 }, { 480, 960, 60, 80 }, { 480, 960, 60, 80 }, { 0, 100, 62, 100 } });
    edit::removeOverlaps (c);
    REQUIRE (c.notes.size() == 3);
    CHECK (c.notes[0].pitch == 60);
    CHECK (c.notes[0].length == 480);
}

TEST_CASE ("Quantise snaps to the song grid")
{
    QuantiseSettings q;
    q.grid = QuantiseSettings::gridFromName ("1/16");

    SECTION ("full strength")
    {
        auto c = clipWith ({ { 250, 200, 60, 100 }, { 470, 100, 62, 100 } }, 3840);
        quantise (c, q);
        CHECK (c.notes[0].start == 240);
        CHECK (c.notes[0].length == 200); // lengths kept
        CHECK (c.notes[1].start == 480);
    }
    SECTION ("half strength moves halfway")
    {
        q.strength = 0.5;
        auto c = clipWith ({ { 260, 100, 60, 100 } });
        quantise (c, q);
        CHECK (c.notes[0].start == 250);
    }
    SECTION ("a clip off the bar line still lands on the song grid")
    {
        auto c = clipWith ({ { 150, 100, 60, 100 } }, 100); // at song tick 250
        quantise (c, q);
        CHECK (c.start + c.notes[0].start == 240);
    }
    SECTION ("swing delays the off-beat sixteenths")
    {
        q.swing = 0.5; // shifts every second step by a quarter step (60 ticks)
        auto c = clipWith ({ { 245, 100, 60, 100 }, { 490, 100, 62, 100 } });
        quantise (c, q);
        CHECK (c.notes[0].start == 300);
        CHECK (c.notes[1].start == 480);
    }
    SECTION ("ends too")
    {
        q.quantiseEnds = true;
        auto c = clipWith ({ { 10, 450, 60, 100 } });
        quantise (c, q);
        CHECK (c.notes[0].start == 0);
        CHECK (c.notes[0].length == 480);
    }
}

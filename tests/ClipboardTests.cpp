#include "arranger/core/Clipboard.h"

#include <catch2/catch_test_macros.hpp>

using namespace arranger;

namespace
{
    constexpr Tick kBeat = kTicksPerQuarter;
    constexpr Tick kBar = kBeat * 4;

    Clip clipAt (Tick start, Tick length, std::vector<Note> notes = {})
    {
        Clip c;
        c.start = start;
        c.length = length;
        c.notes = std::move (notes);
        return c;
    }
}

TEST_CASE ("Clips copy and paste across tracks, by kind", "[clipboard]")
{
    Song song;
    auto a = edit::addTrack (song, "A").id;
    auto b = edit::addTrack (song, "B").id;
    auto& audioTrack = edit::addTrack (song, "Audio");
    audioTrack.output.kind = TrackOutput::Kind::audio;
    auto au = audioTrack.id;

    auto c1 = edit::addClip (song, a, clipAt (kBar, kBar, { { 0, kBeat, 60, 100 } }));
    auto c2 = edit::addClip (song, b, clipAt (kBar * 2, kBar));
    AudioClip take;
    take.file = "take.wav";
    take.start = kBar * 3;
    take.length = kBar;
    auto c3 = edit::addAudioClip (song, au, take);

    auto content = clipboard::copyClips (song, { c1, c2, c3 });
    REQUIRE (content.kind == clipboard::Content::Kind::clips);
    REQUIRE (content.clips.size() == 3);
    CHECK (content.span == kBar * 3);

    SECTION ("pasted on the same first track, the layout stays")
    {
        auto ids = clipboard::pasteClips (song, content, a, kBar * 8);
        REQUIRE (ids.size() == 3);
        CHECK (edit::findClipPtr (song, ids[0])->start == kBar * 8);
        CHECK (edit::trackOfClip (song, ids[1]) == b);
        CHECK (edit::findClipPtr (song, ids[1])->start == kBar * 9);
        CHECK (edit::findAudioClip (song, ids[2])->start == kBar * 10);
        CHECK (edit::findClipPtr (song, ids[0])->notes.size() == 1);
    }
    SECTION ("a clip goes only on a track of its kind")
    {
        auto ids = clipboard::pasteClips (song, content, b, 0);
        // A lands on B, B on the audio track (skipped), the audio clip falls past the last track.
        REQUIRE (ids.size() == 1);
        CHECK (edit::trackOfClip (song, ids[0]) == b);
    }
}

TEST_CASE ("Notes paste into the clip there, or into a new clip", "[clipboard]")
{
    Song song;
    auto t = edit::addTrack (song, "Piano").id;
    auto c = edit::addClip (song, t, clipAt (0, kBar, { { kBeat, kBeat, 60, 90 }, { kBeat * 2, kBeat, 64, 80 } }));
    auto clip = edit::findClipPtr (song, c);

    auto content = clipboard::copyNotes (song, { { c, clip->notes[0] }, { c, clip->notes[1] } });
    REQUIRE (content.kind == clipboard::Content::Kind::notes);
    CHECK (content.notes[0].start == 0);
    CHECK (content.notes[1].start == kBeat);
    CHECK (content.span == kBeat * 2);

    SECTION ("into the clip, which grows up to the bar line")
    {
        auto keys = clipboard::pasteNotes (song, content, t, kBeat * 3);
        REQUIRE (keys.size() == 2);
        CHECK (edit::findClipPtr (song, c)->length == kBar * 2);
        CHECK (edit::findClipPtr (song, c)->notes.size() == 4);
    }
    SECTION ("a place with no clip gets a new clip from the bar line")
    {
        auto keys = clipboard::pasteNotes (song, content, t, kBar * 4 + kBeat);
        REQUIRE (keys.size() == 2);
        CHECK (keys[0].clip != c);
        auto made = edit::findClipPtr (song, keys[0].clip);
        CHECK (made->start == kBar * 4);
        CHECK (made->length == kBar);
        CHECK (made->notes[0].start == kBeat);
    }
    SECTION ("in one clip, notes after its end are dropped")
    {
        auto keys = clipboard::pasteNotesInClip (song, content, c, kBeat * 3);
        CHECK (keys.size() == 1);
        CHECK (edit::findClipPtr (song, c)->length == kBar);
    }
}

TEST_CASE ("Automation points copy and paste", "[clipboard]")
{
    Song song;
    auto t = edit::addTrack (song, "Pad").id;
    edit::setAutomationPoint (song, t, 0, { kBar, 20.0f });
    edit::setAutomationPoint (song, t, 0, { kBar * 2, 110.0f });
    edit::setAutomationPoint (song, t, 0, { kBar * 3, 64.0f });

    auto content = clipboard::copyPoints (edit::findTrack (song, t)->volumeAutomation, { 0, 1 });
    REQUIRE (content.kind == clipboard::Content::Kind::points);
    CHECK (content.span == kBar);

    auto times = clipboard::pastePoints (song, content, t, 1, kBar * 4);
    REQUIRE (times.size() == 2);
    const auto& pan = edit::findTrack (song, t)->panAutomation;
    REQUIRE (pan.size() == 2);
    CHECK (pan[0].at == kBar * 4);
    CHECK (pan[1].value == 110.0f);
}

TEST_CASE ("Clip grid slots copy and paste", "[clipboard]")
{
    Song song;
    auto a = edit::addTrack (song, "A").id;
    auto b = edit::addTrack (song, "B").id;
    edit::setSlot (song, a, 0, clipAt (0, kBar * 2, { { 0, kBeat, 36, 100 } }));

    auto content = clipboard::copySlot (song, a, 0);
    REQUIRE (content.kind == clipboard::Content::Kind::clips);
    CHECK (clipboard::copySlot (song, a, 3).empty());

    auto id = clipboard::pasteSlot (song, content, b, 2);
    REQUIRE (id != 0);
    CHECK (edit::sceneOfClip (song, id) == 2);
    CHECK (edit::findClipPtr (song, id)->length == kBar * 2);

    // Timeline notes pasted into a slot make a clip of whole bars.
    auto t = edit::addClip (song, a, clipAt (kBar, kBar, { { kBeat, kBeat * 5, 60, 90 } }));
    auto notes = clipboard::copyNotes (song, { { t, edit::findClipPtr (song, t)->notes[0] } });
    auto made = clipboard::pasteSlot (song, notes, a, 1);
    REQUIRE (made != 0);
    CHECK (edit::findClipPtr (song, made)->length == kBar * 2);
    CHECK (edit::findClipPtr (song, made)->notes[0].start == 0);
}

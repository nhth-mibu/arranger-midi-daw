#include "TestRender.h"

#include "arranger/core/ProjectFile.h"
#include "arranger/core/SongEdit.h"

#include <catch2/catch_test_macros.hpp>

#include <map>

using namespace arranger;
using test::Renderer;

namespace
{
    constexpr Tick kBeat = kTicksPerQuarter;
    constexpr std::int64_t kBeatSamples = 24000; // 120 BPM at 48 kHz

    /** A hardware track with one Timeline note (pitch 60 on each bar start) and two slots. */
    Song gridSong (TrackId& id)
    {
        Song song;
        song.metronome.enabled = false;
        auto& t = edit::addTrack (song, "Synth");
        t.output = { TrackOutput::Kind::midiPort, "Synth", 1 };
        id = t.id;

        Clip timeline;
        timeline.length = kBeat * 16;
        timeline.loopLength = kBeat * 4;
        timeline.notes = { { 0, 100, 60, 100 } };
        edit::addClip (song, id, timeline);

        Clip a; // one beat long: a note on every beat
        a.length = kBeat;
        a.notes = { { 0, 100, 72, 100 } };
        edit::setSlot (song, id, 0, a);

        Clip b; // two beats long, with a note that is held to the end
        b.length = kBeat * 2;
        b.notes = { { kBeat, kBeat, 76, 100 } };
        edit::setSlot (song, id, 1, b);
        return song;
    }

    std::vector<std::int64_t> onsOf (const Renderer& r, int pitch)
    {
        std::vector<std::int64_t> out;
        for (const auto& [sample, p] : r.noteOns())
            if (p == pitch)
                out.push_back (sample);
        return out;
    }
}

TEST_CASE ("Slots are edited like clips and saved in the project file")
{
    TrackId id = 0;
    auto song = gridSong (id);
    const auto slotId = song.tracks[0].slots[1]->id;
    CHECK (edit::sceneOfClip (song, slotId) == 1);
    CHECK (edit::sceneOfClip (song, song.tracks[0].clips[0]->id) == -1);
    CHECK (edit::trackOfClip (song, slotId) == id);
    CHECK (song.sceneCount() == 8);

    edit::modifyClip (song, slotId, [] (Clip& c) { c.notes.push_back ({ 0, 50, 64, 90 }); c.start = 999; });
    REQUIRE (edit::findClipPtr (song, slotId) != nullptr);
    CHECK (edit::findClipPtr (song, slotId)->notes.size() == 2);
    CHECK (edit::findClipPtr (song, slotId)->start == 0); // slots always start at 0
    CHECK (song.tracks[0].clips.size() == 1);

    song.scenes = { "Intro", "Verse" };
    song.launchQuantise = kBeat;
    edit::setSlot (song, id, 9, Clip {});
    CHECK (song.sceneCount() == 11);
    const auto back = project_file::fromJson (project_file::toJson (song));
    CHECK (back == song);
    CHECK (back.tracks[0].slots[5] == nullptr);
    CHECK (back.tracks[0].slots[9] != nullptr);

    edit::removeClip (song, slotId);
    CHECK (song.tracks[0].slots[1] == nullptr);
    edit::clearSlot (song, id, 0);
    CHECK (song.tracks[0].slots[0] == nullptr);
}

TEST_CASE ("A launched slot waits for the quantise point, loops, and takes over from the Timeline")
{
    TrackId id = 0;
    const auto song = gridSong (id);
    Engine e;
    e.prepare (48000.0, 512);
    e.loadSong (song);
    e.play();

    Renderer r (e, 512);
    r.runRandom (kBeatSamples / 2, 1);  // half a beat in
    e.launchSlot (id, 0, kBeat * 4);    // wait for the next bar
    r.runRandom (kBeatSamples * 2, 2);
    CHECK (e.slotStatus (id).pending == 0);
    CHECK (e.slotStatus (id).playing == -1);
    r.runRandom (kBeatSamples * 5, 3);  // to beat 7.5, near the end of bar 2
    CHECK (e.slotStatus (id).playing == 0);
    CHECK (e.slotStatus (id).pending == SlotStatus::kNone);

    // Bar 1: the Timeline note. Bar 2: the slot's note on every beat, and no Timeline note.
    CHECK (onsOf (r, 60) == std::vector<std::int64_t> { 0 });
    CHECK (onsOf (r, 72) == std::vector<std::int64_t> { 4 * kBeatSamples, 5 * kBeatSamples, 6 * kBeatSamples, 7 * kBeatSamples });

    const auto change = e.popSlotChange();
    REQUIRE (change.has_value());
    CHECK (change->track == id);
    CHECK (change->scene == 0);
    CHECK (change->tick == static_cast<double> (kBeat * 4));

    // Stop the slot at the next bar: the Timeline comes back.
    e.stopSlot (id, kBeat * 4);
    r.runRandom (kBeatSamples * 4 + 100, 4);
    CHECK (e.slotStatus (id).playing == -1);
    CHECK (onsOf (r, 60) == std::vector<std::int64_t> { 0, 8 * kBeatSamples });
    CHECK (onsOf (r, 72).size() == 4);
    const auto stopped = e.popSlotChange();
    REQUIRE (stopped.has_value());
    CHECK (stopped->scene == -1);
    CHECK (stopped->tick == static_cast<double> (kBeat * 8));
    CHECK_FALSE (e.popSlotChange().has_value());
}

TEST_CASE ("Switching slots and wrapping never leaves a note hanging")
{
    TrackId id = 0;
    const auto song = gridSong (id);
    Engine e;
    e.prepare (48000.0, 512);
    e.loadSong (song);

    Renderer r (e, 512);
    e.launchSlot (id, 1, 0); // launching while stopped starts playback
    r.runRandom (kBeatSamples * 5, 5);
    CHECK (e.isPlaying());
    CHECK (onsOf (r, 76) == std::vector<std::int64_t> { kBeatSamples, 3 * kBeatSamples }); // the two-beat slot loops

    e.launchSlot (id, 0, 0); // switch while 76 is held... or just after: either way it must end
    r.runRandom (kBeatSamples * 2, 6);
    e.stopAllSlots (0);
    r.runRandom (kBeatSamples, 7);
    e.stop();
    r.runFixed (1024, 512);

    std::map<int, int> held;
    for (const auto& s : r.sent)
    {
        if (s.event.isNoteOn()) ++held[s.event.bytes[1]];
        if (s.event.isNoteOff()) --held[s.event.bytes[1]];
    }
    for (const auto& [pitch, count] : held)
        CHECK (count == 0);

    // Every note-on of the held note got its note-off within the slot length.
    std::int64_t on = -1;
    for (const auto& s : r.sent)
    {
        if (s.event.bytes[1] != 76)
            continue;
        if (s.event.isNoteOn()) on = s.sample;
        if (s.event.isNoteOff() && on >= 0)
        {
            CHECK (s.sample - on <= kBeatSamples);
            on = -1;
        }
    }
}

TEST_CASE ("A scene launches its slot on every track that has one and stops the others")
{
    TrackId first = 0;
    auto song = gridSong (first);
    auto& other = edit::addTrack (song, "Bass");
    other.output = { TrackOutput::Kind::midiPort, "Synth", 2 };
    const auto second = other.id;
    Clip c;
    c.length = kBeat;
    c.notes = { { 0, 100, 40, 100 } };
    edit::setSlot (song, second, 0, c); // the bass has scene 1 only

    auto& pad = edit::addTrack (song, "Pad");
    pad.output = { TrackOutput::Kind::midiPort, "Synth", 3 };
    Clip padLine;
    padLine.length = kBeat * 4;
    padLine.notes = { { 0, 100, 55, 100 } };
    edit::addClip (song, pad.id, padLine); // the pad has only a Timeline part

    Engine e;
    e.prepare (48000.0, 512);
    e.loadSong (song);
    Renderer r (e, 512);
    e.launchScene (0, 0);
    r.runFixed (kBeatSamples, 512);
    CHECK (e.slotStatus (first).playing == 0);
    CHECK (e.slotStatus (second).playing == 0);
    CHECK (onsOf (r, 55).empty()); // no clip in the scene: its Timeline part is silent too

    e.launchScene (1, 0);
    r.runFixed (kBeatSamples, 512);
    CHECK (e.slotStatus (first).playing == 1);
    CHECK (e.slotStatus (second).playing == -1); // no slot in scene 2: stopped
    CHECK (e.slotStatus (9999).playing == -1);
}

TEST_CASE ("The Clip grid takes over the Timeline on every track while a slot plays", "[clipgrid][engine]")
{
    TrackId id = 0;
    auto song = gridSong (id);
    edit::modifyClip (song, song.tracks[0].clips[0]->id, [] (Clip& c) {
        c.notes.push_back ({ 0, 100, 62, 100 });
    });

    auto& other = edit::addTrack (song, "Pad");
    other.output = { TrackOutput::Kind::midiPort, "Synth", 2 };
    const auto otherId = other.id;
    Clip line;
    line.length = kBeat * 16;
    line.notes = { { 0, 100, 48, 100 }, { kBeat * 8, 100, 50, 100 } };
    edit::addClip (song, otherId, line);

    Engine e;
    e.prepare (48000.0, 512);
    e.loadSong (song);
    Renderer r (e, 512);

    e.launchSlot (id, 0, 0); // starts the Clip grid at once
    r.runFixed (kBeatSamples * 5, 512);
    CHECK (onsOf (r, 72).size() == 5); // the slot loops on its track
    CHECK (onsOf (r, 60).empty());     // its Timeline note is silent
    CHECK (onsOf (r, 48).empty());     // so is another track's Timeline note

    e.stopSlot (id, 0);
    r.runFixed (kBeatSamples * 4, 512); // to beat 9
    CHECK (onsOf (r, 62).size() == 1);  // the Timeline returns on the slot's track
    CHECK (onsOf (r, 50).size() == 1);  // and on the other track
}

TEST_CASE ("Slots keep their phase when the song loop wraps")
{
    TrackId id = 0;
    auto song = gridSong (id);
    song.loop = { true, 0, kBeat * 3 }; // a three-beat song loop against the two-beat slot
    Engine e;
    e.prepare (48000.0, 512);
    e.loadSong (song);
    Renderer r (e, 512);
    e.launchSlot (id, 1, 0);
    r.runRandom (kBeatSamples * 8, 9);
    // The slot's note keeps landing every two beats in real time, whatever the song position does.
    CHECK (onsOf (r, 76) == std::vector<std::int64_t> { kBeatSamples, 3 * kBeatSamples, 5 * kBeatSamples, 7 * kBeatSamples });
}

TEST_CASE ("Launching while stopped mid-bar starts on the bar line, in time with later launches")
{
    TrackId id = 0;
    auto song = gridSong (id);
    auto& bass = edit::addTrack (song, "Bass");
    bass.output = { TrackOutput::Kind::midiPort, "Bass", 2 };
    const auto bassId = bass.id;
    Clip b; // one bar: a note on beat 1 only
    b.length = kBeat * 4;
    b.notes = { { 0, 100, 40, 100 } };
    edit::setSlot (song, bassId, 0, b);

    Engine e;
    e.prepare (48000.0, 512);
    e.loadSong (song);
    Renderer r (e, 512);
    e.locate (kBeat * 5 + kBeat / 2); // stopped in bar 2, between beats 2 and 3
    r.runFixed (1024, 512);
    r.sent.clear();
    const auto startSample = r.rendered;

    e.launchSlot (id, 0, kBeat * 4);
    r.runRandom (kBeatSamples * 2 + 100, 21);
    CHECK (e.isPlaying());
    // The first slot starts on bar 2's line, not where the playhead stood.
    const auto first = e.popSlotChange();
    REQUIRE (first.has_value());
    CHECK (first->tick == static_cast<double> (kBeat * 4));

    e.launchSlot (bassId, 0, kBeat * 4); // waits for bar 3
    r.runRandom (kBeatSamples * 4, 22);
    const auto second = e.popSlotChange();
    REQUIRE (second.has_value());
    CHECK (second->tick == static_cast<double> (kBeat * 8));

    // Every note lands on a beat counted from the launch: both parts share one grid.
    for (auto pitch : { 72, 40 })
        for (auto at : onsOf (r, pitch))
            CHECK ((at - startSample) % kBeatSamples == 0);
    CHECK (onsOf (r, 40) == std::vector<std::int64_t> { startSample + 4 * kBeatSamples });
}

TEST_CASE ("Launching while stopped with no quantise starts where the playhead is")
{
    TrackId id = 0;
    const auto song = gridSong (id);
    Engine e;
    e.prepare (48000.0, 512);
    e.loadSong (song);
    Renderer r (e, 512);
    e.locate (kBeat * 5 + kBeat / 2);
    r.runFixed (1024, 512);
    e.launchSlot (id, 0, 0);
    r.runFixed (1024, 512);
    const auto change = e.popSlotChange();
    REQUIRE (change.has_value());
    CHECK (change->tick == static_cast<double> (kBeat * 5 + kBeat / 2));
}

TEST_CASE ("Clearing a stretch of the Timeline cuts clips at its edges")
{
    Song song;
    const auto id = edit::addTrack (song, "T").id;
    Clip c;
    c.start = 0;
    c.length = kBeat * 8;
    c.notes = { { 0, 100, 60, 100 }, { kBeat * 3, 100, 62, 100 }, { kBeat * 6, 100, 64, 100 } };
    edit::addClip (song, id, c);
    Clip later;
    later.start = kBeat * 8;
    later.length = kBeat * 4;
    edit::addClip (song, id, later);

    edit::clearRange (song, id, kBeat * 2, kBeat * 4);
    const auto& clips = song.tracks[0].clips;
    REQUIRE (clips.size() == 3);
    CHECK (clips[0]->start == 0);
    CHECK (clips[0]->length == kBeat * 2);
    CHECK (clips[0]->notes.size() == 1);
    CHECK (clips[1]->start == kBeat * 4);
    CHECK (clips[1]->length == kBeat * 4);
    REQUIRE (clips[1]->notes.size() == 1);
    CHECK (clips[1]->notes[0].pitch == 64);
    CHECK (clips[2]->start == kBeat * 8);

    edit::clearRange (song, id, kBeat * 3, kBeat * 20);
    REQUIRE (song.tracks[0].clips.size() == 1);
    CHECK (song.tracks[0].clips[0]->length == kBeat * 2);
}

TEST_CASE ("The Clip grid fills from the Timeline, one scene per part of the song", "[clipgrid]")
{
    Song song;
    const Tick bar = song.timeSignature.ticksPerBar();
    auto& bass = edit::addTrack (song, "Bass");
    const auto bassId = bass.id;
    Clip line;
    line.start = 0;
    line.length = bar * 8;
    line.notes = { { 0, 480, 40, 100 }, { bar * 4 + 10, 480, 43, 90 }, { bar * 4 - 240, 480, 45, 80 } }; // the last one crosses into bar 5
    edit::addClip (song, bassId, line);

    auto& drums = edit::addTrack (song, "Drums");
    const auto drumsId = drums.id;
    Clip loop;
    loop.start = bar * 4; // only in the second part
    loop.length = bar * 4;
    loop.loopLength = bar;
    loop.notes = { { 0, 120, 36, 110 } };
    edit::addClip (song, drumsId, loop);
    edit::addAudioTrack (song, "Vocals");

    CHECK (edit::slotsFromTimeline (song, 0) == 0);
    REQUIRE (edit::slotsFromTimeline (song, bar * 4) == 2);
    REQUIRE (song.scenes.size() >= 2);
    CHECK (song.scenes[0] == "Bars 1-4");
    CHECK (song.scenes[1] == "Bars 5-8");

    const auto* b = edit::findTrack (song, bassId);
    REQUIRE (b->slots.size() == 2);
    REQUIRE (b->slots[0] != nullptr);
    CHECK (b->slots[0]->notes.size() == 2);
    CHECK (b->slots[0]->length == bar * 4);
    // A note that crosses the edge ends at the edge.
    for (const auto& n : b->slots[0]->notes)
        CHECK (n.end() <= bar * 4);
    REQUIRE (b->slots[1] != nullptr);
    CHECK (b->slots[1]->notes.front().start == 10);

    const auto* d = edit::findTrack (song, drumsId);
    REQUIRE (d->slots.size() == 2);
    CHECK (d->slots[0] == nullptr); // nothing there in bars 1-4
    REQUIRE (d->slots[1] != nullptr);
    CHECK (d->slots[1]->notes.size() == 4); // the one-bar loop, played out over four bars
}

TEST_CASE ("A clip pad toggles the slot that is playing", "[clip-grid]")
{
    SlotStatus status;
    status.playing = 2;
    CHECK (slotToggleStops (status, 2));
    CHECK_FALSE (slotToggleStops (status, 1));

    status.pending = 3;
    CHECK (slotToggleStops (status, 3));
    CHECK_FALSE (slotToggleStops (status, 4));
    CHECK_FALSE (slotToggleStops (status, -1));
}

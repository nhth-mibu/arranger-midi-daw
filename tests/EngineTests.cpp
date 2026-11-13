#include "TestRender.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace arranger;
using Catch::Matchers::WithinAbs;
using test::Renderer;

TEST_CASE ("Play, stop and resume keep the song position")
{
    Engine e;
    e.prepare (48000.0, 512);
    e.loadSong ({});
    Renderer r (e, 512);

    r.runFixed (4800, 480);
    CHECK_FALSE (e.isPlaying());
    CHECK (e.positionTicks() == 0.0);

    e.play();
    r.runFixed (48000, 480); // one second = two quarters
    CHECK (e.isPlaying());
    CHECK_THAT (e.positionTicks(), WithinAbs (1920.0, 1e-6));

    e.stop();
    r.runFixed (4800, 480);
    CHECK_FALSE (e.isPlaying());
    CHECK_THAT (e.positionTicks(), WithinAbs (1920.0, 1e-6));

    e.play();
    r.runFixed (24000, 480);
    CHECK_THAT (e.positionTicks(), WithinAbs (2880.0, 1e-6));
}

TEST_CASE ("Locate moves the playhead and the next click follows it")
{
    Engine e;
    e.prepare (48000.0, 512);
    e.loadSong ({});
    e.locate (480); // half a beat in
    e.play();

    Renderer r (e, 512);
    r.runFixed (48000, 512);

    REQUIRE_FALSE (r.clicks.empty());
    CHECK (r.clicks[0].sample == 12000); // the rest of the half beat
    CHECK (r.clicks[0].note == Metronome::kBeatNote);
}

TEST_CASE ("Lock-free queue passes items in order")
{
    SpscQueue<int, 8> q;
    for (int i = 0; i < 7; ++i)
        CHECK (q.push (i));
    CHECK_FALSE (q.push (99)); // full: one slot stays free
    for (int i = 0; i < 7; ++i)
        CHECK (q.pop().value() == i);
    CHECK_FALSE (q.pop().has_value());
}

TEST_CASE ("MIDI buffer never grows on the audio thread")
{
    MidiEventBuffer b (4);
    for (int i = 0; i < 6; ++i)
        b.add (MidiEvent::noteOn (i, 1, 60, 100));
    CHECK (b.size() == 4);
    CHECK (b.droppedCount() == 2);
}

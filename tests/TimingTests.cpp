// Timing to the sample: every note goes out on the sample the tempo map says,
// with many tempo changes, any block size and a loop. And the Clip grid starts
// clips on the bar, in time with the Timeline and the metronome.

#include "TestRender.h"

#include "arranger/core/SongEdit.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

using namespace arranger;
using test::Renderer;

namespace
{
    constexpr double kRate = 48000.0;

    /** The sample a tick plays at, from the tempo map. */
    std::int64_t sampleOf (const TempoMap& tempo, double tick)
    {
        return static_cast<std::int64_t> (std::llround (tempo.secondsAt (tick) * kRate));
    }

    Song randomTempoSong (unsigned seed)
    {
        std::mt19937 rng (seed);
        Song s;
        s.metronome.enabled = false;
        std::vector<TempoPoint> points { { 0, 40.0 + static_cast<double> (rng() % 200) } };
        for (int i = 1; i < 30; ++i)
            points.push_back ({ points.back().at + 1 + static_cast<Tick> (rng() % 2000), 30.0 + static_cast<double> (rng() % 2700) / 10.0 });
        s.tempo.setPoints (points);
        auto& t = edit::addTrack (s, "Out");
        t.output = { TrackOutput::Kind::midiPort, "Out", 1 };
        Clip c;
        c.length = 3840 * 8;
        for (int n = 0; n < 300; ++n)
            c.notes.push_back ({ static_cast<Tick> (rng() % (3840 * 8 - 10)), 5, static_cast<std::uint8_t> (n % 128), 100 });
        edit::normalise (c);
        edit::addClip (s, t.id, c);
        return s;
    }
}

TEST_CASE ("Every note goes out on the sample the tempo map gives, through 30 tempo changes", "[timing]")
{
    for (const unsigned seed : { 1u, 2u, 3u })
    {
        INFO ("seed " << seed);
        const auto song = randomTempoSong (seed);
        Engine e;
        e.prepare (kRate, 1024);
        e.loadSong (song);
        e.play();
        Renderer r (e, 1024);
        r.runRandom (sampleOf (song.tempo, 3840 * 8) + 100, seed);

        // The expected notes, sorted by start (and pitch, for notes at the same tick).
        std::vector<std::pair<std::int64_t, int>> expected;
        for (const auto& n : song.tracks[0].clips[0]->notes)
            expected.push_back ({ sampleOf (song.tempo, static_cast<double> (n.start)), n.pitch });
        std::sort (expected.begin(), expected.end());
        auto got = r.noteOns();
        std::sort (got.begin(), got.end());

        REQUIRE (got.size() == expected.size());
        int off = 0;
        for (std::size_t i = 0; i < got.size(); ++i)
            if (std::abs (got[i].first - expected[i].first) > 1 || got[i].second != expected[i].second)
                ++off;
        CHECK (off == 0);
    }
}

TEST_CASE ("A loop repeats on the same samples, pass after pass", "[timing]")
{
    Song s;
    s.metronome.enabled = false;
    s.tempo.setPoints ({ { 0, 133.0 }, { 960, 91.5 } });
    s.loop = { true, 480, 480 + 3840 };
    auto& t = edit::addTrack (s, "Out");
    t.output = { TrackOutput::Kind::midiPort, "Out", 1 };
    Clip c;
    c.length = 3840 * 2;
    c.notes = { { 480, 10, 60, 100 }, { 1000, 10, 62, 100 }, { 4319, 1, 64, 100 } };
    edit::addClip (s, t.id, c);

    Engine e;
    e.prepare (kRate, 1024);
    e.loadSong (s);
    e.locate (480);
    e.play();
    Renderer r (e, 1024);
    const auto loopSamples = sampleOf (s.tempo, 480 + 3840) - sampleOf (s.tempo, 480);
    r.runRandom (loopSamples * 10, 9);

    const auto ons = r.noteOns();
    REQUIRE (ons.size() >= 27);
    for (std::size_t i = 3; i < ons.size(); ++i)
    {
        CHECK (ons[i].second == ons[i - 3].second);
        // One pass later, within a sample (each pass is rounded to whole samples).
        CHECK (std::abs (ons[i].first - ons[i - 3].first - loopSamples) <= 1);
    }
}

TEST_CASE ("Clip grid launches start on the next bar, in time with the metronome", "[timing][grid]")
{
    Song s;
    s.metronome.enabled = true;
    auto& t = edit::addTrack (s, "Out");
    t.output = { TrackOutput::Kind::midiPort, "Out", 1 };
    const auto id = t.id;
    Clip c;
    c.length = 3840;
    c.notes = { { 0, 10, 60, 100 }, { 960, 10, 61, 100 } };
    edit::setSlot (s, id, 0, c);

    Engine e;
    e.prepare (kRate, 512);
    e.loadSong (s);
    e.play();
    Renderer r (e, 512);
    r.runRandom (30000, 1); // a bit into the first bar (a bar is 96,000 samples at 120 BPM)
    e.launchScene (0, 3840);
    r.runRandom (96000 * 3, 2);

    const auto ons = r.noteOns();
    REQUIRE (ons.size() >= 4);
    CHECK (ons[0].first == 96000); // the next bar, not at once
    CHECK (ons[0].second == 60);
    CHECK (ons[1].first == 96000 + 24000);
    CHECK (ons[2].first == 192000); // the slot loops each bar

    // The downbeat click lands on the same sample as the clip start.
    bool clickOnBar = false;
    for (const auto& click : r.clicks)
        clickOnBar = clickOnBar || click.sample == 96000;
    CHECK (clickOnBar);
}

TEST_CASE ("Timing does not drift over a long song at an odd sample rate", "[timing]")
{
    Song s;
    s.metronome.enabled = false;
    s.tempo.setPoints ({ { 0, 97.3 } });
    auto& t = edit::addTrack (s, "Out");
    t.output = { TrackOutput::Kind::midiPort, "Out", 1 };
    Clip c;
    c.length = 960;
    c.loopLength = 960;
    c.notes = { { 0, 10, 60, 100 } };
    c.length = 960 * 600; // 600 beats: about six minutes
    edit::addClip (s, t.id, c);

    const double rate = 44100.0;
    Engine e;
    e.prepare (rate, 1024);
    e.loadSong (s);
    e.play();
    Renderer r (e, 1024);
    r.runRandom (static_cast<std::int64_t> (s.tempo.secondsAt (960.0 * 600) * rate), 4);

    const auto ons = r.noteOns();
    REQUIRE (ons.size() >= 590);
    for (std::size_t i = 0; i < ons.size(); ++i)
    {
        const auto expected = static_cast<std::int64_t> (std::llround (s.tempo.secondsAt (960.0 * static_cast<double> (i)) * rate));
        CHECK (std::abs (ons[i].first - expected) <= 1);
    }
}

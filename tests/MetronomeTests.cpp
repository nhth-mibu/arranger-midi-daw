#include "TestRender.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

using namespace arranger;
using test::Renderer;

namespace
{
    Song clickSong (double bpm, TimeSignature sig = {})
    {
        Song s;
        s.tempo = TempoMap (bpm);
        s.timeSignature = sig;
        s.metronome = { true, 0.7f, true };
        return s;
    }

    Engine& startedEngine (Engine& e, double sampleRate, double bpm, TimeSignature sig = {})
    {
        e.prepare (sampleRate, 1024);
        e.loadSong (clickSong (bpm, sig));
        e.play();
        return e;
    }

    /** Exact sample (rounded up) where beat k lands, worked out from scratch. */
    std::int64_t idealSample (std::int64_t beat, double samplesPerBeat)
    {
        return static_cast<std::int64_t> (std::ceil (static_cast<double> (beat) * samplesPerBeat - 1.0e-6));
    }
}

TEST_CASE ("Clicks land on exact beat samples at 120 BPM, 48 kHz")
{
    Engine e;
    Renderer r (startedEngine (e, 48000.0, 120.0));
    r.runFixed (48000 * 10, 512);

    REQUIRE (r.clicks.size() == 20);
    for (std::size_t k = 0; k < r.clicks.size(); ++k)
        CHECK (r.clicks[k].sample == static_cast<std::int64_t> (k) * 24000);
}

TEST_CASE ("No drift over an hour, at an awkward tempo and sample rate")
{
    const double bpm = GENERATE (112.37, 97.0, 133.33);
    const double rate = GENERATE (44100.0, 48000.0, 96000.0);

    Engine e;
    Renderer r (startedEngine (e, rate, bpm));
    r.runRandom (static_cast<std::int64_t> (rate * 3600), 1234);

    const double samplesPerBeat = rate * 60.0 / bpm;
    const auto expectedClicks = static_cast<std::size_t> (std::floor ((rate * 3600 - 1) / samplesPerBeat)) + 1;
    REQUIRE (r.clicks.size() == expectedClicks);

    std::int64_t worst = 0;
    for (std::size_t k = 0; k < r.clicks.size(); ++k)
        worst = std::max (worst, std::abs (r.clicks[k].sample - idealSample (static_cast<std::int64_t> (k), samplesPerBeat)));

    CHECK (worst == 0); // every click on its exact sample, an hour in
}

TEST_CASE ("Output does not depend on the audio buffer size")
{
    Engine a, b;
    Renderer ra (startedEngine (a, 44100.0, 128.0));
    Renderer rb (startedEngine (b, 44100.0, 128.0));
    ra.keepAudio = rb.keepAudio = true;

    ra.runFixed (44100 * 20, 64);
    rb.runRandom (44100 * 20, 99);

    REQUIRE (ra.clicks.size() == rb.clicks.size());
    for (std::size_t i = 0; i < ra.clicks.size(); ++i)
        CHECK (ra.clicks[i].sample == rb.clicks[i].sample);
    CHECK (ra.audio == rb.audio); // bit for bit
}

TEST_CASE ("The click sound starts on the beat sample")
{
    Engine e;
    Renderer r (startedEngine (e, 48000.0, 120.0));
    r.keepAudio = true;
    r.runFixed (48000 * 2, 480);

    CHECK (r.audio[0] != 0.0f);
    CHECK (r.audio[23999] == 0.0f);
    CHECK (r.audio[24000] != 0.0f);
}

TEST_CASE ("Downbeats are accented, following the meter")
{
    SECTION ("3/4: accent every third quarter")
    {
        Engine e;
        Renderer r (startedEngine (e, 48000.0, 120.0, { 3, 4 }));
        r.runFixed (48000 * 3, 256);
        REQUIRE (r.clicks.size() == 6);
        for (std::size_t k = 0; k < 6; ++k)
            CHECK (r.clicks[k].note == (k % 3 == 0 ? Metronome::kAccentNote : Metronome::kBeatNote));
    }

    SECTION ("6/8: clicks on eighths, accent every sixth")
    {
        Engine e;
        Renderer r (startedEngine (e, 48000.0, 120.0, { 6, 8 }));
        r.runFixed (48000 * 3, 256); // 3 s = 6 quarters = 12 eighths
        REQUIRE (r.clicks.size() == 12);
        CHECK (r.clicks[1].sample == 12000);
        CHECK (r.clicks[6].note == Metronome::kAccentNote);
        CHECK (r.clicks[7].note == Metronome::kBeatNote);
    }
}

TEST_CASE ("A tempo change takes effect from the next beat")
{
    Engine e;
    Renderer r (startedEngine (e, 48000.0, 120.0));
    r.runFixed (36000, 500); // 1.5 beats at 120 BPM
    e.loadSong (clickSong (60.0));
    r.runFixed (48000 * 4, 500);

    REQUIRE (r.clicks.size() >= 4);
    CHECK (r.clicks[0].sample == 0);
    CHECK (r.clicks[1].sample == 24000);
    // Half a beat was left at the change; at 60 BPM that half takes 24000 samples.
    CHECK (r.clicks[2].sample == 36000 + 24000);
    CHECK (r.clicks[3].sample - r.clicks[2].sample == 48000);
}

TEST_CASE ("Metronome off means silence and no notes")
{
    Engine e;
    startedEngine (e, 48000.0, 120.0);
    auto song = clickSong (120.0);
    song.metronome.enabled = false;
    e.loadSong (song);
    Renderer r (e);
    r.keepAudio = true;
    r.runFixed (48000 * 2, 512);

    CHECK (r.clicks.empty());
    for (float s : r.audio)
        REQUIRE (s == 0.0f);
}

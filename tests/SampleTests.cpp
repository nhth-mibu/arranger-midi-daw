#include "arranger/core/Sample.h"
#include "arranger/core/Transients.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>

using namespace arranger;
using Catch::Approx;

namespace
{
    /** A loop of decaying sine bursts ("hits") at the given seconds. */
    Sample makeLoop (const std::vector<double>& hitTimes, double seconds, double rate = 44100.0, float level = 0.8f)
    {
        Sample s;
        s.sampleRate = rate;
        s.channels.assign (2, std::vector<float> (static_cast<std::size_t> (seconds * rate), 0.0f));
        for (const double t : hitTimes)
        {
            const auto start = static_cast<std::size_t> (t * rate);
            for (std::size_t i = start; i < s.frames(); ++i)
            {
                const double age = static_cast<double> (i - start) / rate;
                const auto v = static_cast<float> (level * std::exp (-age * 30.0) * std::sin (age * 2.0 * 3.14159265358979 * 180.0));
                s.channels[0][i] += v;
                s.channels[1][i] += v;
            }
        }
        return s;
    }
}

TEST_CASE ("WAV files round-trip in 16, 24 and 32 bit")
{
    const auto loop = makeLoop ({ 0.0, 0.25 }, 0.5);
    for (const int bits : { 16, 24, 32 })
    {
        const auto back = wav::parse (wav::serialise (loop, bits));
        REQUIRE (back.channelCount() == 2);
        REQUIRE (back.frames() == loop.frames());
        CHECK (back.sampleRate == 44100.0);
        const float tolerance = bits == 16 ? 1.0e-4f : 1.0e-6f;
        float worst = 0.0f;
        for (std::size_t i = 0; i < loop.frames(); ++i)
            worst = std::max (worst, std::abs (back.channels[1][i] - loop.channels[1][i]));
        CHECK (worst < tolerance);
    }
}

TEST_CASE ("WAV files are written to and read from disk, and bad files are refused")
{
    const auto path = (std::filesystem::temp_directory_path() / "arranger-test-loop.wav").string();
    wav::write (makeLoop ({ 0.0 }, 0.1), path, 16);
    const auto s = wav::read (path);
    CHECK (s.name == "arranger-test-loop");
    CHECK (s.seconds() == Approx (0.1).margin (0.001));

    CHECK_THROWS_AS (wav::parse ({ 'n', 'o', 'p', 'e' }), wav::Error);
    CHECK_THROWS_AS (wav::read ("/no/such/file.wav"), wav::Error);
}

TEST_CASE ("Transient detection finds each hit, close to where it starts")
{
    const std::vector<double> hits { 0.0, 0.25, 0.5, 0.625, 0.75, 1.0, 1.5 };
    const auto loop = makeLoop (hits, 2.0);
    const auto found = findTransients (loop.mono(), loop.sampleRate);
    REQUIRE (found.size() == hits.size());
    for (std::size_t i = 0; i < hits.size(); ++i)
    {
        const double at = static_cast<double> (found[i].frame) / loop.sampleRate;
        CHECK (at == Approx (hits[i]).margin (0.004));
        CHECK (found[i].strength > 0.3f);
    }
}

TEST_CASE ("Silence and steady tones have no transients after the start")
{
    std::vector<float> silence (44100, 0.0f);
    CHECK (findTransients (silence, 44100.0).empty());

    std::vector<float> tone (44100);
    for (std::size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.5f * static_cast<float> (std::sin (static_cast<double> (i) * 0.05));
    CHECK (findTransients (tone, 44100.0).size() == 1); // only the start
}

TEST_CASE ("Low sensitivity keeps only strong hits")
{
    auto loop = makeLoop ({ 0.0, 0.5 }, 1.0);
    const auto ghost = makeLoop ({ 0.25 }, 1.0, 44100.0, 0.05f); // a quiet hit while the first still rings
    for (std::size_t i = 0; i < loop.frames(); ++i)
        for (auto c : { std::size_t { 0 }, std::size_t { 1 } })
            loop.channels[c][i] += ghost.channels[c][i];

    CHECK (findTransients (loop.mono(), loop.sampleRate, { 1.0, 0.05 }).size() == 3);
    CHECK (findTransients (loop.mono(), loop.sampleRate, { 0.0, 0.05 }).size() == 2);
}

TEST_CASE ("Slices cover the whole sample with no gaps")
{
    const auto loop = makeLoop ({ 0.0, 0.25, 0.5, 0.75 }, 1.0);
    for (const auto& slices : { slicing::byTransients (loop), slicing::byGrid (loop.frames(), 8), slicing::byMarkers (loop.frames(), { 30000, 100, 100, 999999 }) })
    {
        REQUIRE_FALSE (slices.empty());
        CHECK (slices.front().start == 0);
        CHECK (slices.back().end == loop.frames());
        for (std::size_t i = 1; i < slices.size(); ++i)
        {
            CHECK (slices[i].start == slices[i - 1].end);
            CHECK (slices[i].end > slices[i].start);
        }
    }
    CHECK (slicing::byTransients (loop).size() == 4);
    CHECK (slicing::byGrid (loop.frames(), 8).size() == 8);
    CHECK (slicing::byMarkers (loop.frames(), { 30000, 100, 100, 999999 }).size() == 3);
    CHECK (slicing::byGrid (0, 8).empty());
}

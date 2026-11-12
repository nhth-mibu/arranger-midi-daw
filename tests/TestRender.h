#pragma once

#include "arranger/engine/Engine.h"

#include <cstdint>
#include <functional>
#include <random>
#include <vector>

namespace arranger::test
{

struct Click
{
    std::int64_t sample;
    int note;
};

/** Plays an engine for `totalSamples` in blocks of varying size and records every click. */
struct Renderer
{
    Engine& engine;
    std::vector<float> left, right;
    std::vector<Click> clicks;
    std::int64_t rendered = 0;
    int maxBlock = 1024;
    bool keepAudio = false;
    std::vector<float> audio; ///< mono copy of the output when keepAudio is on

    explicit Renderer (Engine& e, int maxBlockSize = 1024)
        : engine (e), left (static_cast<std::size_t> (maxBlockSize)), right (static_cast<std::size_t> (maxBlockSize)), maxBlock (maxBlockSize) {}

    void run (std::int64_t totalSamples, const std::function<int()>& nextBlockSize)
    {
        MidiEventBuffer in, out;
        const std::int64_t end = rendered + totalSamples;
        while (rendered < end)
        {
            const int n = static_cast<int> (std::min<std::int64_t> (nextBlockSize(), end - rendered));
            float* chans[] = { left.data(), right.data() };
            AudioBlock block { chans, 2, n };
            out.clear();
            engine.process (block, in, out);

            for (const auto& e : out)
                if (e.isNoteOn())
                    clicks.push_back ({ rendered + e.sampleOffset, e.bytes[1] });

            if (keepAudio)
                audio.insert (audio.end(), left.begin(), left.begin() + n);

            rendered += n;
        }
    }

    void runFixed (std::int64_t totalSamples, int blockSize)
    {
        run (totalSamples, [blockSize] { return blockSize; });
    }

    void runRandom (std::int64_t totalSamples, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_int_distribution<int> dist (1, maxBlock);
        run (totalSamples, [&] { return dist (rng); });
    }
};

} // namespace arranger::test

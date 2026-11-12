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

/** Any MIDI message the engine sent to a hardware port, with its absolute sample. */
struct Sent
{
    std::int64_t sample;
    MidiEvent event;
};

/** Plays an engine for `totalSamples` in blocks of varying size and records every click. */
struct Renderer
{
    Engine& engine;
    std::vector<float> left, right;
    std::vector<Click> clicks;   ///< metronome notes (sent to port 99)
    std::vector<Sent> sent;      ///< everything else that went to hardware ports
    MidiEventBuffer input { 256 }; ///< fed to the engine in the next block, then cleared
    std::int64_t rendered = 0;
    int maxBlock = 1024;
    bool keepAudio = false;
    std::vector<float> audio; ///< mono copy of the output when keepAudio is on

    static constexpr std::uint16_t kClickPort = 99;

    explicit Renderer (Engine& e, int maxBlockSize = 1024)
        : engine (e), left (static_cast<std::size_t> (maxBlockSize)), right (static_cast<std::size_t> (maxBlockSize)), maxBlock (maxBlockSize)
    {
        engine.setClickPort (kClickPort);
    }

    /** Note-ons sent to hardware, as (sample, pitch). */
    std::vector<std::pair<std::int64_t, int>> noteOns() const
    {
        std::vector<std::pair<std::int64_t, int>> r;
        for (const auto& s : sent)
            if (s.event.isNoteOn())
                r.push_back ({ s.sample, s.event.bytes[1] });
        return r;
    }

    void run (std::int64_t totalSamples, const std::function<int()>& nextBlockSize)
    {
        MidiEventBuffer out;
        const std::int64_t end = rendered + totalSamples;
        while (rendered < end)
        {
            const int n = static_cast<int> (std::min<std::int64_t> (nextBlockSize(), end - rendered));
            float* chans[] = { left.data(), right.data() };
            AudioBlock block { chans, 2, n };
            out.clear();
            engine.process (block, input, out);
            input.clear();

            for (const auto& e : out)
            {
                if (e.port == kClickPort)
                {
                    if (e.isNoteOn())
                        clicks.push_back ({ rendered + e.sampleOffset, e.bytes[1] });
                }
                else
                {
                    sent.push_back ({ rendered + e.sampleOffset, e });
                }
            }

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

#include "arranger/core/TimeStretch.h"

#include <algorithm>
#include <cmath>

namespace arranger::stretch
{

Sample changeSpeed (const Sample& in, double speed)
{
    speed = std::clamp (speed, 0.25, 4.0);
    Sample out;
    out.name = in.name;
    out.sampleRate = in.sampleRate;
    const auto frames = in.frames();
    const auto channels = in.channels.size();
    out.channels.assign (channels, {});
    if (frames == 0)
        return out;
    if (std::abs (speed - 1.0) < 1.0e-6)
    {
        out.channels = in.channels;
        return out;
    }

    // Pieces of about 40 ms, half of each over the next; the search looks 10 ms each way.
    const auto window = std::max<std::size_t> (64, static_cast<std::size_t> (in.sampleRate * 0.04) & ~std::size_t { 1 });
    const auto hop = window / 2;
    const auto reach = static_cast<std::ptrdiff_t> (std::max<std::size_t> (16, static_cast<std::size_t> (in.sampleRate * 0.01)));
    const auto outFrames = static_cast<std::size_t> (std::llround (static_cast<double> (frames) / speed));
    const auto mono = in.mono();
    const auto total = static_cast<std::ptrdiff_t> (frames);

    std::vector<float> hann (window);
    for (std::size_t i = 0; i < window; ++i)
        hann[i] = static_cast<float> (0.5 - 0.5 * std::cos (6.283185307179586 * static_cast<double> (i) / static_cast<double> (window)));

    for (auto& c : out.channels)
        c.assign (outFrames + window, 0.0f);

    std::ptrdiff_t previous = 0; // where the last piece came from
    for (std::size_t k = 0; k * hop < outFrames; ++k)
    {
        auto from = static_cast<std::ptrdiff_t> (std::llround (static_cast<double> (k * hop) * speed));
        if (k > 0)
        {
            // The piece that goes on from the last one without a join. Find the start near `from` that looks most like it.
            const auto natural = previous + static_cast<std::ptrdiff_t> (hop);
            double best = -1.0e30;
            auto bestAt = from;
            const auto compare = static_cast<std::ptrdiff_t> (hop);
            for (auto d = -reach; d <= reach; d += 2)
            {
                const auto at = from + d;
                if (at < 0 || at + compare >= total || natural + compare >= total)
                    continue;
                double dot = 0.0, energy = 1.0e-9;
                for (std::ptrdiff_t i = 0; i < compare; i += 2)
                {
                    const double a = mono[static_cast<std::size_t> (at + i)], b = mono[static_cast<std::size_t> (natural + i)];
                    dot += a * b;
                    energy += a * a;
                }
                const double score = dot / std::sqrt (energy);
                if (score > best)
                {
                    best = score;
                    bestAt = at;
                }
            }
            from = bestAt;
        }
        from = std::clamp<std::ptrdiff_t> (from, 0, std::max<std::ptrdiff_t> (0, total - 1));
        previous = from;
        const auto to = k * hop;
        for (std::size_t c = 0; c < channels; ++c)
        {
            const auto& src = in.channels[c];
            auto& dst = out.channels[c];
            for (std::size_t i = 0; i < window; ++i)
            {
                const auto s = static_cast<std::size_t> (from) + i;
                if (s >= frames)
                    break;
                // The first piece starts at full level: there is nothing before it to fade from.
                dst[to + i] += src[s] * (k == 0 && i < hop ? 1.0f : hann[i]);
            }
        }
    }
    for (auto& c : out.channels)
        c.resize (outFrames);
    return out;
}

double tempoFromLength (double seconds, double nearBpm, int beatsPerBar)
{
    if (seconds <= 0.0 || nearBpm <= 0.0)
        return 0.0;
    const double barsAtNear = seconds * nearBpm / 60.0 / std::max (1, beatsPerBar);
    // Loops are 1, 2, 4, 8... bars long far more often than 3 or 5.
    double best = 0.0, bestDistance = 1.0e30;
    for (int bars = 1; bars <= 64; bars *= 2)
    {
        const double distance = std::abs (std::log2 (barsAtNear / bars));
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = bars * beatsPerBar * 60.0 / seconds;
        }
    }
    return best;
}

} // namespace arranger::stretch

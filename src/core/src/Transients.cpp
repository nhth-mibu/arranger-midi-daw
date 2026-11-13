#include "arranger/core/Transients.h"

#include <algorithm>
#include <cmath>

namespace arranger
{

std::vector<Transient> findTransients (const std::vector<float>& mono, double sampleRate, const TransientSettings& settings)
{
    std::vector<Transient> found;
    if (mono.empty() || sampleRate <= 0)
        return found;

    // Energy in short hops, then how much each hop rises above the one before
    // (in dB, so quiet and loud hits are judged alike).
    const auto hop = static_cast<std::size_t> (std::max (16.0, sampleRate * 0.0029)); // about 3 ms
    const std::size_t hops = mono.size() / hop;
    if (hops < 2)
        return found;

    std::vector<double> level (hops);
    for (std::size_t h = 0; h < hops; ++h)
    {
        double sum = 0;
        for (std::size_t i = h * hop; i < (h + 1) * hop; ++i)
            sum += static_cast<double> (mono[i]) * mono[i];
        level[h] = 10.0 * std::log10 (sum / static_cast<double> (hop) + 1.0e-9); // floor at -90 dB
    }

    std::vector<double> rise (hops, 0.0);
    rise[0] = std::max (0.0, level[0] + 60.0); // a file that starts with sound starts with a hit
    for (std::size_t h = 1; h < hops; ++h)
    {
        // Compare with the loudest of the last few hops, so a decaying tail isn't a new hit.
        double before = level[h - 1];
        for (std::size_t k = 2; k <= 4 && k <= h; ++k)
            before = std::max (before, level[h - k] - 1.5 * static_cast<double> (k));
        rise[h] = std::max (0.0, level[h] - before);
    }

    const double sensitivity = std::clamp (settings.sensitivity, 0.0, 1.0);
    const double thresholdDb = 24.0 - 21.0 * sensitivity;   // 24 dB jump .. 3 dB jump
    // Ignore hits much quieter than the loudest part: 20 dB below .. 60 dB below.
    const double loudest = *std::max_element (level.begin(), level.end());
    const double floorDb = std::max (-80.0, loudest - 20.0 - 40.0 * sensitivity);
    const auto minGapHops = static_cast<std::size_t> (std::max (1.0, settings.minGapSeconds * sampleRate / static_cast<double> (hop)));

    double strongest = 0.0;
    std::vector<std::pair<std::size_t, double>> peaks;
    for (std::size_t h = 0; h < hops; ++h)
    {
        // An attack can take a few hops to reach its full level.
        double reached = level[h];
        for (std::size_t k = 1; k <= 3 && h + k < hops; ++k)
            reached = std::max (reached, level[h + k]);
        if (rise[h] < thresholdDb || reached < floorDb)
            continue;
        if (! peaks.empty() && h - peaks.back().first < minGapHops)
        {
            if (rise[h] > peaks.back().second && h - peaks.back().first <= 2)
                peaks.back().second = rise[h]; // the same attack, still rising
            continue;
        }
        peaks.push_back ({ h, rise[h] });
    }
    for (const auto& p : peaks)
        strongest = std::max (strongest, p.second);

    for (const auto& [h, strength] : peaks)
    {
        // Walk back inside the hop before to where the attack really starts.
        std::size_t frame = h * hop;
        if (h > 0)
        {
            const std::size_t from = (h - 1) * hop;
            float peak = 0.0f;
            for (std::size_t i = frame; i < frame + hop; ++i)
                peak = std::max (peak, std::abs (mono[i]));
            const float gate = peak * 0.1f;
            std::size_t i = frame + hop - 1;
            while (i > from && std::abs (mono[i]) < gate)
                --i;
            while (i > from && std::abs (mono[i - 1]) >= gate * 0.5f)
                --i;
            frame = i;
        }
        else
            frame = 0;
        found.push_back ({ frame, strongest > 0 ? static_cast<float> (strength / strongest) : 1.0f });
    }
    return found;
}

namespace slicing
{

std::vector<Slice> byMarkers (std::size_t frames, std::vector<std::size_t> starts)
{
    std::vector<Slice> slices;
    if (frames == 0)
        return slices;
    starts.push_back (0);
    std::sort (starts.begin(), starts.end());
    starts.erase (std::unique (starts.begin(), starts.end()), starts.end());
    while (! starts.empty() && starts.back() >= frames)
        starts.pop_back();
    for (std::size_t i = 0; i < starts.size(); ++i)
        slices.push_back ({ starts[i], i + 1 < starts.size() ? starts[i + 1] : frames });
    return slices;
}

std::vector<Slice> byGrid (std::size_t frames, int count)
{
    std::vector<std::size_t> starts;
    count = std::max (1, count);
    for (int i = 0; i < count; ++i)
        starts.push_back (static_cast<std::size_t> (std::llround (static_cast<double> (frames) * i / count)));
    return byMarkers (frames, std::move (starts));
}

std::vector<Slice> byTransients (const Sample& sample, const TransientSettings& settings)
{
    const auto mono = sample.mono();
    std::vector<std::size_t> starts;
    for (const auto& t : findTransients (mono, sample.sampleRate, settings))
        starts.push_back (nearestZeroCrossing (mono, t.frame, static_cast<std::size_t> (sample.sampleRate * 0.001)));
    return byMarkers (sample.frames(), std::move (starts));
}

std::size_t nearestZeroCrossing (const std::vector<float>& mono, std::size_t frame, std::size_t reach)
{
    if (mono.empty())
        return 0;
    frame = std::min (frame, mono.size() - 1);
    const auto crosses = [&] (std::size_t i) { return i == 0 || (mono[i - 1] <= 0.0f) != (mono[i] <= 0.0f) || mono[i] == 0.0f; };
    for (std::size_t d = 0; d <= reach; ++d)
    {
        if (frame >= d && crosses (frame - d))
            return frame - d;
        if (frame + d < mono.size() && crosses (frame + d))
            return frame + d;
    }
    return frame;
}

} // namespace slicing
} // namespace arranger

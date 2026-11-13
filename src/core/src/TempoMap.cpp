#include "arranger/core/TempoMap.h"

#include <algorithm>

namespace arranger
{

void TempoMap::set (Tick at, double bpm)
{
    at = std::max<Tick> (0, at);
    bpm = clampTempo (bpm);
    auto it = std::lower_bound (points.begin(), points.end(), at, [] (const TempoPoint& p, Tick t) { return p.at < t; });
    if (it != points.end() && it->at == at)
        it->bpm = bpm;
    else
        points.insert (it, { at, bpm });
    rebuild();
}

void TempoMap::remove (Tick at)
{
    if (at == 0)
        return;
    std::erase_if (points, [at] (const TempoPoint& p) { return p.at == at; });
    rebuild();
}

void TempoMap::setPoints (std::vector<TempoPoint> newPoints)
{
    for (auto& p : newPoints)
    {
        p.at = std::max<Tick> (0, p.at);
        p.bpm = clampTempo (p.bpm);
    }
    std::stable_sort (newPoints.begin(), newPoints.end(), [] (auto& a, auto& b) { return a.at < b.at; });
    // Keep the last point given for any tick.
    std::vector<TempoPoint> unique;
    for (const auto& p : newPoints)
    {
        if (! unique.empty() && unique.back().at == p.at)
            unique.back() = p;
        else
            unique.push_back (p);
    }
    if (unique.empty() || unique.front().at != 0)
        unique.insert (unique.begin(), { 0, unique.empty() ? 120.0 : unique.front().bpm });
    points = std::move (unique);
    rebuild();
}

void TempoMap::rebuild()
{
    startSeconds.assign (points.size(), 0.0);
    for (std::size_t i = 1; i < points.size(); ++i)
        startSeconds[i] = startSeconds[i - 1]
                        + static_cast<double> (points[i].at - points[i - 1].at) * secondsPerTick (points[i - 1].bpm);
}

std::size_t TempoMap::indexAt (double tick) const noexcept
{
    // Last point with at <= tick.
    std::size_t lo = 0, hi = points.size();
    while (hi - lo > 1)
    {
        const auto mid = (lo + hi) / 2;
        if (static_cast<double> (points[mid].at) <= tick)
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

double TempoMap::tempoAt (double tick) const noexcept
{
    return points[indexAt (tick)].bpm;
}

double TempoMap::secondsAt (double tick) const noexcept
{
    const auto i = indexAt (tick);
    return startSeconds[i] + (tick - static_cast<double> (points[i].at)) * secondsPerTick (points[i].bpm);
}

double TempoMap::tickAtSeconds (double seconds) const noexcept
{
    std::size_t lo = 0, hi = points.size();
    while (hi - lo > 1)
    {
        const auto mid = (lo + hi) / 2;
        if (startSeconds[mid] <= seconds)
            lo = mid;
        else
            hi = mid;
    }
    return static_cast<double> (points[lo].at) + (seconds - startSeconds[lo]) / secondsPerTick (points[lo].bpm);
}

} // namespace arranger

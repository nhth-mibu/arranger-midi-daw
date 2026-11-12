#pragma once

#include "arranger/core/Time.h"

#include <vector>

namespace arranger
{

struct TempoPoint
{
    Tick at = 0;
    double bpm = 120.0;

    bool operator== (const TempoPoint&) const = default;
};

/**
    Tempo changes over the song. There is always a point at tick 0. Between
    points the tempo is constant (no ramps yet).

    All conversions are computed from the start of the song, so they are exact
    to floating point precision and never accumulate error.
*/
class TempoMap
{
public:
    TempoMap() = default;
    explicit TempoMap (double bpm) { points[0].bpm = clampTempo (bpm); }

    const std::vector<TempoPoint>& getPoints() const noexcept { return points; }

    /** Adds or replaces the point at `at`. */
    void set (Tick at, double bpm);
    /** Removes the point at `at` (the point at 0 can't be removed). */
    void remove (Tick at);
    /** Replaces all points. Sorts, drops duplicates and adds a 0 point if missing. */
    void setPoints (std::vector<TempoPoint>);

    double tempoAt (double tick) const noexcept;
    /** The point index that covers `tick`. */
    std::size_t indexAt (double tick) const noexcept;

    double secondsAt (double tick) const noexcept;
    double tickAtSeconds (double seconds) const noexcept;

    bool operator== (const TempoMap& o) const { return points == o.points; }

private:
    void rebuild();

    std::vector<TempoPoint> points { TempoPoint {} };
    std::vector<double> startSeconds { 0.0 }; ///< seconds at each point, cached
};

} // namespace arranger

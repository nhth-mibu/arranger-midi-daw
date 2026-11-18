#include "arranger/core/KidsMode.h"

#include <algorithm>
#include <cmath>

namespace arranger::kids_mode
{

double clampPauseAfter (double seconds) noexcept
{
    if (! std::isfinite (seconds))
        return kDefaultPauseAfter;
    return std::clamp (seconds, kMinPauseAfter, kMaxPauseAfter);
}

void Keys::down (int key, double nowMs)
{
    downAt[key] = nowMs;
    lastMs = std::max (lastMs, nowMs);
    ++pressCount;
}

void Keys::up (int key, double nowMs)
{
    if (downAt.erase (key) > 0)
        lastMs = std::max (lastMs, nowMs);
}

void Keys::tap (double nowMs)
{
    lastMs = std::max (lastMs, nowMs);
    ++pressCount;
}

void Keys::reset()
{
    downAt.clear();
    lastMs = -1.0e12;
}

int Keys::held (double nowMs) const
{
    return static_cast<int> (std::count_if (downAt.begin(), downAt.end(), [nowMs] (const auto& k) { return nowMs - k.second < kMaxHoldMs; }));
}

bool Keys::quiet (double nowMs, double pauseAfterSeconds) const
{
    if (held (nowMs) > 0)
        return false;
    // A key held too long stops counting at the moment it ran out.
    double last = lastMs;
    for (const auto& k : downAt)
        last = std::max (last, k.second + kMaxHoldMs);
    return nowMs - last >= clampPauseAfter (pauseAfterSeconds) * 1000.0;
}

} // namespace arranger::kids_mode

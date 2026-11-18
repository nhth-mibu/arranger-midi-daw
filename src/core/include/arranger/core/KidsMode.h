#pragma once

#include <map>

/**
    Kids Mode: pretend play for children. Any key on any keyboard plays the
    song; when no key is pressed for a short time, the song pauses, and the
    next key goes on from there.

    This class only keeps the time: it hears the keys and says when the song
    must pause. The app starts and pauses the transport. Plain C++, no JUCE.
*/
namespace arranger::kids_mode
{

/** The shortest and longest wait before the song pauses, in seconds. */
inline constexpr double kMinPauseAfter = 0.5;
inline constexpr double kMaxPauseAfter = 5.0;
inline constexpr double kDefaultPauseAfter = 1.5;

/** A key held for longer than this (a lost note-off) no longer keeps the song playing. */
inline constexpr double kMaxHoldMs = 20000.0;

/** Limits a wait to kMinPauseAfter..kMaxPauseAfter. */
double clampPauseAfter (double seconds) noexcept;

class Keys
{
public:
    /** A key goes down. While it is held, the song keeps playing. */
    void down (int key, double nowMs);
    /** A key comes up. The wait starts again from now. */
    void up (int key, double nowMs);
    /** A key with no "up" (a computer key, a click). */
    void tap (double nowMs);
    /** Forgets all keys (a note-off was lost, or the mode was switched on again). */
    void reset();

    /** True when no key is held and none was pressed or let go for `pauseAfterSeconds`. */
    bool quiet (double nowMs, double pauseAfterSeconds) const;

    /** Number of keys held now (keys held too long do not count). */
    int held (double nowMs) const;

    /** Counts every key that went down or was tapped. The screen uses it to bounce. */
    int presses() const noexcept { return pressCount; }

private:
    std::map<int, double> downAt; ///< key -> time it went down
    double lastMs = -1.0e12;      ///< last key down, up or tap
    int pressCount = 0;
};

} // namespace arranger::kids_mode

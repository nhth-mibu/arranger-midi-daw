#pragma once

#include <cstdint>
#include <string>

namespace arranger
{

/** Musical time. Every position in a song is stored in ticks. */
using Tick = std::int64_t;

/** Ticks per quarter note. 960 divides cleanly into triplets, quintuplets and 64ths. */
inline constexpr Tick kTicksPerQuarter = 960;

inline constexpr double kMinTempoBpm = 20.0;
inline constexpr double kMaxTempoBpm = 400.0;

double clampTempo (double bpm) noexcept;

struct TimeSignature
{
    int numerator = 4;
    int denominator = 4;

    /** True for 1..32 over 1, 2, 4, 8, 16 or 32. */
    bool isValid() const noexcept;

    /** Length of one beat (one denominator note) in ticks. */
    Tick ticksPerBeat() const noexcept { return kTicksPerQuarter * 4 / denominator; }
    Tick ticksPerBar() const noexcept { return ticksPerBeat() * numerator; }

    bool operator== (const TimeSignature&) const = default;
};

/** A position as shown to people: bar, beat and sixteenth, all counted from 1. */
struct BarBeat
{
    std::int64_t bar = 1;
    int beat = 1;
    int sixteenth = 1;
    int tick = 0; ///< remaining ticks inside the sixteenth

    bool operator== (const BarBeat&) const = default;
};

/** Converts a tick position (0 = start of bar 1) to bar, beat and sixteenth. */
BarBeat toBarBeat (Tick position, TimeSignature sig) noexcept;

/** Formats as "17.3.2". */
std::string formatBarBeat (const BarBeat&);

/** Seconds per tick at a tempo given in quarter notes per minute. */
inline double secondsPerTick (double bpm) noexcept { return 60.0 / (bpm * static_cast<double> (kTicksPerQuarter)); }

} // namespace arranger

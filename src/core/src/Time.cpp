#include "arranger/core/Time.h"

#include <algorithm>

namespace arranger
{

double clampTempo (double bpm) noexcept
{
    if (! (bpm == bpm)) // NaN
        return 120.0;
    return std::clamp (bpm, kMinTempoBpm, kMaxTempoBpm);
}

bool TimeSignature::isValid() const noexcept
{
    const bool goodDenominator = denominator == 1 || denominator == 2 || denominator == 4
                              || denominator == 8 || denominator == 16 || denominator == 32;
    return goodDenominator && numerator >= 1 && numerator <= 32;
}

BarBeat toBarBeat (Tick position, TimeSignature sig) noexcept
{
    if (! sig.isValid())
        sig = {};

    const Tick bar = sig.ticksPerBar();
    const Tick beat = sig.ticksPerBeat();
    const Tick sixteenth = kTicksPerQuarter / 4;

    // Floor division so negative positions (count-in) still read sensibly.
    Tick barIndex = position / bar;
    Tick inBar = position % bar;
    if (inBar < 0)
    {
        inBar += bar;
        --barIndex;
    }

    const Tick beatIndex = inBar / beat;
    const Tick inBeat = inBar % beat;

    BarBeat result;
    result.bar = barIndex + 1;
    result.beat = static_cast<int> (beatIndex) + 1;
    result.sixteenth = static_cast<int> (inBeat / sixteenth) + 1;
    result.tick = static_cast<int> (inBeat % sixteenth);
    return result;
}

std::string formatBarBeat (const BarBeat& p)
{
    return std::to_string (p.bar) + "." + std::to_string (p.beat) + "." + std::to_string (p.sixteenth);
}

} // namespace arranger

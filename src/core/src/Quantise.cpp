#include "arranger/core/Quantise.h"

#include "arranger/core/SongEdit.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace arranger
{

Tick QuantiseSettings::gridFromName (const std::string& name)
{
    const Tick q = kTicksPerQuarter;
    if (name == "1/1") return q * 4;
    if (name == "1/2") return q * 2;
    if (name == "1/4") return q;
    if (name == "1/8") return q / 2;
    if (name == "1/8T") return q / 3;
    if (name == "1/16") return q / 4;
    if (name == "1/16T") return q / 6;
    if (name == "1/32") return q / 8;
    return q / 4;
}

Tick nearestGridPoint (Tick position, const QuantiseSettings& s)
{
    const Tick grid = std::max<Tick> (1, s.grid);
    const auto stepIndex = static_cast<Tick> (std::floor (static_cast<double> (position) / static_cast<double> (grid)));
    const Tick swingOffset = static_cast<Tick> (std::llround (std::clamp (s.swing, 0.0, 1.0) * static_cast<double> (grid) * 0.5));

    auto pointFor = [&] (Tick step) { return step * grid + ((step % 2 != 0) ? swingOffset : 0); };

    Tick best = pointFor (stepIndex);
    for (Tick step = stepIndex - 1; step <= stepIndex + 1; ++step)
        if (std::llabs (pointFor (step) - position) < std::llabs (best - position))
            best = pointFor (step);
    return best;
}

void quantise (Clip& clip, const QuantiseSettings& s)
{
    const double strength = std::clamp (s.strength, 0.0, 1.0);
    for (auto& n : clip.notes)
    {
        const Tick absStart = clip.start + n.start;
        const Tick target = nearestGridPoint (absStart, s);
        if (s.window > 0 && std::llabs (target - absStart) > s.window)
            continue;

        const Tick oldEnd = absStart + n.length;
        const Tick newStart = absStart + static_cast<Tick> (std::llround (static_cast<double> (target - absStart) * strength));
        Tick newEnd = oldEnd + (newStart - absStart); // keep the length

        if (s.quantiseEnds)
        {
            const Tick endTarget = nearestGridPoint (oldEnd, s);
            newEnd = oldEnd + static_cast<Tick> (std::llround (static_cast<double> (endTarget - oldEnd) * strength));
            if (newEnd <= newStart)
                newEnd = newStart + std::max<Tick> (1, s.grid);
        }

        n.start = std::max<Tick> (0, newStart - clip.start);
        n.length = std::max<Tick> (1, newEnd - newStart);
    }
    edit::normalise (clip);
}

} // namespace arranger

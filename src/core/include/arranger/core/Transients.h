#pragma once

#include "arranger/core/Sample.h"

#include <cstddef>
#include <vector>

namespace arranger
{

/**
    Finds the starts of hits in audio. The slicer uses it to chop loops; drum
    audio to MIDI will use it too, which is why it lives on its own.
*/
struct TransientSettings
{
    double sensitivity = 0.5;   ///< 0 finds only the strongest hits, 1 finds nearly everything
    double minGapSeconds = 0.05; ///< hits closer than this count as one
};

struct Transient
{
    std::size_t frame = 0; ///< where the hit starts
    float strength = 0.0f; ///< 0..1, relative to the strongest hit
};

std::vector<Transient> findTransients (const std::vector<float>& mono, double sampleRate, const TransientSettings& = {});

/** One slice of a sample: [start, end) in frames. */
struct Slice
{
    std::size_t start = 0;
    std::size_t end = 0;

    bool operator== (const Slice&) const = default;
};

namespace slicing
{
    /** Slices at each transient. The first slice always starts at frame 0. */
    std::vector<Slice> byTransients (const Sample&, const TransientSettings& = {});

    /** `count` equal slices (a loop of 2 bars cut in 16ths is count = 32). */
    std::vector<Slice> byGrid (std::size_t frames, int count);

    /** Slices from hand-placed start points (sorted and de-duplicated for you). */
    std::vector<Slice> byMarkers (std::size_t frames, std::vector<std::size_t> starts);

    /** Moves a frame to the nearest zero crossing within `reach` frames, to avoid clicks. */
    std::size_t nearestZeroCrossing (const std::vector<float>& mono, std::size_t frame, std::size_t reach);
}

} // namespace arranger

#pragma once

#include "arranger/core/Song.h"

namespace arranger
{

struct QuantiseSettings
{
    Tick grid = kTicksPerQuarter / 4; ///< 1/16 by default
    double strength = 1.0;            ///< 0..1: how far notes move toward the grid
    double swing = 0.0;               ///< 0..1: delays every second grid step by up to half a step
    bool quantiseEnds = false;        ///< also snap note ends (lengths)
    Tick window = 0;                  ///< only move notes within this distance of a grid line; 0 = all

    /** Common grids: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32. */
    static Tick gridFromName (const std::string&);
};

/** Nearest grid position to an absolute tick, with swing applied. */
Tick nearestGridPoint (Tick position, const QuantiseSettings&);

/**
    Quantises the notes of a clip. Positions are measured on the song grid
    (clip start + note start), so clips that don't start on a bar still line up.
*/
void quantise (Clip&, const QuantiseSettings&);

} // namespace arranger

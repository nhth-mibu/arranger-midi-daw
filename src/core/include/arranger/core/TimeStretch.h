#pragma once

#include "arranger/core/Sample.h"

/**
    Time-stretch: makes audio faster or slower and keeps its pitch. It joins
    short pieces of the audio where their waves agree (WSOLA). Good for drum
    loops and single instruments at small changes; a large change can sound
    rough.
*/
namespace arranger::stretch
{

/** A new sample that plays `speed` times as fast (2 = half as long). 0.25 to 4. */
Sample changeSpeed (const Sample&, double speed);

/** The tempo of a loop, from its length: the number of whole bars that puts the tempo nearest to `nearBpm`. */
double tempoFromLength (double seconds, double nearBpm, int beatsPerBar = 4);

} // namespace arranger::stretch

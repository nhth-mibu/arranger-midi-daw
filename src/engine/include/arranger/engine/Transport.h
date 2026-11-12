#pragma once

#include "arranger/engine/Processor.h"

namespace arranger
{

/**
    Song position and tempo, owned by the audio thread.

    Time is kept as a sample counter plus a tempo anchor (a tick that falls on a
    known sample). Changing tempo moves the anchor to "now". Every position is
    computed from the anchor, so there is no rounding error building up.
*/
class Transport
{
public:
    void prepare (double sampleRate) noexcept;

    void play() noexcept;
    void stop() noexcept;
    void locate (double tick) noexcept;
    void setTempo (double bpm) noexcept;
    void setTimeSignature (TimeSignature) noexcept;

    /** Describes the next block of `numSamples`. */
    ProcessContext contextForBlock (int numSamples) const noexcept;

    /** Moves time forward after a block was rendered. */
    void advance (int numSamples) noexcept;

    bool isPlaying() const noexcept { return playing; }
    double positionTicks() const noexcept;
    double tempo() const noexcept { return tempoBpm; }
    TimeSignature timeSignature() const noexcept { return sig; }

private:
    double ticksPerSample() const noexcept;

    double sampleRate = 48000.0;
    double tempoBpm = 120.0;
    TimeSignature sig;
    bool playing = false;

    std::int64_t sampleTime = 0;
    std::int64_t anchorSample = 0;
    double anchorTick = 0.0;
};

} // namespace arranger

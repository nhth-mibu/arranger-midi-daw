#pragma once

#include "arranger/engine/Processor.h"

namespace arranger
{

/**
    Song position, owned by the audio thread.

    Time is kept as a sample counter plus an anchor (a tick that falls on a
    known sample). Positions come from the anchor through the tempo map, so
    there is no rounding error building up. Changing the tempo map or locating
    moves the anchor to "now".
*/
class Transport
{
public:
    Transport();

    void prepare (double sampleRate) noexcept;

    void play() noexcept;
    void stop() noexcept;
    void locate (double tick) noexcept;

    /** The map must stay alive until the next call (the engine keeps it in the snapshot). */
    void setTempoMap (const TempoMap*) noexcept;
    void setTimeSignature (TimeSignature) noexcept;

    /** Describes the next `numSamples` samples. */
    ProcessContext contextFor (int numSamples, int blockOffset) const noexcept;

    /** Moves time forward after a span was rendered. */
    void advance (int numSamples) noexcept;

    bool isPlaying() const noexcept { return playing; }
    double positionTicks() const noexcept;
    double tempoNow() const noexcept;
    TimeSignature timeSignature() const noexcept { return sig; }
    std::int64_t samplePosition() const noexcept { return sampleTime; }
    double sampleRate() const noexcept { return rate; }

private:
    void reanchor (double tick) noexcept;

    TempoMap fallbackTempo;
    const TempoMap* tempo = &fallbackTempo;
    double rate = 48000.0;
    TimeSignature sig;
    bool playing = false;

    std::int64_t sampleTime = 0;
    std::int64_t anchorSample = 0;
    double anchorTick = 0.0;
    double anchorSeconds = 0.0;
};

} // namespace arranger

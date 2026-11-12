#pragma once

#include "arranger/core/Time.h"
#include "arranger/engine/AudioBlock.h"
#include "arranger/engine/MidiEventBuffer.h"

#include <cstdint>

namespace arranger
{

struct ProcessSpec
{
    double sampleRate = 48000.0;
    int maxBlockSize = 512;
};

/** Where the song is during one block. Built by the transport. */
struct ProcessContext
{
    bool playing = false;
    std::int64_t sampleTime = 0; ///< first sample of the block, counted since play started
    int numSamples = 0;
    double tickAtStart = 0.0;    ///< song position at the first sample
    double ticksPerSample = 0.0; ///< 0 when stopped
    double tempoBpm = 120.0;
    TimeSignature timeSignature;

    /** The tempo anchor: tick `anchorTick` falls exactly on sample `anchorSample`.
        Positions are always worked out from the anchor, never added up block by
        block, so timing can't drift however long the song plays. */
    std::int64_t anchorSample = 0;
    double anchorTick = 0.0;

    /** First sample (on the sampleTime clock) at or after the given song position. */
    std::int64_t sampleForTick (double tick) const noexcept;
};

/**
    Our own processor interface. The metronome, and later the SoundFont player,
    drum sampler, slicer and hosted plugins, all sit behind this one shape, so
    plugin hosting slots in without reworking the engine.

    process() runs on the audio thread: it must not allocate, lock or wait.
*/
class Processor
{
public:
    virtual ~Processor() = default;

    virtual void prepare (const ProcessSpec&) = 0;

    /** Add audio into `audio` and MIDI into `midiOut`. `midiIn` is read-only input. */
    virtual void process (const ProcessContext&, AudioBlock& audio,
                          const MidiEventBuffer& midiIn, MidiEventBuffer& midiOut) noexcept = 0;

    /** Silence any sound in flight (stop, locate). */
    virtual void reset() noexcept {}

    /** Extra latency this processor adds, in samples (for delay compensation). */
    virtual int latencySamples() const noexcept { return 0; }
};

} // namespace arranger

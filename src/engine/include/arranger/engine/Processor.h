#pragma once

#include "arranger/core/TempoMap.h"
#include "arranger/core/Time.h"
#include "arranger/engine/AudioBlock.h"
#include "arranger/engine/MidiEventBuffer.h"

#include <array>
#include <cstdint>

namespace arranger
{

struct ProcessSpec
{
    double sampleRate = 48000.0;
    int maxBlockSize = 512;
};

/** Where the song is during one block (or part of a block). Built by the transport. */
struct ProcessContext
{
    bool playing = false;
    std::int64_t sampleTime = 0; ///< first sample of this span, counted since play started
    int numSamples = 0;
    int blockOffset = 0;         ///< where this span starts inside the audio callback's buffer
    double sampleRate = 48000.0;
    double tickAtStart = 0.0;    ///< song position at the first sample
    double tickAtEnd = 0.0;      ///< song position just after the last sample
    double tempoBpm = 120.0;     ///< tempo at the start
    double ticksPerSample = 0.0; ///< at the start; 0 when stopped
    TimeSignature timeSignature;
    const TempoMap* tempo = nullptr;

    /** The anchor: tick `anchorTick` falls exactly on sample `anchorSample`.
        Positions are always worked out from the anchor through the tempo map,
        never added up block by block, so timing can't drift. */
    std::int64_t anchorSample = 0;
    double anchorTick = 0.0;
    double anchorSeconds = 0.0;

    /** First sample (on the sampleTime clock) at or after the given song position. */
    std::int64_t sampleForTick (double tick) const noexcept;
    /** Song position at a sample (on the sampleTime clock). */
    double tickAtSample (std::int64_t sample) const noexcept;
};

/** Where single MIDI channels of an instrument go instead of its main output. Null = the main output. */
struct ChannelOutputs
{
    std::array<float*, 16> left {}, right {};
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

    /** For an instrument that makes each MIDI channel apart (the SoundFont player): sends some
        channels to their own buffers in the next process() calls. Null turns it off. Audio thread. */
    virtual void setChannelOutputs (const ChannelOutputs*) noexcept {}

    /** Extra latency this processor adds, in samples (for delay compensation). */
    virtual int latencySamples() const noexcept { return 0; }
};

} // namespace arranger

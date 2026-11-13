#pragma once

#include "arranger/core/DeviceProfile.h"
#include "arranger/core/Sample.h"
#include "arranger/core/Song.h"
#include "arranger/engine/MidiEventBuffer.h"

#include <functional>
#include <utility>
#include <memory>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace arranger
{

/** One MIDI message at an absolute song position. */
struct SequenceEvent
{
    Tick at = 0;
    std::array<std::uint8_t, 3> bytes {};
    std::uint8_t size = 3;
    std::uint8_t order = 0; ///< at equal time: 0 note-off, 1 controls, 2 note-on
};

/** One Clip grid slot, flattened: events from 0 to `length`, which then repeats. */
struct SlotSequence
{
    ClipId clip = 0;
    Tick length = 0; ///< 0: empty slot
    std::vector<SequenceEvent> events; ///< sorted by (at, order); every `at` is below `length`
};

struct PlaybackTrack
{
    TrackId id = 0;
    std::uint16_t port = 0;     ///< see MidiEvent::port
    std::uint8_t channel = 0;   ///< 0..15
    bool audible = true;        ///< false when muted, or another track is soloed
    std::uint64_t contentKey = 0; ///< changes when events or routing change
    std::vector<SequenceEvent> setup;  ///< bank, program, volume, pan, sends: sent on play
    std::vector<SequenceEvent> events; ///< sorted by (at, order)
    std::vector<SlotSequence> slots;   ///< Clip grid; slots[scene]
};

/** One audio clip, ready to play: the audio in memory and its place in the song in seconds. */
struct AudioRegion
{
    TrackId track = 0;
    ClipId clip = 0;
    std::shared_ptr<const Sample> sample;
    Tick start = 0, end = 0;   ///< song position
    double startSeconds = 0.0; ///< of `start`, by the tempo map
    double endSeconds = 0.0;
    double offset = 0.0;       ///< seconds into the file at `start`
    double fadeIn = 0.0, fadeOut = 0.0; ///< seconds
    float gainLeft = 1.0f, gainRight = 1.0f; ///< clip gain, track volume and pan
};

/** The gain of a mixer strip on each side: volume 100 and pan 64 give 1 and 1. */
std::pair<float, float> stripGains (double volume, double pan) noexcept;

/** Gives the audio of a file, or null. Called on the UI thread while a snapshot is built. */
using SampleSource = std::function<std::shared_ptr<const Sample> (const std::string& file)>;

/** Gives the audio of a file at another speed with the same pitch, or null. `speed` is not 1. */
using StretchSource = std::function<std::shared_ptr<const Sample> (const std::string& file, double speed)>;

/**
    Everything the audio thread needs to play the song, flattened and sorted.
    Built on the UI thread from a Song, then handed over whole.
*/
struct PlaybackSnapshot
{
    TempoMap tempo;
    TimeSignature timeSignature;
    LoopRange loop;
    MetronomeSettings metronome;
    float masterGain = 1.0f;
    std::vector<PlaybackTrack> tracks;
    std::vector<std::string> ports; ///< hardware output names; ports[i] is MidiEvent port i + 1
    std::vector<TrackId> samplerTracks; ///< sampler and plugin tracks: samplerTracks[i] plays MidiEvent port kSamplerPortBase + i

    /** The level of a plugin track: its instrument has no volume of its own. */
    struct BusGain
    {
        TrackId track = 0;
        float left = 1.0f, right = 1.0f; ///< volume and pan; 0 when the track is not audible
    };
    std::vector<BusGain> busGains;

    /** Volume and pan automation of an audio or plugin track: the engine works out the level as the song moves.
        (Other tracks get their automation as MIDI controller messages in their events.) */
    struct GainLane
    {
        TrackId track = 0;
        AutomationLine volume, pan;
        float staticVolume = 100.0f, staticPan = 64.0f; ///< the mixer values, for a line with no points

        /** Left and right gain at a song position. */
        std::pair<float, float> at (double tick) const noexcept;
    };
    std::vector<GainLane> gainLanes;
    const GainLane* laneFor (TrackId) const noexcept;
    std::vector<AudioRegion> audio;     ///< the clips of the audible audio tracks, sorted by start

    /** The MidiEvent port of a sampler track, or kNoPort. */
    std::uint16_t samplerPort (TrackId) const noexcept;
};

/**
    Builds a snapshot. Hardware output names get port numbers in the order they
    are first used. Muted tracks, and all but soloed tracks, are marked inaudible.
*/
PlaybackSnapshot buildSnapshot (const Song&, const std::function<MixerControls (const std::string& port)>& mixerForPort = {},
                                const SampleSource& samples = {}, const StretchSource& stretched = {});

/** The CC and program messages that set a track's sound and mixer values.
    `controls` says which controller numbers the device uses (-1 = skip). */
std::vector<SequenceEvent> trackSetupMessages (const Track&, const MixerControls& controls = {});

} // namespace arranger

#pragma once

#include "arranger/core/DeviceProfile.h"
#include "arranger/core/Song.h"

#include <functional>

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

struct PlaybackTrack
{
    TrackId id = 0;
    std::uint16_t port = 0;     ///< see MidiEvent::port
    std::uint8_t channel = 0;   ///< 0..15
    bool audible = true;        ///< false when muted, or another track is soloed
    std::uint64_t contentKey = 0; ///< changes when events or routing change
    std::vector<SequenceEvent> setup;  ///< bank, program, volume, pan, sends: sent on play
    std::vector<SequenceEvent> events; ///< sorted by (at, order)
};

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
};

/**
    Builds a snapshot. Hardware output names get port numbers in the order they
    are first used. Muted tracks, and all but soloed tracks, are marked inaudible.
*/
PlaybackSnapshot buildSnapshot (const Song&, const std::function<MixerControls (const std::string& port)>& mixerForPort = {});

/** The CC and program messages that set a track's sound and mixer values.
    `controls` says which controller numbers the device uses (-1 = skip). */
std::vector<SequenceEvent> trackSetupMessages (const Track&, const MixerControls& controls = {});

} // namespace arranger

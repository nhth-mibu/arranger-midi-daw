#pragma once

#include "arranger/core/Arranging.h"

#include <string>
#include <utility>
#include <vector>

/**
    Hardware-aware orchestration: does the song fit the synths it plays on?

    The tracks are grouped by the device they play on (a MIDI port, or the
    app's SoundFont player). For each device the check counts the notes that
    sound at once (the sustain pedal holds notes), the parts (channels) in use,
    the note range and the controllers, against the device's capabilities
    (DeviceCapabilities: from its profile, or the numbers the user typed).

    Where the song asks for too much, it proposes musical compromises as
    proposals (Arranging.h): remove chord notes that other notes double, move
    a pad to the built-in synth, move notes into range. Bass, melody and drums
    are protected: they change only when nothing else helps.

    Voices are an estimate: a patch with two tones, layers or long release
    tails can use more. So the profiles keep conservative numbers.
*/
namespace arranger::hardware
{

/** One device that the song plays on. */
struct DeviceUse
{
    std::string key;       ///< the MIDI port name, or "soundfont" for the built-in player
    std::string name;      ///< "Roland D-05 (D-05 port)"
    std::string profileId;
    DeviceCapabilities capabilities;
    std::vector<TrackId> tracks;
    std::vector<int> channels; ///< in use, sorted
    int peakVoices = 0;
    Tick peakAt = 0;
    std::vector<std::pair<Tick, Tick>> overloaded; ///< spans with more voices than the polyphony
    std::vector<std::string> problems;             ///< plain sentences, worst first
};

struct Report
{
    std::vector<DeviceUse> devices;
    std::vector<arranging::Proposal> proposals; ///< ranked
    std::map<TrackId, arranging::Role> roles;
};

/** The built-in SoundFont player as a device: 16 parts, 256 voices. */
DeviceCapabilities soundFontCapabilities();

/** Notes that sound at once on a set of tracks: the most, and where. The sustain pedal (CC 64) holds notes. */
std::pair<int, Tick> peakVoices (const std::vector<const Track*>&);

/** Checks the song against its devices and proposes repairs. */
Report check (const Song&, const DeviceLibrary&, const arranging::Preferences* = nullptr, TrackId melody = 0);

} // namespace arranger::hardware

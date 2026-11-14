#pragma once

#include "arranger/core/DeviceProfile.h"
#include "arranger/core/Song.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

/**
    Songs for the Yamaha PSR-S970's song player: a Standard MIDI File (format 0)
    that plays on the keyboard's own voices. Tracks that already play on the
    keyboard keep their voices. Other tracks get the nearest Yamaha voice: the
    same name, the same General MIDI sound, or a sound found from words in the
    name ("pad", "bass", ...). Drums get a drum kit.
*/
namespace arranger::psr_export
{

struct Error : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/** How one track goes into the file. */
struct Part
{
    std::size_t track = 0; ///< index in song.tracks
    int channel = 1;       ///< 1..16
    TrackSound voice;      ///< a voice of the keyboard
    std::string how;       ///< "kept", "same name", "same GM sound", "from the name", "drum kit"
};

struct Plan
{
    std::vector<Part> parts;
    std::vector<std::string> skipped; ///< "Vocals: an audio track"
};

/** The profile id of the keyboard. */
inline constexpr const char* kProfileId = "yamaha-psr-s970";

/** Chooses a channel and a voice for each track. Throws Error when the library has no PSR-S970 profile. */
Plan plan (const Song&, const DeviceLibrary&);

/**
    The song file. It starts with XG System On and one bar where the keyboard
    gets its voices; the music starts at bar 2. Lyrics go in as lyric events.
*/
std::vector<std::uint8_t> write (const Song&, const Plan&);

/** The General MIDI program (0..127) that words in a sound name suggest; -1 when none do. */
int gmProgramFromName (const std::string& name);

} // namespace arranger::psr_export

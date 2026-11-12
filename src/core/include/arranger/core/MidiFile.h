#pragma once

#include "arranger/core/Song.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <vector>

namespace arranger::midi_file
{

struct Error : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/**
    Reads a Standard MIDI File (format 0 or 1) into a new song. Each MIDI
    channel used in each file track becomes one of our tracks with one clip.
    Bank and program changes at the start become the track's sound; tempo
    changes become the tempo map. Throws Error.
*/
Song read (std::span<const std::uint8_t> bytes);
Song load (const std::filesystem::path&);

/** Writes a format 1 file at 960 ticks per quarter: a tempo track, then one track per song track. */
std::vector<std::uint8_t> write (const Song&);
void save (const Song&, const std::filesystem::path&);

} // namespace arranger::midi_file

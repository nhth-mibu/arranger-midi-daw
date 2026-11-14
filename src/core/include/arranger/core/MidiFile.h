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

struct WriteOptions
{
    bool singleTrack = false; ///< format 0: all events in one track
    std::vector<std::vector<std::uint8_t>> startSysEx; ///< whole messages (F0 ... F7), at tick 0
    Tick setupAt = 0;         ///< where each track's bank, program and mixer values go
    Tick offset = 0;          ///< the music starts this much later
    bool lyrics = false;      ///< the tracks' lyric marks as lyric events
    bool automation = false;  ///< volume, pan, reverb and chorus lines as CC 7, 10, 91 and 93
    bool xfChords = false;    ///< the Score's chord symbols as Yamaha XF chord events
};
std::vector<std::uint8_t> write (const Song&, const WriteOptions&);

/** A chord symbol ("F#m7", "C/E", "N.C.") as a Yamaha XF chord meta event; empty when the text is no chord. */
std::vector<std::uint8_t> xfChord (const std::string& chordName);
void save (const Song&, const std::filesystem::path&);

} // namespace arranger::midi_file

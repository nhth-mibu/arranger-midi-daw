#pragma once

#include "arranger/core/Time.h"

#include <string>
#include <vector>

namespace arranger
{

/** Track colours are names from the theme palette, so a theme can restyle them. */
inline constexpr const char* kTrackColours[] = { "coral", "amber", "teal", "blue", "violet", "green", "pink" };

struct Track
{
    std::string name = "Track";
    std::string colour = "teal";
    int midiChannel = 1; ///< 1..16

    bool operator== (const Track&) const = default;
};

struct MetronomeSettings
{
    bool enabled = true;
    float level = 0.7f;       ///< 0..1
    bool accentDownbeat = true;

    bool operator== (const MetronomeSettings&) const = default;
};

/**
    The song: everything that is saved in a project file.

    Phase 0 holds tempo, meter, key, a plain track list and metronome settings.
    Clips, notes and the tempo map arrive in Phase 1.
*/
struct Song
{
    std::string title = "Untitled";
    double tempoBpm = 120.0;
    TimeSignature timeSignature;
    std::string key = "C major";
    std::vector<Track> tracks;
    MetronomeSettings metronome;

    bool operator== (const Song&) const = default;
};

} // namespace arranger

#pragma once

#include "arranger/core/Sample.h"
#include "arranger/core/Song.h"

#include <vector>

/**
    Audio to MIDI: finds notes in recorded audio.

    `melody` follows one note at a time (a voice, a bass, a lead line) with
    the YIN pitch method. `drums` finds hits with the transient detector and
    sorts them into kick, snare and hi-hat by how bright each hit is. Chords
    are out of reach for both.
*/
namespace arranger::audio_to_midi
{

struct Found
{
    double start = 0.0;  ///< seconds from the start of the file
    double length = 0.0; ///< seconds
    int pitch = 60;
    int velocity = 100;

    bool operator== (const Found&) const = default;
};

struct MelodySettings
{
    int lowestPitch = 28;    ///< E1
    int highestPitch = 96;   ///< C7
    double minNoteSeconds = 0.06;
    double clarity = 0.2;    ///< YIN threshold: lower takes only very clear notes
};

std::vector<Found> melody (const Sample&, const MelodySettings& = {});

struct DrumSettings
{
    double sensitivity = 0.5;
    int kick = 36, snare = 38, hat = 42; ///< General MIDI drum notes
};

std::vector<Found> drums (const Sample&, const DrumSettings& = {});

/** The notes that are inside an audio clip, as a MIDI clip at the same place in the song. */
Clip toClip (const std::vector<Found>&, const Song&, const AudioClip&);

} // namespace arranger::audio_to_midi

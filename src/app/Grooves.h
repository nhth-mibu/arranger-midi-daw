#pragma once

#include "arranger/core/Groove.h"
#include "arranger/core/Song.h"

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

/** The user's groove library (a folder of .groove files) and the hits in audio for grooves. */
namespace arranger::grooves
{

/** <app data>/Arranger MIDI DAW/Grooves */
juce::File folder();

/** The user's saved grooves, by name. */
std::vector<groove::Groove> load();
/** Saves a groove (a file with the same name is replaced). */
void save (const groove::Groove&);
/** The built-in grooves (at the song tempo) and the user's own after them. */
std::vector<groove::Groove> library (double bpm);

/** The hits in an audio file, in seconds from its start. Throws when the file can't be read. */
std::vector<groove::Hit> hitsInFile (const std::string& path);
/** The hits in an audio clip of the song, in seconds from the start of the song. Throws when the file can't be read. */
std::vector<groove::Hit> hitsInClip (const Song&, ClipId);

} // namespace arranger::grooves

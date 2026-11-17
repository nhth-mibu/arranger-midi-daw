#pragma once

#include "arranger/core/Sample.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace arranger::audio_files
{

/** The audio files that the app reads: WAV, AIFF, FLAC and Ogg Vorbis on every system,
    and the formats of the system (MP3, M4A on macOS; MP3, WMA on Windows).
    Video containers use the first audio stream, decoded by a separate FFmpeg executable. */
juce::String wildcard();       ///< for a file chooser: "*.wav;*.aif;..."
juce::String formatNames();    ///< for messages: "WAV, AIFF, FLAC, Ogg Vorbis, ..."
bool isAudioFile (const juce::File&);
bool isVideoFile (const juce::File&);

/** Reads a whole file into memory. Throws wav::Error when the file can't be read. */
Sample read (const std::string& path);

} // namespace arranger::audio_files

#pragma once

#include "arranger/core/AudioToMidi.h"
#include "arranger/core/Sample.h"
#include "arranger/core/Song.h"
#include "arranger/core/StyleChord.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

/**
    Audio analysis: takes a recording apart into what the app can edit.

    The heavy models run outside the core. A helper program (Python) splits
    the audio into stems (Demucs), finds the notes of each stem (Basic Pitch)
    and names the instruments in it (PANNs), and writes what it found as JSON.
    Whisper (whisper.cpp) writes down the words. This part reads those
    results, finds the chords itself, and puts everything into the song:
    one audio track and one MIDI track per stem, chord symbols, and lyrics
    under the notes of the vocal track.
*/
namespace arranger::analysis
{

/** An instrument or sound that a model heard, with how sure it is (0..1). */
struct Tag
{
    std::string label; ///< an AudioSet name, e.g. "Electric guitar"
    float score = 0.0f;

    bool operator== (const Tag&) const = default;
};

/** A sung or spoken word, with times in seconds from the start of the file. */
struct Word
{
    double start = 0.0;
    double end = 0.0;
    std::string text;
    float probability = 1.0f;

    bool operator== (const Word&) const = default;
};

/** A chord between two times (seconds from the start of the file). */
struct TimedChord
{
    double start = 0.0;
    double end = 0.0;
    Chord chord;

    bool operator== (const TimedChord&) const = default;
};

/** One part of the recording: "vocals", "drums", "bass", "other", "guitar", "piano", or "mix" (all of it). */
struct Stem
{
    std::string name;
    std::string file;  ///< a WAV file; empty for "mix" (the source file)
    bool hasNotes = false;
    std::vector<audio_to_midi::Found> notes;
    std::vector<Tag> tags; ///< best first
};

/** A place the user listened to and answered: its chord is locked (see Clarify.h). */
struct Answer
{
    double start = 0.0;
    double end = 0.0;
    Chord chord;

    bool operator== (const Answer&) const = default;
};

/** Everything found in one audio file. */
struct Result
{
    std::string source;              ///< the analysed file
    std::vector<Stem> stems;
    std::vector<TimedChord> chords;
    std::vector<Word> words;
    std::string language;            ///< of the words, e.g. "en"
    std::vector<std::string> warnings;
    std::vector<Answer> answers;     ///< places the user answered (Clarify.h)

    const Stem* stem (const std::string& name) const noexcept;
};

/** Reads the JSON that the helper program writes (and toJson writes). Throws std::runtime_error. */
Result parse (const std::string& json);
std::string toJson (const Result&);

// ---- chords -------------------------------------------------------------------------

/** How strongly each of the 12 pitch classes (0 = C) sounds, frame by frame. */
struct Chroma
{
    double hopSeconds = 0.1;
    std::vector<std::array<float, 12>> frames; ///< all notes
    std::vector<std::array<float, 12>> bass;   ///< low notes only (for the bass note of a chord)
};

Chroma chroma (const Sample&);

/**
    Finds the chords in audio: major, minor, 7, maj7, m7, sus4 and dim
    chords, with a bass note when it is clearly not the root. A chord must
    stay for `minSeconds` to count. Drums and voices disturb it: give it the
    stems without them when there are stems.
*/
std::vector<TimedChord> findChords (const Sample&, double minSeconds = 0.5);

/**
    How well a chord fits one chroma (0..1, the score the chord finder uses).
    0 for chord types the finder does not know (it knows major, minor, 7,
    maj7, m7, sus4 and dim).
*/
float chordFit (const std::array<float, 12>& chroma, const Chord&);

/** The chord types the chord finder knows. */
std::vector<ChordType> findableChordTypes();

/** All the stems added together, for the chords: drums and vocals left out. Empty when no stem is left. */
Sample harmonicMix (const std::vector<const Sample*>&);

// ---- words --------------------------------------------------------------------------

/** One channel at a new sample rate (Whisper wants 16 kHz). */
std::vector<float> resampleMono (const Sample&, double rate);

/** The words as lines of text: a new line after a pause of `gapSeconds` or more. For the AI to read and correct. */
std::vector<std::string> lines (const std::vector<Word>&, double gapSeconds = 1.2);

// ---- instruments --------------------------------------------------------------------

/** The General MIDI sound for a stem, from its tags and its name. */
struct Instrument
{
    int program = 0;     ///< 0..127
    bool drums = false;  ///< channel 10
    std::string name;    ///< what was heard, for the track name

    bool operator== (const Instrument&) const = default;
};

Instrument instrumentFor (const Stem&);

/** The General MIDI program for an AudioSet label; -1 when it names no instrument. */
int programForLabel (const std::string& label);

// ---- into the song ------------------------------------------------------------------

struct ApplyOptions
{
    bool stemTracks = true;  ///< an audio track per stem
    bool noteTracks = true;  ///< a MIDI track per stem with notes
    bool chords = true;      ///< chord symbols on the first new MIDI track (or the vocal track)
    bool lyrics = true;      ///< the words under the notes of the vocal track
    bool muteSource = true;  ///< mute the track of the analysed clip when stem tracks are added
};

struct Applied
{
    std::vector<TrackId> audioTracks;
    std::vector<TrackId> midiTracks;
    TrackId chordTrack = 0;
    TrackId lyricsTrack = 0;
    int chords = 0;
    int words = 0;
};

/** The song position of a time in the file of an audio clip (as audio_to_midi::toClip places notes). */
Tick tickInSong (const Song&, const AudioClip&, double fileSeconds);

/**
    Puts what was found in the audio clip `source` into the song, after the
    clip's track. Each new track gets the same place in time as the clip.
    Throws std::runtime_error when the clip is not there.
*/
Applied apply (Song&, ClipId source, const Result&, const ApplyOptions& = {});

/**
    Puts words on the notes of a clip: each word goes to the first free note
    that starts near it. Words with no note near them get a note of their own
    (pitch `fallbackPitch`) so that no word is lost. Returns the lyric marks.
*/
std::vector<ScoreMark> placeWords (Clip& clip, const std::vector<std::pair<Tick, Word>>& words, Tick slack, int fallbackPitch = 67);

} // namespace arranger::analysis

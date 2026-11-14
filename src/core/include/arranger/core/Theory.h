#pragma once

#include "arranger/core/StyleChord.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/**
    Music theory: keys and modes, scales, note spelling, key finding, roman
    numerals and voice-leading checks. Many ideas come from the music21
    toolkit (MIT, BSD licence): the Krumhansl-Schmuckler key finder, keys
    found in windows of bars, roman numeral figures and the voice-leading
    rules of its VoiceLeadingQuartet. The code here is our own. Plain C++, no
    JUCE.
*/
namespace arranger::theory
{

enum class Mode : std::uint8_t { major, minor, dorian, phrygian, lydian, mixolydian, locrian };

/** A key: its tonic, its mode and the key signature that spells it. */
struct Key
{
    int tonic = 0;           ///< 0 = C ... 11 = B
    Mode mode = Mode::major;
    int fifths = 0;          ///< key signature: sharps (+) or flats (-), -7 .. 7

    bool minor() const noexcept { return mode == Mode::minor; }
    bool operator== (const Key&) const = default;
};

/** A key with the signature that has the fewest accidentals (F# major, not Gb major; Eb minor, not D# minor). */
Key makeKey (int tonic, Mode);

/**
    Reads a key: "Eb major", "F# minor", "Am", "C", "D dorian", "Bb mixolydian".
    The spelling of the tonic is kept when it is a real key (C# major has 7 sharps).
    Empty when the text is no key.
*/
std::optional<Key> parseKey (std::string_view);

/** "Eb major", "F# minor", "D dorian". */
std::string name (const Key&);

/** The seven notes of the key's mode as semitones above the tonic. */
std::array<int, 7> modeSteps (Mode);

/**
    Note spelling on the line of fifths: ... Bb=-2 F=-1 C=0 G=1 D=2 A=3 E=4 B=5 F#=6 ...
    A pitch class gets the place nearest to a centre; the key gives the centre.
    These turn a place into a letter (0 = C ... 6 = B) and an alteration.
*/
int fifthsPlace (int pitchClass, int centre);
int letterOf (int place);
int alterOf (int place);

/** The centre of the key on the line of fifths: notes near it are the key's own spellings. */
int centre (const Key&);

/** A pitch class spelled in a key: "F#", "Gb", "Bb", "Cb" (in Gb major). */
std::string noteName (int pitchClass, const Key&);

/** "C#", "Bb", "F##" for a place on the line of fifths. */
std::string placeName (int place);

/** A chord name with its root and bass spelled in the key ("Gb" in Db major, not "F#"). */
std::string chordName (const Chord&, const Key&);

// ---- scales -------------------------------------------------------------------------------

enum class Scale : std::uint8_t
{
    major, naturalMinor, harmonicMinor, melodicMinor, dorian, phrygian, lydian, mixolydian, locrian,
    majorPentatonic, minorPentatonic, blues, wholeTone, chromatic,
};

inline constexpr int kScaleCount = 14;

/** "major", "harmonic minor", "minor pentatonic" ... */
std::string name (Scale);

/** Reads a scale name; "minor" and "aeolian" are the natural minor, "ionian" is major. */
std::optional<Scale> parseScale (std::string_view);

/** The scale's notes as semitones above the tonic. */
std::vector<int> intervals (Scale);

/** The scale's notes from the tonic, spelled: each note of a seven-note scale gets its own letter. */
std::vector<std::string> spellScale (int tonic, Scale, const Key& context);

// ---- key finding ---------------------------------------------------------------------------

/** How much each pitch class sounds in the notes from `from` to `to` (note length, a little more for loud notes). */
std::array<double, 12> pitchWeights (const std::vector<Note>&, Tick from, Tick to);

struct KeyGuess
{
    Key key;
    double score = 0.0; ///< correlation with the key's profile, -1 .. 1
};

/** All 24 major and minor keys, the best match first (Krumhansl-Schmuckler, Krumhansl-Kessler profiles). */
std::vector<KeyGuess> rankKeys (std::span<const double, 12> weights);

/** The best key, or empty when there are too few notes. */
std::optional<KeyGuess> findKey (const std::vector<Note>&, Tick from, Tick to);

/** How sure the best guess is, 0 .. 1: how far it is ahead of the second (music21 calls this tonal certainty). */
double certainty (const std::vector<KeyGuess>&);

struct KeyRegion
{
    Tick start = 0, end = 0;
    Key key;
    double score = 0.0;
};

/**
    The keys of a song, part by part: each bar gets the best key of a window of
    `windowBars` bars around it, and short changes (one bar) are smoothed out.
*/
std::vector<KeyRegion> keyRegions (const std::vector<Note>&, Tick from, Tick to, Tick barLength, int windowBars = 4);

// ---- roman numerals ------------------------------------------------------------------------

/**
    The roman numeral of a chord in a key: "I", "ii7", "V65" (an inversion by
    its figured bass), "vii°7", "iiø7", "bVII", "V7/V" (a secondary dominant),
    "Imaj7". Extended chords keep their suffix ("V9", "IVadd9", "Vsus4").
*/
std::string roman (const Chord&, const Key&);

/**
    The chord of a roman numeral in a key: "ii7", "V7/V", "bVI", "vii°7",
    "I64", "IVmaj7", "V9", "N6" (the Neapolitan). Upper case is a major chord,
    lower case a minor chord. In minor, "vii" is on the leading note and "VII"
    on the note below it. Empty when the text is no numeral.
*/
std::optional<Chord> fromRoman (std::string_view, const Key&);

/** The chords of a progression like "I vi IV V" or "ii7 - V7 - Imaj7" (spaces, commas, dashes or bars between). */
std::vector<Chord> progression (std::string_view, const Key&);

/** The chord on each note of the key's scale (triads, or seventh chords). */
std::vector<Chord> diatonicChords (const Key&, bool sevenths);

// ---- voice leading -------------------------------------------------------------------------

struct Voice
{
    std::string name;
    std::vector<Note> notes; ///< one note at a time, song positions
};

/**
    Splits the notes of one track into voices, top voice first: a melody is
    one voice, a four-note chord part is four. `maxVoices` caps the count.
*/
std::vector<Voice> splitVoices (const std::string& name, const std::vector<Note>&, int maxVoices = 4);

struct VoiceIssue
{
    enum class Kind : std::uint8_t { parallelFifths, parallelOctaves, hiddenFifths, hiddenOctaves, voiceCrossing, largeLeap };

    Kind kind = Kind::parallelFifths;
    Tick at = 0;
    int upper = 0, lower = 0; ///< voice indexes (the same for a leap)
    std::string text;
};

/** "parallel fifths", "voice crossing" ... */
std::string name (VoiceIssue::Kind);

/**
    The common-practice voice-leading faults between the voices, in time
    order: parallel fifths and octaves, hidden fifths and octaves in the outer
    voices (when the top voice leaps), voices that cross, and leaps of more
    than an octave. At most `limit` issues.
*/
std::vector<VoiceIssue> checkVoiceLeading (const std::vector<Voice>&, Tick from, Tick to, std::size_t limit = 200);

} // namespace arranger::theory

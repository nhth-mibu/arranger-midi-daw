#pragma once

#include "arranger/core/Song.h"

#include <cstdint>
#include <string>
#include <vector>

/**
    Notation: turns the notes of a track into something a score can show.
    Played notes are never exact, so this quantises them for display only
    (the notes in the song do not change), splits them into bars and
    readable note values with ties, spells the pitches in the key, and
    groups beams. It knows nothing about drawing; see ScoreLayout.h.
*/
namespace arranger::notation
{

enum class Clef : std::uint8_t { treble, bass, percussion };

/** A written accidental. */
enum class Accidental : std::int8_t { none, doubleFlat, flat, natural, sharp, doubleSharp };

/** A note value: whole, half, quarter, eighth, 16th, 32nd. */
enum class Value : std::uint8_t { whole, half, quarter, eighth, sixteenth, thirtySecond };

/** How many beams (or flags) a value has: 0 for quarters and longer. */
int beamCount (Value) noexcept;

/** A pitch as written: a letter, its alteration and octave (C4 = middle C). */
struct Spelling
{
    int step = 0;   ///< 0 = C, 1 = D ... 6 = B
    int alter = 0;  ///< -2 .. 2 semitones
    int octave = 4;

    bool operator== (const Spelling&) const = default;
};

/** One notehead of a chord. */
struct Head
{
    int pitch = 60;           ///< MIDI note
    Spelling spelling;
    int position = 0;         ///< staff position in half spaces: 0 = bottom line, 8 = top line
    Accidental accidental = Accidental::none; ///< what to print; none when the bar already says it
    bool tieToNext = false;   ///< tied to the same note in the next event of this staff
    bool cross = false;       ///< an X notehead (cymbals and hi-hats on a drum staff)

    bool operator== (const Head&) const = default;
};

/** A note, chord or rest in a bar. */
struct Event
{
    Tick start = 0;          ///< from the start of the bar
    Tick length = 0;         ///< written length
    Value value = Value::quarter;
    int dots = 0;
    bool triplet = false;    ///< plays 2/3 of its written value
    bool rest = false;
    bool wholeBarRest = false;
    std::vector<Head> heads; ///< lowest first; empty for a rest
    bool stemUp = true;
    int beamGroup = -1;      ///< events with the same number (in a bar) share beams
    int tupletGroup = -1;    ///< events with the same number share a "3" bracket

    bool operator== (const Event&) const = default;
};

struct Bar
{
    Tick start = 0;          ///< song position
    Tick length = 0;
    TimeSignature time;
    std::vector<Event> events;
};

struct Staff
{
    Clef clef = Clef::treble;
    std::vector<Bar> bars;
};

/** One track as notation: one staff, or two (a grand staff) for piano music. */
struct Part
{
    std::string name;
    int keyFifths = 0;       ///< key signature: sharps (+) or flats (-)
    TimeSignature time;
    std::vector<Staff> staves;
    bool grandStaff = false;
    bool drums = false;
};

struct Options
{
    enum class Staves : std::uint8_t { automatic, single, grand };

    Tick grid = kTicksPerQuarter / 4;  ///< smallest written value when not in triplets
    bool triplets = true;              ///< use eighth triplets where the notes fit them better
    Staves staves = Staves::automatic;
    int split = 60;                    ///< on a grand staff, notes from here up go to the upper staff
    bool drums = false;                ///< drum notation (GM drum map)
};

/** Sharps (+) or flats (-) of a key name like "Eb major" or "F# minor". */
int keyFifths (const std::string& keyName);

/** How a pitch is spelled in a key. */
Spelling spell (int pitch, int keyFifths);

/**
    The notes of a track as notation, from bar 1 to `end` (rounded up to a
    whole bar). `notes` have song positions (not clip positions).
*/
Part makePart (const std::string& name, const std::vector<Note>& notes, Tick end, TimeSignature, int keyFifths, const Options& = {});

/** The notes a track plays on the Timeline, with song positions. Loop repeats are written out. */
std::vector<Note> timelineNotes (const Track&);

/** True for tracks that are drums: MIDI channel 10, or the drum sampler. */
bool isDrumTrack (const Track&);

} // namespace arranger::notation

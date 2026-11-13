#pragma once

#include "arranger/core/Style.h"

#include <optional>
#include <span>
#include <string>

namespace arranger
{

/** Yamaha's chord types, in the order style files number them. */
enum class ChordType : std::uint8_t
{
    major, major6, major7, major7Sharp11, majorAdd9, major7_9, major6_9, augmented,
    minor, minor6, minor7, minor7Flat5, minorAdd9, minor7_9, minor7_11, minorMajor7, minorMajor7_9,
    diminished, diminished7,
    seventh, seventhSus4, seventhFlat5, seventh9, seventhSharp11, seventh13, seventhFlat9, seventhFlat13, seventhSharp9,
    major7Augmented, seventhAugmented,
    octave, fifth, sus4, sus2,
    cancel,
};

inline constexpr int kChordTypeCount = 35;

/** A chord for the style to follow. */
struct Chord
{
    int root = 0;                       ///< 0 = C ... 11 = B
    ChordType type = ChordType::major;
    int bass = -1;                      ///< bass note of an on-bass chord (C/E), -1 = the root

    bool operator== (const Chord&) const = default;
};

namespace style_chord
{
    /** "C", "F#m7", "Bb7sus4", "C/E". */
    std::string name (const Chord&);

    /** The chord's notes as semitones above the root, lowest first (the root is 0). */
    std::vector<int> tones (ChordType);

    /**
        The chord for the keys held in the chord area of the keyboard
        (Yamaha's "fingered on bass" way): three or more keys name a chord,
        one key is a root with its octave, a root and its fifth is 1+5. The
        lowest key is the bass note. Empty when the keys make no known chord.
    */
    std::optional<Chord> detect (std::span<const int> pitches);

    /**
        The chord that a passage is in: `weight[n]` is how much note n
        (0 = C) sounds, `bass` is the note the bass plays most (-1 = not
        known). Empty when there are too few notes to tell.
    */
    std::optional<Chord> guess (std::span<const double, 12> weight, int bass);

    /** False when the rule's note mute or chord mute silences the channel for this chord. */
    bool plays (const StyleChannelRule&, const Chord&);

    /**
        Moves one recorded note to the chord being played, by the channel's
        rule. Empty when the note must not sound (chord cancel). This follows
        Yamaha's published rules; the keyboard itself has the last word.
    */
    std::optional<int> transpose (int pitch, const StyleChannelRule&, const Chord&);
}

} // namespace arranger

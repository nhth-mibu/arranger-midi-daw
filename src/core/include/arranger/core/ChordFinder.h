#pragma once

#include "arranger/core/StyleChord.h"

#include <array>
#include <optional>
#include <vector>

namespace arranger::chords
{

/**
    Finds the chords that the notes of some tracks make, for chord symbols on
    the score. Drum tracks are left out. The time from `from` to `to` is cut in
    steps (a bar, half a bar or a beat, from the bar lines); each step gets
    the chord of its notes, and a mark is made only where the chord changes.
    When the lowest note is a chord note but not the root, the chord gets a
    bass ("C/E").
*/
std::vector<ScoreMark> find (const std::vector<const Track*>&, const TimeSignature&, Tick from, Tick to, Tick step);

/** Replaces the chord symbols of a track from `from` to `to` with new ones (other marks stay). */
void replaceChords (Track&, Tick from, Tick to, const std::vector<ScoreMark>& chords);

} // namespace arranger::chords

namespace arranger::guitar
{

inline constexpr int kMuted = -1;

/** A chord on a six-string guitar in standard tuning (E A D G B E). */
struct Shape
{
    std::array<int, 6> frets {}; ///< low E string first: the fret, 0 = open, kMuted = not played
    int firstFret = 1;           ///< the fret at the top of the diagram (1: the nut shows)

    bool operator== (const Shape&) const = default;
};

/** A playable shape for a chord: an open chord when there is a usual one, else a barre chord as low on the neck as it goes. */
std::optional<Shape> shape (const Chord&);

/** The notes (0 = C) a shape plays, low string first; muted strings are left out. For tests. */
std::vector<int> notes (const Shape&);

} // namespace arranger::guitar

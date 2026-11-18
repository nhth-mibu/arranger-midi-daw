#pragma once

#include "arranger/core/AudioAnalysis.h"
#include "arranger/core/Style.h"
#include "arranger/core/StyleChord.h"

#include <functional>
#include <string>
#include <vector>

/**
    Style from Audio: makes an arranger style (PSR-S970, SFF2) from a song
    that the audio analysis took apart (stems with notes, and chords).

    1. The bar lines: the tempo and the first downbeat, from the drum hits
       (or the bass when there are no drums). The tempo stays the same in
       the whole song.
    2. The sections: the song in phrases of 4 (or 2) bars, grouped by how
       the drums play and how busy the parts are. The quietest group is
       Main A, the busiest Main D. The first phrase is the intro, the last
       one the ending. Bars where the drums break the pattern at a phrase
       end are fills.
    3. The parts: drums to Rhythm 2, bass to Bass, piano and guitar to
       Chord 1 and Chord 2, the other stem to the pad (or Chord 1). Each
       note moves to the CMaj7 source chord from the chord it was played
       over, so that the keyboard can move it to any chord again.
    4. Compare and refine: the style plays the song's own chords bar by bar,
       and that is compared with the notes of the stems (drum hits and their
       timing, bass notes, chord notes, how many notes per bar). Then the
       settings of each part change one at a time (which bars it comes from,
       the pattern length, notes kept only when most phrases play them,
       quantise, the triplet grid, clean-up of weak notes) and a change stays
       when the score goes up. It stops when no change helps.

    It matches notes and rhythm, not sound: the user picks the voices.
    Nothing here uses JUCE or plays audio; the app plays the A/B preview.
*/
namespace arranger::style_from_audio
{

// ---- the bar lines ---------------------------------------------------------------

struct Grid
{
    double bpm = 120.0;
    double firstDownbeat = 0.0; ///< seconds into the file where bar 1 starts
    int beatsPerBar = 4;
    double confidence = 0.0;    ///< 0..1: how well the hits fall on the beats

    double beatSeconds() const noexcept { return 60.0 / bpm; }
    double barSeconds() const noexcept { return beatSeconds() * beatsPerBar; }
    /** Seconds in the file to ticks from bar 1 (960 a beat). Before bar 1 it is negative. */
    double tickAt (double seconds) const noexcept;
    double secondsAt (double tick) const noexcept;
};

/** The note times (seconds) that the bar lines come from: drum hits, else bass notes, else all notes. With weights (kick counts most). */
struct Onset
{
    double seconds = 0.0;
    double weight = 1.0;
    bool low = false;    ///< a kick or a bass note: these mark the downbeat
    bool change = false; ///< a chord change: marks the downbeat, not the tempo
};
std::vector<Onset> onsets (const analysis::Result&);

/**
    Finds the tempo and bar 1 from note onsets (bar 1: the first downbeat
    at or just before the first note). `nearBpm` (0: about 110)
    breaks the tie between half and double tempo. `bpm` more than 0: the
    tempo is known, only the bar lines are found.
*/
Grid findGrid (const std::vector<Onset>&, int beatsPerBar = 4, double nearBpm = 0.0, double bpm = 0.0);

// ---- the parts ---------------------------------------------------------------------

/** A stem that becomes a style part. */
struct PartSource
{
    std::string stem;  ///< drums, bass, piano, guitar, other, vocals
    int channel = 9;   ///< 9..16 (StylePart)
    bool drums = false;
    analysis::Instrument instrument;
};

/** Which stem plays which style part. Vocals become Phrase 1 only when `vocals` is true. */
std::vector<PartSource> partsFor (const analysis::Result&, bool vocals = false);

/** A note of a stem in song time: ticks from bar 1 (960 a beat). */
struct Hit
{
    double tick = 0.0;
    double length = 240.0;
    int pitch = 60;
    int velocity = 100;
};

/**
    Moves a note played over `chord` to what it would be over CMaj7 (the
    source chord of a style), near where it was: C major 3rds for minor
    3rds, major 7ths for minor 7ths, and so on. `bassPart`: follow the bass
    note of an on-bass chord (C/E) as the root.
*/
int toSourceChord (int pitch, const Chord& chord, bool bassPart = false);

// ---- the plan: which section plays in which bar --------------------------------------

struct Plan
{
    int bars = 0;                       ///< bars of the song from bar 1
    int phraseBars = 4;
    std::vector<std::string> barSection; ///< by bar: "Main A", "Fill In BB", "Intro A", "Ending A"
    std::vector<int> barGroup;          ///< by bar: 0..3 for Main A..D, -1 intro, -2 ending
    std::vector<Chord> beatChords;      ///< by beat from bar 1
    std::vector<std::string> sections;  ///< in Yamaha's order
};

// ---- the score ---------------------------------------------------------------------

struct PartScore
{
    int channel = 9;
    std::string name;   ///< "Bass"
    double onsets = 0.0, timing = 0.0, notes = 0.0, density = 0.0; ///< each 0..1
    double total = 0.0; ///< 0..1
    int bars = 0;       ///< the bars it was compared in
};

struct SectionScore
{
    std::string section;
    std::vector<PartScore> parts;
    double total = 0.0; ///< 0..1
    int bars = 0;
};

struct Score
{
    double total = 0.0; ///< 0..1, bars weighted
    std::vector<SectionScore> sections;
};

// ---- making it ---------------------------------------------------------------------

/** What the refine loop can change, for one part of one section. */
struct PartSettings
{
    int window = 0;      ///< which candidate bars the pattern comes from
    int consensus = 0;   ///< 0: one place only. 50 or 34: notes that at least that % of the places play
    int quantise = 50;   ///< % towards the grid: 0, 50, 100
    int grid = 4;        ///< steps a beat: 4 (16ths) or 3 (triplets)
    int cleanup = 1;     ///< 0 keep all, 1 drop very weak and short notes, 2 drop more
    bool operator== (const PartSettings&) const = default;
};

struct Options
{
    std::string name;       ///< of the style; from the file name when empty
    double bpm = 0.0;       ///< more than 0: the tempo is known
    int beatsPerBar = 4;
    int mainBars = 0;       ///< the length of the main sections: 1, 2 or 4 bars; 0: the loop chooses
    bool vocals = false;    ///< the vocal line as Phrase 1
    int maxRounds = 6;      ///< rounds of the refine loop; 0: no refining
    std::function<bool (double progress, const std::string& step)> progress; ///< false stops the loop (the best so far is kept)
};

/** One step of the refine loop that made the score better. */
struct Step
{
    int round = 0;
    std::string change; ///< "Main A, Bass: 4 bars of consensus"
    double before = 0.0, after = 0.0;
};

struct Made
{
    Style style;
    Grid grid;
    Plan plan;
    std::vector<PartSource> parts;
    Score first;            ///< before refining
    Score best;
    std::vector<Step> steps;
    int tried = 0;          ///< versions the loop compared
    std::vector<std::string> warnings;
    std::string source;     ///< the audio file
};

/** Makes the style. Throws std::runtime_error when the analysis has no notes to make one from. */
Made make (const analysis::Result&, const Options& = {});

/** Compares a style with the stems of the song, played with the song's chords by the plan. */
Score compare (const Style&, const analysis::Result&, const Grid&, const Plan&, const std::vector<PartSource>&);

/** What preview() puts in the song. */
enum class Listen
{
    original, ///< the recording only
    style,    ///< the style only
    both      ///< both, the recording a little lower
};

/**
    A song for listening: the tempo of the grid, the recording on an audio
    track that starts at bar 1, and the style playing the song's chords bar
    by bar on MIDI tracks (General MIDI sounds). For A/B with Session::audition.
*/
Song preview (const Made&, Listen = Listen::both);

/** "Main A" -> its bars in the song, for "bars 9-16, 25-32". */
std::string barsOf (const Plan&, const std::string& section);

} // namespace arranger::style_from_audio

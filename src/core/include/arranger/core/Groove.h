#pragma once

#include "arranger/core/Song.h"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

/**
    Groove extraction and transfer: the feel of one part (how early or late
    each step is played, and how loud) put on other parts.

    A groove is a template of one or more bars on a grid (1/8, 1/16 or a
    triplet grid). For each step it keeps the timing offset (ticks; more than
    0 = late) and the loudness (velocity). Drum grooves also keep a lane for
    the kick, the snare and the hi-hats, because a drummer often plays these
    differently (a lazy snare over a tight kick).

    A groove comes from MIDI notes (most exact), from the hits in audio (the
    app finds them; see fromHits), or from words ("lazy swing, behind the
    beat"; see fromFeel and feelFromWords). Applying a groove bends time: the
    steps of the pattern move to the groove's positions, notes between steps
    move in proportion, so notes never change order. Loudness follows the
    groove's accents and keeps the part's own level. Timing and loudness have
    separate amounts, and each drum lane has its own amount.

    Nothing here uses JUCE or plays audio. The app reads audio files and plays
    the before/after preview; undo makes the change reversible.
*/
namespace arranger::groove
{

/** The drum lanes of a groove. `all` is every note together (the lane for melodic parts). */
enum class Lane : std::uint8_t { all, kick, snare, hats, other };
inline constexpr int kLanes = 5;

const char* laneName (Lane); ///< "all", "kick", "snare", "hats", "other"
/** The lane of a General MIDI drum note. */
Lane laneOfDrum (int pitch) noexcept;

/** One step of a lane. */
struct Step
{
    double offset = 0.0;   ///< ticks; more than 0 = played late
    double velocity = 0.0; ///< mean velocity 1..127; 0 = nothing was played on this step
    double rate = 0.0;     ///< 0..1: how often the step was played (in how many repeats of the pattern)

    bool played() const noexcept { return velocity > 0.0; }
    bool operator== (const Step&) const = default;
};

struct Groove
{
    std::string name = "Groove";
    std::string source;   ///< "midi", "audio", "words" or "built-in"
    std::string about;    ///< where it came from, as shown: "Drums, bars 1-8"
    Tick grid = kTicksPerQuarter / 4; ///< the step
    Tick length = kTicksPerQuarter * 4; ///< the pattern (one or more bars)
    double bpm = 0.0;     ///< the tempo it was taken at (for "ms" in summaries); 0 = not known
    /** lanes[lane][step]; a lane without played steps is empty. lanes[all] is never empty for a valid groove. */
    std::array<std::vector<Step>, kLanes> lanes;

    int steps() const noexcept { return grid > 0 ? static_cast<int> (length / grid) : 0; }
    bool empty() const noexcept { return lanes[0].empty(); }
    bool hasLane (Lane l) const noexcept { return ! lanes[static_cast<int> (l)].empty(); }
    const std::vector<Step>& lane (Lane l) const noexcept { return lanes[static_cast<int> (l)]; }

    bool operator== (const Groove&) const = default;
};

// ---- the feel in numbers -----------------------------------------------------------------

/** A groove in words a musician uses, for summaries and for comparing. */
struct Summary
{
    double swing = 50.0;     ///< percent: 50 = straight, 66 = triplet swing (of the groove's grid pairs)
    std::array<double, kLanes> behindMs {}; ///< mean offset of each lane in ms (more than 0 = behind the beat); needs bpm
    std::array<double, kLanes> behindTicks {};
    std::vector<double> accents; ///< lane `all`: relative loudness of each step (1 = the mean); 0 for steps not played
    double spreadMs = 0.0;   ///< how loose: the spread of offsets around the swing, in ms
};
Summary summarise (const Groove&);
/** "Swing 62%, snare 14 ms behind, accents on 2 and 4". */
std::string describe (const Groove&);

// ---- from MIDI -----------------------------------------------------------------------------

struct ExtractOptions
{
    Tick grid = 0;          ///< 0: found from the notes (the coarsest grid that fits: 1/8, 1/8T, 1/16, 1/16T, 1/32)
    int bars = 1;           ///< pattern length in bars (1, 2 or 4)
    Tick from = 0;          ///< song positions to read
    Tick to = std::numeric_limits<Tick>::max();
    bool drums = false;     ///< fill the kick/snare/hats lanes
};

/** A note at a song position, for extraction (the clip start added). */
struct TimedNote
{
    double at = 0.0; ///< song ticks; may have fractions (audio hits)
    int pitch = 60;
    int velocity = 100;
};

/** The notes of a track as song positions (looping clips played out), from `from` to `to`. */
std::vector<TimedNote> notesOf (const Track&, Tick from = 0, Tick to = std::numeric_limits<Tick>::max());

/** The grid that fits the notes best (see ExtractOptions::grid). */
Tick findGrid (const std::vector<TimedNote>&, const TimeSignature&);

/** Takes the groove from notes. Empty groove when there are too few notes (fewer than 4). */
Groove fromNotes (const std::vector<TimedNote>&, const TimeSignature&, const ExtractOptions& = {});

// ---- from audio -------------------------------------------------------------------------

/** A hit found in audio: when, how strong, and what it sounds like. */
struct Hit
{
    double seconds = 0.0;
    double strength = 1.0; ///< 0..1, relative to the strongest hit
    Lane lane = Lane::all; ///< kick (low), snare (middle), hats (high), or all when not known
};

/** Finds hits in mono audio and tells kick, snare and hi-hat apart by their sound (low, middle, high). */
std::vector<Hit> findHits (const std::vector<float>& mono, double sampleRate, double sensitivity = 0.6);

/** Where bar 1 is and the tempo, from the hits. bpm more than 0: the tempo is known. */
struct AudioGrid
{
    double bpm = 120.0;
    double firstDownbeat = 0.0; ///< seconds
    double confidence = 0.0;
};
AudioGrid findAudioGrid (const std::vector<Hit>&, int beatsPerBar = 4, double bpm = 0.0);

/** The hits as notes on a grid from bar 1 (kick 36, snare 38, hats 42, others 60). Velocity from strength. */
std::vector<TimedNote> hitsToNotes (const std::vector<Hit>&, const AudioGrid&);

/** Takes the groove from audio hits (see findHits). The tempo is found when bpm is 0. */
Groove fromHits (const std::vector<Hit>&, const TimeSignature&, double bpm = 0.0, ExtractOptions = {});
/** The same with the bar lines known (audio on the song's Timeline: the song's tempo and bars). */
Groove fromHits (const std::vector<Hit>&, const TimeSignature&, const AudioGrid&, ExtractOptions = {});

// ---- from words ------------------------------------------------------------------------------

/** A feel in plain numbers: what the AI makes from a description, or the user types. */
struct Feel
{
    std::string name = "Feel";
    Tick grid = kTicksPerQuarter / 2;  ///< the swing pairs: 1/8 (480) or 1/16 (240)
    double swing = 50.0;               ///< percent 50..75
    double behindMs = 0.0;             ///< the whole part; more than 0 = behind the beat (laid back)
    double kickMs = 0.0, snareMs = 0.0, hatsMs = 0.0; ///< added for each drum lane
    std::vector<double> accents;       ///< velocity of each step (1..127); empty: on the beat a bit louder
    double bpm = 120.0;                ///< to turn ms into ticks
    int beatsPerBar = 4;
};

/** Reads a description: swing / shuffle / straight, laid back / lazy / behind, pushed / ahead / rushing,
    tight, loose, backbeat (accents on 2 and 4), on the beat, off-beat, soft, punchy, a "62%" swing, "10 ms". */
Feel feelFromWords (const std::string& text, double bpm = 120.0, int beatsPerBar = 4);

Groove fromFeel (const Feel&);

/** Ready grooves: straight, swing 54/58/62/66 %, shuffle 16ths, laid back, pushed, backbeat. */
std::vector<Groove> builtIns (double bpm = 120.0);

// ---- applying -------------------------------------------------------------------------------

struct ApplyOptions
{
    double timing = 1.0;   ///< 0..1.5: how far notes move to the groove's timing
    double velocity = 0.8; ///< 0..1.5: how much of the groove's accents the notes take
    double quantise = 0.0; ///< 0..1: first move notes toward the straight grid (for loose playing)
    /** For drum parts: the amount of each lane, times timing and velocity (index by Lane; `all` is ignored). */
    std::array<double, kLanes> laneAmount { 1.0, 1.0, 1.0, 1.0, 1.0 };
    bool drums = false;    ///< use the groove's drum lanes for the notes (by pitch)
    Tick from = 0;         ///< song positions of the notes to change
    Tick to = std::numeric_limits<Tick>::max();
};

/** The offset (ticks) the groove gives a song position, in a lane (falls back to `all`). */
double offsetAt (const Groove&, double songTick, Lane = Lane::all);
/** The relative loudness (1 = the mean) the groove gives a song position. */
double accentAt (const Groove&, double songTick, Lane = Lane::all);

/** Puts the groove on a clip (notes in [from, to) of the song). Returns the number of notes changed. */
int apply (Clip&, const Groove&, const ApplyOptions&);

/** Puts the groove on tracks. With no tracks: every MIDI track. Returns the number of notes changed. */
int apply (Song&, const std::vector<TrackId>&, const Groove&, ApplyOptions);

// ---- comparing ----------------------------------------------------------------------------

/** How close a part's feel is to a groove. */
struct Match
{
    double timingErrorMs = 0.0;   ///< mean distance of the step offsets (played steps of both), in ms (at `bpm`)
    double timingErrorTicks = 0.0;
    double accentCorrelation = 0.0; ///< -1..1: do loud and soft steps line up
    double swingDifference = 0.0; ///< percent points
    int steps = 0;                ///< steps compared
    double score = 0.0;           ///< 0..100
};
/** Compares two grooves of the same grid and length (a part's own groove and the source). */
Match compare (const Groove& part, const Groove& target, double bpm, Lane = Lane::all);

// ---- the library file ---------------------------------------------------------------------

std::string toJson (const Groove&);
/** Throws std::runtime_error on a bad file. */
Groove fromJson (const std::string&);

} // namespace arranger::groove

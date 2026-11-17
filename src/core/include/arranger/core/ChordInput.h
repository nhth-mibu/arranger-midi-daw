#pragma once

#include "arranger/core/Arranging.h"
#include "arranger/core/StyleChord.h"
#include "arranger/core/Theory.h"

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

/**
    Smart chord input: the song's chord lane, chords typed as names or roman
    numerals, the next chord to try, a palette of chords for the key (for
    pads), voicings, chords played on a keyboard, and other chords for a bar
    (reharmonizing) with the music moved to them.

    The chord lane is the chord symbols of one track (see chordTrack), so the
    Score, the MIDI file, Style Fill and the AI tools see the same chords.
    Plain C++, no JUCE.
*/
namespace arranger::chord_input
{

// ---- the chord lane ------------------------------------------------------------------

/**
    The track whose chord symbols are the song's chord lane: the first track
    with chord symbols, else the first MIDI track that is not drums, else the
    first track. 0 when the song has no tracks.
*/
TrackId chordTrack (const Song&, const DeviceLibrary* = nullptr);

/** A chord of the lane, from its time to the next chord (or the end of the song). */
struct LaneChord
{
    Tick at = 0, end = 0;
    std::string text;           ///< as written: "Am7", "C/E"
    std::optional<Chord> chord; ///< empty for a text that is no chord (N.C.)
};

/** The chords of the lane in time order. */
std::vector<LaneChord> laneChords (const Song&, const DeviceLibrary* = nullptr);

/** The lane chord that sounds at a time, or null. */
const LaneChord* chordAt (const std::vector<LaneChord>&, Tick);

/** The song's key, or C major when its text is no key. */
theory::Key songKey (const Song&);

/** A chord name ("F#m7", "C/E") or a roman numeral in the key ("vi", "V7/V", "bVII"). */
std::optional<Chord> parseChord (std::string_view, const theory::Key&);

/** What typed chords become. */
struct Typed
{
    std::vector<ScoreMark> marks;      ///< song times; a mark only where the chord changes
    std::vector<std::string> unknown;  ///< words that are no chord
    Tick end = 0;                      ///< the end of the last bar typed
    int bars = 0;
};

/**
    Reads chords typed for bars from `start` (a bar line). "|" ends a bar:
    "Am F | C G" is two bars with two chords each, which share the bar. With
    no "|", each chord is one bar: "Am F C G" is four bars. "%" repeats the
    bar before; "/" or "." holds the chord before for one more part of a bar
    ("C / / G" is C for three beats). Names and roman numerals can mix.
    Chords are spelled in the key.
*/
Typed typeChords (std::string_view text, const theory::Key&, const TimeSignature&, Tick start);

/** Replaces the lane chords from `from` to `to` (other marks stay). Creates nothing when the song has no tracks. */
void writeChords (Song&, Tick from, Tick to, const std::vector<ScoreMark>& marks, const DeviceLibrary* = nullptr);

/** The chords typed so far in a text, for suggestions: names or numerals, in order; unknown words are left out. */
std::vector<Chord> chordsIn (std::string_view text, const theory::Key&);

// ---- the next chord --------------------------------------------------------------------

struct Suggestion
{
    Chord chord;
    std::string name;  ///< spelled in the key
    std::string roman; ///< in the key
    std::string why;   ///< "goes home: V to I"
    double score = 0.0;
};

/**
    The chords that most likely come next after `before` (the last chords, in
    order) in the key, best first. With nothing before, the chords a song
    starts on. The rules are common-practice and pop harmony: the usual moves
    between the key's chords, known progressions (I–V–vi–IV, ii–V–I ...),
    secondary dominants that resolve, and chords borrowed from the parallel
    key. With seventh chords before, the suggestions are seventh chords too.
*/
std::vector<Suggestion> suggestNext (const std::vector<Chord>& before, const theory::Key&, std::size_t count = 6);

// ---- the chord palette (pads) ---------------------------------------------------------------

struct PaletteChord
{
    Chord chord;
    std::string name, roman;
    std::string group; ///< "diatonic", "borrowed" or "secondary"
};

/**
    Sixteen chords for the key, as two rows of eight pads. Top row: the seven
    chords of the key and the dominant seventh. Bottom row: chords borrowed
    from the parallel key and the secondary dominants.
*/
std::vector<PaletteChord> palette (const theory::Key&);

// ---- voicings --------------------------------------------------------------------------

enum class Voicing : std::uint8_t
{
    close,  ///< the notes as near as they go
    open,   ///< the second note from the bottom an octave up
    drop2,  ///< the second note from the top an octave down (jazz piano)
    smooth  ///< the inversion nearest to the chord before (voice leading)
};

inline constexpr int kVoicingCount = 4;
const char* voicingName (Voicing); ///< "Close", "Open", "Drop 2", "Smooth"

struct VoiceOptions
{
    int inversion = 0;  ///< 0 = root position, 1 = first inversion ... (wraps); not used by smooth
    Voicing voicing = Voicing::close;
    int centre = 60;    ///< the lowest note of the chord (without the bass) is near this note
    bool bass = true;   ///< add the bass note (the root, or the slash bass) an octave below
};

/**
    The notes to play for a chord, lowest first. `previous` (the notes of
    the chord before) is used by the smooth voicing: it picks the inversion
    whose notes move least.
*/
std::vector<int> voice (const Chord&, const VoiceOptions&, const std::vector<int>& previous = {});

// ---- chords played on a keyboard -------------------------------------------------------------

/** Follows the held keys and names the chord they make (Yamaha's fingered way, lowest key = bass). */
class ChordListener
{
public:
    /** A key went down. Returns the chord when the held keys now make one that differs from the last. */
    std::optional<Chord> noteOn (int pitch);
    void noteOff (int pitch);
    void clear() { held.clear(); last.reset(); }

    std::optional<Chord> lastChord() const { return last; }
    const std::set<int>& heldKeys() const noexcept { return held; }

private:
    std::set<int> held;
    std::optional<Chord> last;
};

/** A chord heard at a song position. */
struct ChordHit
{
    double tick = 0.0;
    Chord chord;
};

/**
    The chord symbols for chords played while recording: each chord moves to
    the nearest grid line (a bar, half a bar or a beat); when more chords
    land on one line, the last one played wins; a chord the same as the one
    before is left out. Spelled in the key.
*/
std::vector<ScoreMark> quantiseHits (const std::vector<ChordHit>&, Tick grid, const theory::Key&);

// ---- reharmonizing ---------------------------------------------------------------------

/** Another way to harmonize a chord's bars: one chord, or two that share its time. */
struct Alternative
{
    std::string label; ///< "Tritone substitute: Db7"
    std::string why;
    std::vector<Chord> chords;
};

/**
    Other chords for `chord` in the key: a tritone substitute, a suspension,
    richer colour, the relative chord, a chord borrowed from the parallel key,
    a secondary dominant or a ii-V into `next`, a passing diminished chord and
    a bass line that walks to `next`. At most `count`, the most usual first.
*/
std::vector<Alternative> alternatives (const Chord& chord, const theory::Key&, const std::optional<Chord>& next, std::size_t count = 6);

/**
    Moves a note from one chord to another: a chord note goes to the note
    with the same job in the new chord (root to root, third to third ...),
    another note stays when it fits the key and the new chord, else goes to
    the nearest note of the new chord. The octave stays as near as it goes.
    A bass part's root goes to the new chord's bass note.
*/
int moveNote (int pitch, const Chord& from, const Chord& to, const theory::Key&, bool bassPart);

/**
    Reharmonizing as a proposal (see Arranging.h), so each option plays
    against the original before it is taken. Finds the lane chord at `at`;
    each option changes that chord on the lane and moves the notes of the
    chord and bass parts in its bars (the melody and the drums stay). Empty
    options when there is no chord at `at`. At most 4 bars change.
*/
arranging::Proposal reharmonize (const Song&, Tick at, const DeviceLibrary* = nullptr, std::size_t count = 6);

} // namespace arranger::chord_input

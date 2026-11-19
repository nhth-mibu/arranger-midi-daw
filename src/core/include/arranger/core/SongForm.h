#pragma once

#include "arranger/core/DeviceProfile.h"
#include "arranger/core/Sample.h"
#include "arranger/core/Song.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
    Song forms: other orders of the same sections (a short version, a longer
    intro, a gig version, an instrumental break), kept in the song without a
    copy of the Timeline. A form plays as a preview; only "use" writes it to
    the Timeline, as one undo step.

    Where a form puts two sections next to each other that are not next to
    each other in the original, the join gets a transition: a drum fill (the
    style's Fill In when the song has a style), the lead-in the next section
    has in the original, a held chord, or a one-beat break. The automatic
    choice looks at both sides: how busy they are, whether the drums play,
    and what led into the section in the original.

    Plain music rules, no model. Plain C++, no JUCE.
*/
namespace arranger::form
{

// ---- sections ------------------------------------------------------------------------

/** Gives the audio of a file (null when it can't be read), so that recordings count when sections are found. */
using SampleLookup = std::function<std::shared_ptr<const Sample> (const std::string& file)>;

/**
    Finds the sections of a song with none: where the texture changes (which
    tracks play, how busy), with similar parts named alike ("Verse 1",
    "Verse 2", "Chorus 1"). A song with an empty Timeline and Clip grid
    scenes gets one section for each used scene. With `audio`, audio tracks
    count by how loud they play and how the loudness moves in each bar;
    without it, only where they have clips.
*/
std::vector<SongSection> findSections (const Song&, const DeviceLibrary* = nullptr, const SampleLookup& audio = {});

/** The song's own sections, or the found ones when it has none. */
std::vector<SongSection> sectionsOf (const Song&, const DeviceLibrary* = nullptr, const SampleLookup& audio = {});

/** What a section is, from its name: intro, verse, prechorus, chorus, bridge, solo, outro, or other. */
std::string kindOf (std::string_view sectionName);

/** Why a list of sections is not valid (empty names, the same name twice, overlaps, no length), or empty. */
std::string problemWith (const std::vector<SongSection>&);

/**
    Reads sections from text, one per line: "Intro 1-4", "Verse 1: 5-12",
    "Chorus 13-20" (bars from 1, both included). Throws std::runtime_error
    with the line that is wrong.
*/
std::vector<SongSection> parseSections (const Song&, std::string_view text);
/** The sections as text, as parseSections reads it. */
std::string sectionsText (const Song&, const std::vector<SongSection>&);

// ---- forms ---------------------------------------------------------------------------

enum class Preset
{
    shortVersion,      ///< intro, the first verse and chorus, the last chorus, the outro
    longIntro,         ///< the intro twice, the first time with drums and bass only: a build
    gig,               ///< a longer intro to count in, the last chorus twice, a final chord
    instrumentalBreak  ///< a verse with the melody resting (room for a solo) before the last chorus
};

const char* presetId (Preset);   ///< short, long_intro, gig, instrumental_break
std::string presetName (Preset); ///< "Short version"
std::optional<Preset> parsePreset (std::string_view);
inline constexpr Preset kPresets[] = { Preset::shortVersion, Preset::longIntro, Preset::gig, Preset::instrumentalBreak };

/** A form from a preset and the song's sections. Throws std::runtime_error when the song has no sections. */
SongForm makePreset (const Song&, const std::vector<SongSection>&, Preset, const DeviceLibrary* = nullptr, TrackId melody = 0);

/**
    A form from text: section names in order, with "x2" to repeat and
    "(no Melody)" to rest tracks: "Intro, Verse 1, Chorus 1 x2, Outro".
    Throws std::runtime_error naming a section that is not there.
*/
SongForm parseForm (const Song&, const std::vector<SongSection>&, const std::string& name, std::string_view order);

/** "Intro → Verse 1 → Chorus 1 ×2 → Outro". */
std::string describe (const SongForm&);

const char* transitionId (Transition);   ///< automatic, none, fill, pickup, hold, stop
std::string transitionName (Transition); ///< "Drum fill"
std::optional<Transition> parseTransition (std::string_view);

/** Where two parts of a form meet. */
struct Boundary
{
    int part = 0;          ///< the part that begins here (1 or more)
    Tick at = 0;           ///< in the form
    std::string from, to;  ///< part names
    bool natural = false;  ///< the original has these two sections next to each other: no transition is needed
    Transition chosen = Transition::none;      ///< what the preview plays (the part's choice, or the recommendation)
    Transition recommended = Transition::none;
    std::vector<Transition> options;           ///< what fits here, best first
    std::string why;       ///< the reason for the recommendation
};

/** A form written out: the song as it would be. */
struct Rendered
{
    Song song;                        ///< the Timeline in the form's order; sections follow the parts
    std::vector<Boundary> boundaries; ///< the joins between parts
    Transition ending = Transition::none;
    Tick length = 0;
    std::vector<std::string> notes;   ///< things to know ("No drum track: the fill was left out")
};

/**
    Writes a form out. The song's tracks, tempo, automation, chords and
    lyrics follow the parts; audio clips are cut to the parts with short
    fades. Notes that come in just before a section (pushes) go with it.
    Throws std::runtime_error when a part names no section.
*/
Rendered render (const Song&, const SongForm&, const std::vector<SongSection>&, const DeviceLibrary* = nullptr);

/** The song with a form used: the rendered song, its sections are the parts, and the song keeps no forms (their sections are gone). */
Song use (const Song&, const SongForm&, const std::vector<SongSection>&, const DeviceLibrary* = nullptr);

/** The form with this name in the song, or null. Names are compared without case. */
const SongForm* findForm (const Song&, std::string_view name);

/** Adds a form, or replaces the form with the same name. */
void putForm (Song&, SongForm);

} // namespace arranger::form

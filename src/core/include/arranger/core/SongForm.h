#pragma once

#include "arranger/core/DeviceProfile.h"
#include "arranger/core/Song.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
    Alternative song forms: named arrangements (a short version, a long
    intro, a bridge twice) made of the song's sections, without copies of the
    music. A form plays as a song built from the sections in its order. Where
    a new pair of sections meets (they were not next to each other in the
    song), the app proposes a transition that fits both sides: a drum fill, a
    bass pickup into the next part's first note, a held chord, or a break.
    The Timeline changes only when the user takes the form, as one undo step.

    Nothing here uses JUCE or plays audio. The app plays the built song.
*/
namespace arranger::song_form
{

const char* kindId (Transition::Kind);       ///< none, fill, pickup, hold, rest
std::string kindName (Transition::Kind);     ///< "Drum fill"
std::optional<Transition::Kind> parseKind (std::string_view);
/** The usual length of a transition, in beats. */
int defaultBeats (Transition::Kind, const TimeSignature&);

// ---- sections ---------------------------------------------------------------------------

/**
    Proposes sections from the music: the song in phrases of 4 or 8 bars,
    phrases that play alike grouped, and neighbours of one group joined.
    Names: Intro and Ending for a first or last part that comes only once,
    Chorus for the group that comes most, Verse for the one before it,
    then Bridge, Part E... A group that comes more than once is numbered
    (Verse 1, Verse 2). Ids come from the song's nextId.
*/
std::vector<Section> findSections (Song&);

const Section* findSection (const Song&, std::uint32_t id);

/** Sorts sections, makes them whole bars and takes away overlaps. Sections with no length go. */
void tidySections (Song&);

/** A form from section names ("Intro Verse 1 Chorus 1 Chorus 1 Ending"; commas and | allowed). Unknown names go to `unknown`. */
SongForm formFromNames (const Song&, const std::string& name, const std::string& names, std::vector<std::string>* unknown = nullptr);

/** The form of the song as it is: every section in Timeline order. */
SongForm currentForm (const Song&);

/** "Intro, Verse 1, Chorus 1 x2, Ending" */
std::string partsText (const Song&, const SongForm&);

/** Makes `transitions` one shorter than `parts`. */
void fitTransitions (SongForm&);

// ---- boundaries and transitions ---------------------------------------------------------

/** Where one part of a form goes into the next. */
struct Boundary
{
    int index = 0;               ///< transitions[index]
    std::uint32_t from = 0, to = 0; ///< section ids
    Tick at = 0;                 ///< in the built song: where the next part starts
    bool isNew = false;          ///< the two sections were not next to each other in the song
    double densityBefore = 0.0, densityAfter = 0.0; ///< notes per bar, the last bar before and the first bar after
    std::vector<Transition::Kind> kinds; ///< what fits here (none first)
    Transition::Kind suggested = Transition::Kind::none;
    std::string because;         ///< why the suggestion: "The Chorus is busier: a fill lifts into it"
};

std::vector<Boundary> boundaries (const Song&, const SongForm&, const DeviceLibrary* = nullptr);

// ---- building ---------------------------------------------------------------------------

/**
    The song as the form plays it: each part's music (MIDI notes and
    controllers, audio clips, automation, chord symbols and lyrics) copied
    from its section, one after the other, then the transitions. The Clip
    grid, mixer and sounds stay. The built song's sections are the parts.
*/
Song build (const Song&, const SongForm&, const DeviceLibrary* = nullptr);

/** Where each part starts in the built song (and the end of the last), for previews. */
std::vector<Tick> partStarts (const Song&, const SongForm&);

/**
    Takes the form: the Timeline becomes the built song. The other forms
    stay when all their sections are still there (they now name the copies).
    Throws std::runtime_error when the form is empty or names a missing section.
*/
void use (Song&, const SongForm&, const DeviceLibrary* = nullptr);

/** Why a form can't be built, or empty. */
std::string problemWith (const Song&, const SongForm&);

} // namespace arranger::song_form

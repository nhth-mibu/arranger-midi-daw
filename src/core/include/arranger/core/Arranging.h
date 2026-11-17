#pragma once

#include "arranger/core/DeviceProfile.h"
#include "arranger/core/Song.h"

#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
    Arrangement help that proposes small, explained edits: the arrangement
    doctor (this file) and the hardware check (HardwareFit.h). Both use one
    proposal-and-preview mechanism: a proposal says what was seen, which
    tracks and bars it touches, and gives one or two options. Each option is
    a ready result (the changed tracks), so the app can play it against the
    original before the user takes it as one undo step.

    The rules are plain music rules (density, register, repetition, contrast,
    polyphony), not a model. They give suggestions, not verdicts.
*/
namespace arranger::arranging
{

// ---- what the tracks play --------------------------------------------------------

/** A note as it sounds in the song: song times, looping clips played out. */
struct PlayedNote
{
    Tick on = 0, off = 0;
    int pitch = 60, velocity = 100;
};

/** The notes of a MIDI track that start in [from, to), sorted by time. */
std::vector<PlayedNote> playedNotes (const Track&, Tick from = 0, Tick to = std::numeric_limits<Tick>::max());

/** What a track does in the arrangement. Guessed from its name, channel and notes. */
enum class Role
{
    drums,
    bass,
    melody,
    chords,
    other
};

const char* roleName (Role);

/** True for a drum part: a sampler kit, or a channel the device plays drums on. */
bool isDrumTrack (const Track&, const DeviceLibrary* = nullptr);

/** The role of every MIDI track with notes. `melody` (not 0) is the melody track the user chose. */
std::map<TrackId, Role> guessRoles (const Song&, const DeviceLibrary* = nullptr, TrackId melody = 0);

/** "bars 9-12" for [from, to). */
std::string barsText (const Song&, Tick from, Tick to);

// ---- proposals -------------------------------------------------------------------

/** One way to do a proposal: the tracks as they would be after it. */
struct Option
{
    std::string label;   ///< "Play on the beats only"
    std::string summary; ///< "Removes 24 of 96 notes in Piano"
    std::vector<Track> tracks; ///< the changed tracks, whole (they replace the tracks with the same ids)
    int notesChanged = 0;
};

struct Proposal
{
    std::string id;     ///< the same for the same song and request, so a tool can find it again
    std::string kind;   ///< density, overlap, repetition, contrast, energy, register, polyphony, range, parts, drums
    std::string title;  ///< "Thin the Piano in bars 9-12"
    std::string because; ///< the observations behind it
    std::vector<TrackId> tracks; ///< the tracks it changes
    Tick from = 0, to = 0;       ///< the bars it changes
    std::vector<Track> before;   ///< the tracks as they were, to see if the song changed since
    std::vector<Option> options; ///< one or two
    double weight = 1.0;         ///< for the order: how strong the evidence is, times the user's earlier choices
};

/** True while the tracks of the proposal are as they were when it was made. */
bool isCurrent (const Song&, const Proposal&);

/** Why an option can't be taken (a note out of range, a missing track), or empty. */
std::string problemWith (const Song&, const Option&);

/** The song with an option done: for previews. */
Song withOption (const Song&, const Option&);

/** Does an option of a proposal. Throws std::runtime_error when the song changed since, or the option is not valid. */
void apply (Song&, const Proposal&, int option);

/**
    The user's earlier choices, so the proposals they take come first. Only
    explicit choices count: taking an option, or dismissing a proposal.
*/
struct Preferences
{
    struct Count
    {
        int taken = 0, dismissed = 0;
    };
    std::map<std::string, Count> counts; ///< by "kind" and by "kind/option"

    void took (const Proposal&, int option);
    void dismissed (const Proposal&);
    /** More than 1 for kinds the user takes, less for kinds they dismiss. */
    double weight (const std::string& kind) const;
    /** The option of a kind the user took most, or -1. */
    int favouriteOption (const std::string& kind, int options) const;

    std::string toJson() const;
    static Preferences fromJson (const std::string&);
};

/** Orders proposals by weight (strongest first), with the user's preferences. */
void rank (std::vector<Proposal>&, const Preferences* = nullptr);

// ---- the arrangement doctor ----------------------------------------------------

/** What the user wants from the arrangement. */
enum class Goal
{
    standOut,      ///< make a section (a chorus) stand out, the melody stays
    clearMelody,   ///< give the melody room
    lessClutter,   ///< thin the busiest bars
    moreVariation  ///< less of the same bar again and again
};

const char* goalId (Goal);          ///< stand_out, clear_melody, less_clutter, more_variation
std::string goalName (Goal);        ///< "Make a section stand out"
std::optional<Goal> parseGoal (std::string_view);

/** Something the doctor saw, with numbers. */
struct Finding
{
    std::string kind; ///< density, overlap, repetition, contrast
    std::string text;
    Tick from = 0, to = 0;
    std::vector<TrackId> tracks;
    double amount = 0.0; ///< how strong (for the order)
};

struct DoctorRequest
{
    Goal goal = Goal::lessClutter;
    Tick from = 0, to = 0; ///< standOut: the section to lift. Others: where to look (to = 0: the whole song).
    TrackId melody = 0;    ///< 0: guessed
    const DeviceLibrary* devices = nullptr;
    const Preferences* preferences = nullptr;
    std::size_t mostProposals = 6;
};

struct DoctorReport
{
    std::vector<Finding> findings; ///< all four checks, strongest first
    std::vector<Proposal> proposals; ///< for the goal, ranked
    std::map<TrackId, Role> roles;
    TrackId melody = 0;
};

/** Looks at the arrangement and proposes edits for the goal. The melody track is never changed. */
DoctorReport diagnose (const Song&, const DoctorRequest&);

} // namespace arranger::arranging

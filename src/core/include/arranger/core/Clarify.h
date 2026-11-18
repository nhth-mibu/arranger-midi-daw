#pragma once

#include "arranger/core/Arranging.h"
#include "arranger/core/AudioAnalysis.h"
#include "arranger/core/Song.h"

#include <string>
#include <vector>

/**
    Transcription that asks questions: after an audio analysis, finds the few
    places where the result is a near tie ("bass E or A here?"), so that the
    user can listen and choose. The answer corrects the chord there, moves the
    bass notes of the bass stem to the chosen note, and locks the place, so
    a later check does not ask again. Other edits stay as they are.

    The first version asks about two things only: the bass note and the chord
    root. The evidence is the chroma of the harmony stems (all notes and the
    low notes) and the notes of the bass stem. The scores are not
    probabilities: a question means "these two are close", not "50/50".

    Without a bass stem (chords found in the full mix) the low notes are
    less sure (the kick drum is there too): the bass counts half, chords over
    another bass note are not proposed, and it asks only when the other
    choice fits better than the one the analysis has. A chord over its fifth
    is never proposed (that is mostly a root-fifth bass line), over its
    seventh only with a bass stem.
*/
namespace arranger::clarify
{

/** The note the bass plays for a chord: its bass note, or its root. */
int bassOf (const Chord&) noexcept;

/** One answer to choose: a chord for the place. */
struct Choice
{
    Chord chord;
    std::string label;  ///< "Bass E (C/E)", "Am"
    double score = 0.0; ///< how well it fits the audio (higher is better); for the order, not a probability
};

/** A place where the analysis is not sure. */
struct Question
{
    std::string id;          ///< "q-12.40": the same for the same place, so a tool can find it again
    std::string kind;        ///< "bass" (the bass notes differ) or "root" (the same bass, another chord root)
    double start = 0.0;      ///< seconds in the analysed file
    double end = 0.0;
    std::string text;        ///< "Bass E or A here?"
    std::string because;     ///< the evidence, in words
    std::vector<Choice> choices; ///< two: [0] what the analysis has now, [1] the other one
    double doubt = 0.0;      ///< the score of [1] divided by the score of [0]; near 1 or more: a real question
    std::vector<std::string> similar; ///< ids of other questions with the same two choices
};

struct Settings
{
    std::size_t mostQuestions = 8;
    double ask = 0.9;          ///< ask when the other choice scores at least this part of the current one
    double shortest = 0.8;     ///< seconds: shorter chords are not worth a question
    double bassWeight = 0.4;   ///< how much the bass evidence counts against the harmony
};

/**
    The chord score of a chord in a span: how well its notes match `harmony`
    (0..1, as the chord finder scores) plus the bass evidence for its bass
    note. `bassWeights` is 0..1 per note (0 = C), the strongest at 1.
*/
double score (const Chord&, const std::array<float, 12>& harmony, const std::array<float, 12>& bassWeights, const Settings& = {});

/**
    How strongly each note sounds in the bass from `start` to `end` (seconds):
    from the notes of the bass stem and the low part of the chroma. The
    strongest note gets 1. All zero when nothing is there.
*/
std::array<float, 12> bassEvidence (const analysis::Result&, const analysis::Chroma& harmony, const analysis::Chroma* bassStem, double start, double end);

/** The sum of the chroma frames from `start` to `end` (seconds). */
std::array<float, 12> harmonyEvidence (const analysis::Chroma&, double start, double end);

/**
    Finds the places to ask about, the most useful first. Places that the
    user answered before (Result::answers) are left out. `bassStem` is the
    chroma of the bass stem when there is one (else the low notes of
    `harmony` count).
*/
std::vector<Question> find (const analysis::Result&, const analysis::Chroma& harmony, const analysis::Chroma* bassStem = nullptr, const Settings& = {});

/** What an answer changed. */
struct Changes
{
    int chords = 0;     ///< chord symbols (and chords in the result)
    int bassNotes = 0;  ///< notes moved to the other bass note
    std::vector<TrackId> tracks; ///< the tracks of the song that changed
    std::vector<std::string> what; ///< in words, for the user
};

/**
    Puts an answer into the analysis result: the chord of the place becomes
    `chosen`, the bass notes of the bass stem on the old bass note move to the
    new one (the nearest octave), and the place is locked (Result::answers).
    Choosing the chord that is there already only locks the place.
*/
Changes answer (analysis::Result&, const Question&, const Chord& chosen);

/**
    Puts an answer into the song made from the audio clip `source`: chord
    symbols in the place, the notes of the "Chords" track that the analysis
    made, and the bass notes of the bass track ("Bass MIDI", else a track that
    plays bass) on the old bass note. Notes on other pitches stay as they are.
*/
Changes change (Song&, ClipId source, const Question&, const Chord& chosen);

/** The place in the song: [from, to) in ticks, on whole beats. */
std::pair<Tick, Tick> span (const Song&, ClipId source, const Question&);

/**
    A question as a proposal, for the shared A/B cards and previews: option A
    keeps what the analysis has, option B takes the other choice.
*/
arranging::Proposal proposal (const Song&, ClipId source, const Question&);

/**
    The song to listen to for a choice, looping the place: the recording as it
    is, and with `choice` >= 0 a guide track that plays the chord of that
    choice (piano, its bass note low). The other MIDI tracks are muted.
*/
Song listen (const Song&, ClipId source, const Question&, int choice);

/** The questions with the same two choices as `q` (its `similar` ids) that are in `all`. */
std::vector<const Question*> similarTo (const std::vector<Question>& all, const Question& q);

} // namespace arranger::clarify

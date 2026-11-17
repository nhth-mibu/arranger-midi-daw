#pragma once

// Shared helpers of the arrangement doctor and the hardware check (not public).

#include "arranger/core/Arranging.h"

#include <functional>
#include <string>
#include <vector>

namespace arranger::arranging::detail
{

inline Tick barLength (const Song& s) { return std::max<Tick> (1, s.timeSignature.ticksPerBar()); }
inline Tick beatLength (const Song& s) { return std::max<Tick> (1, s.timeSignature.ticksPerBeat()); }

/** The end of the song, rounded up to a whole bar. */
Tick songEnd (const Song&);

std::string trackName (const Song&, TrackId);
std::string lower (std::string);
bool nameHas (const std::string& name, std::initializer_list<const char*> words);

/** A note name: "C4" (MIDI 60). */
std::string pitchText (int pitch);

/**
    Changes the notes of a track that start in [from, to). `change` gets each
    note at its song time; it can change pitch, velocity and length, and
    returns false to remove the note. A looping clip with a changed note is
    played out first. Returns the number of notes changed or removed.
*/
int editNotes (Track&, Tick from, Tick to, const std::function<bool (Note&)>& change);

/** Adds notes (song times) to the clips they start in; notes outside every clip are left out. Returns how many went in. */
int addNotes (Track&, const std::vector<Note>& songNotes);

/** A proposal on one or more tracks, with `before` filled in. */
Proposal makeProposal (const Song&, std::string kind, std::string id, std::string title, std::string because,
                       std::vector<TrackId> tracks, Tick from, Tick to);

/** Adds an option when it changes something. */
void addOption (Proposal&, std::string label, std::string summary, std::vector<Track> tracks, int changed);

/** "1 note" or "12 notes". */
std::string count (int n, const char* one, const char* many);

} // namespace arranger::arranging::detail

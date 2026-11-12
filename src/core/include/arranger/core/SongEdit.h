#pragma once

#include "arranger/core/Song.h"

#include <functional>

namespace arranger::edit
{

// ---- Finding things ---------------------------------------------------------
Track* findTrack (Song&, TrackId);
const Track* findTrack (const Song&, TrackId);
int trackIndex (const Song&, TrackId); ///< -1 if missing

struct ClipRef
{
    Track* track = nullptr;
    std::size_t index = 0;
    const Clip& clip() const { return *track->clips[index]; }
};
ClipRef findClip (Song&, ClipId);
ClipPtr findClipPtr (const Song&, ClipId);
TrackId trackOfClip (const Song&, ClipId); ///< 0 if missing

// ---- Tracks -----------------------------------------------------------------
/** Adds a track after the given index (-1 = at the end). Picks a free colour and MIDI channel. */
Track& addTrack (Song&, const std::string& name, int insertAfter = -1);
void removeTrack (Song&, TrackId);
void moveTrack (Song&, TrackId, int newIndex);

// ---- Clips ------------------------------------------------------------------
/** Adds a clip to a track (gives it an id if it has none) and returns its id. */
ClipId addClip (Song&, TrackId, Clip);
void removeClip (Song&, ClipId);

/** Edits a clip copy-on-write: other holders of the old clip (undo, engine) are unaffected. */
void modifyClip (Song&, ClipId, const std::function<void (Clip&)>&);

/** Moves a clip in time and/or to another track. */
void moveClip (Song&, ClipId, Tick newStart, TrackId newTrack);

/** Splits a clip at an absolute tick; notes crossing the split are shortened. Returns the new right-hand clip id. */
ClipId splitClip (Song&, ClipId, Tick at);

/** Joins clips on the same track into the first one. */
void joinClips (Song&, const std::vector<ClipId>&);

// ---- Notes ------------------------------------------------------------------
/** Sorts notes and controls, drops zero-length notes and clamps values. */
void normalise (Clip&);

/** Merges overlapping notes of the same pitch (keeps the earlier start). */
void removeOverlaps (Clip&);

/** Applies `fn` to a copy of every clip (for whole-song transforms). */
void forEachClip (Song&, const std::function<void (Clip&)>&);

std::uint32_t takeId (Song&);

} // namespace arranger::edit

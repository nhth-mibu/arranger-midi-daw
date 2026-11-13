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
    bool inSlot = false; ///< true: index is a Clip grid slot, not a Timeline clip
    const Clip& clip() const { return inSlot ? *track->slots[index] : *track->clips[index]; }
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

// ---- Clip grid ----------------------------------------------------------------
/** Empties a stretch of a track's Timeline: clips are cut at the edges and removed between them. */
void clearRange (Song&, TrackId, Tick from, Tick to);

/** Puts a clip in a track's slot (replacing what is there) and returns its id. The clip starts at 0. */
ClipId setSlot (Song&, TrackId, int scene, Clip);
void clearSlot (Song&, TrackId, int scene);
/** The scene a slot clip is in, or -1 for Timeline clips and unknown ids. */
int sceneOfClip (const Song&, ClipId);

// ---- Notes ------------------------------------------------------------------
/** Sorts notes and controls, drops zero-length notes and clamps values. */
void normalise (Clip&);

/** Merges overlapping notes of the same pitch (keeps the earlier start). */
void removeOverlaps (Clip&);

/** A note in a clip, named by its value: it stays right when other notes of the clip change. */
struct NoteKey
{
    ClipId clip = 0;
    Note note;

    bool operator== (const NoteKey&) const = default;
};

/**
    Puts a note on a track's Timeline at a song position. The note goes into
    the clip that is there; with no clip there, a new clip of one bar (or
    more, to hold the note) is made. Returns the note, or clip 0 on failure.
*/
NoteKey addTimelineNote (Song&, TrackId, Tick at, Tick length, int pitch, int velocity = 90);

/** Changes notes with `fn` (start, length, pitch, velocity) and returns them as they are after. Missing notes are skipped. */
std::vector<NoteKey> changeNotes (Song&, const std::vector<NoteKey>&, const std::function<void (Note&)>& fn);

void removeNotes (Song&, const std::vector<NoteKey>&);

// ---- Score marks ----------------------------------------------------------------
/**
    Puts a mark on a track's score. A chord, lyric or dynamic replaces the one
    at the same time; an empty text removes it. An articulation is added, or
    removed when the same one is there already.
*/
void setMark (Song&, TrackId, ScoreMark);

/** Removes the marks of a track from `from` up to (not including) `to`. */
void clearMarks (Song&, TrackId, Tick from, Tick to);

/**
    Puts the words of a text under the notes of a track, one word (or one
    part of a word, after each hyphen) per note start, from `from` on.
    Returns the number of words placed.
*/
int setLyrics (Song&, TrackId, Tick from, const std::string& text, const std::vector<Tick>& noteStarts);

/** Applies `fn` to a copy of every clip (for whole-song transforms). */
void forEachClip (Song&, const std::function<void (Clip&)>&);

std::uint32_t takeId (Song&);

} // namespace arranger::edit

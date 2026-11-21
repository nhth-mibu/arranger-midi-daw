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

/** Turns solo on or off. exclusive: turning it on clears every other solo, so one track is soloed at a time. */
void setSolo (Song&, TrackId, bool on, bool exclusive = true);
/** Flips solo (see setSolo). */
void toggleSolo (Song&, TrackId, bool exclusive = true);

// ---- Clips ------------------------------------------------------------------
/** Adds a clip to a track (gives it an id if it has none) and returns its id. */
ClipId addClip (Song&, TrackId, Clip);
void removeClip (Song&, ClipId);

/** What a clip plays, with a looping clip played out to its end (loopLength becomes 0). */
Clip playedClip (const Clip&);

/** Edits a clip copy-on-write: other holders of the old clip (undo, engine) are unaffected. */
void modifyClip (Song&, ClipId, const std::function<void (Clip&)>&);

/** Moves a clip in time and/or to another track. */
void moveClip (Song&, ClipId, Tick newStart, TrackId newTrack);

/** Splits a clip at an absolute tick; notes crossing the split are shortened. Returns the new right-hand clip id. */
ClipId splitClip (Song&, ClipId, Tick at);

/** Joins clips on the same track into the first one. */
void joinClips (Song&, const std::vector<ClipId>&);

// ---- Audio clips ------------------------------------------------------------------
// Audio clips share their ids with MIDI clips: trackOfClip, removeClip, moveClip
// and splitClip work on them too.
AudioClipPtr findAudioClip (const Song&, ClipId);
/** How fast an audio clip plays its file: 1, or song tempo / file tempo when the clip follows the tempo. */
double audioSpeed (const Song&, const AudioClip&);

/** Adds an audio clip to a track (gives it an id if it has none) and returns its id. */
ClipId addAudioClip (Song&, TrackId, AudioClip);

void modifyAudioClip (Song&, ClipId, const std::function<void (AudioClip&)>&);

/** Adds an audio track after the given index (-1 = at the end). */
Track& addAudioTrack (Song&, const std::string& name, int insertAfter = -1);

// ---- Clip grid ----------------------------------------------------------------
/** Empties a stretch of a track's Timeline: clips are cut at the edges and removed between them. */
void clearRange (Song&, TrackId, Tick from, Tick to);

/** Puts a clip in a track's slot (replacing what is there) and returns its id. The clip starts at 0. */
ClipId setSlot (Song&, TrackId, int scene, Clip);
/**
    Fills the Clip grid from the Timeline: the song is cut into parts of
    `sceneLength` ticks, and each part becomes one scene. A MIDI track gets a
    slot clip in each scene where it has notes (looping clips are played out).
    Scenes are named "Bars 1-4" and so on. Slots already there are replaced.
    Returns the number of scenes (at most 128).
*/
int slotsFromTimeline (Song&, Tick sceneLength);
/** Clears the slot of a scene: the MIDI clip and the audio clip. */
void clearSlot (Song&, TrackId, int scene);
/** Puts an audio clip in a slot of an audio track (replacing what is there) and returns its id. The clip starts at 0. */
ClipId setAudioSlot (Song&, TrackId, int scene, AudioClip);

// ---- Takes and crossfades ------------------------------------------------------------
/** The shortest crossfade where one audio clip covers another (about 10 ms at 120 BPM). */
inline constexpr Tick kAudioCrossfade = kTicksPerQuarter / 48;

/** The part of an audio clip that plays: where clips overlap, only the top clip plays, with crossfades at its edges. */
struct AudioPiece
{
    AudioClipPtr clip;
    Tick start = 0, end = 0;     ///< song position, inside the clip
    Tick fadeIn = 0, fadeOut = 0;
};

/** The parts of the Timeline audio clips of a track that play, sorted by start. Clips that do not overlap play whole. */
std::vector<AudioPiece> audioPieces (const Track&);
/** A layer above all clips of the track that overlap this span: a new take goes there. */
int layerAbove (const Track&, Tick start, Tick end) noexcept;
/** Puts an audio clip on top of the clips it overlaps, so it is the take that plays. */
void bringToFront (Song&, ClipId);
/** The audio clips of the track at a song position, top first. */
std::vector<AudioClipPtr> audioClipsAt (const Track&, Tick);
/** The audio clips of a track in the order to draw them: bottom first, top last. */
std::vector<AudioClipPtr> audioDrawOrder (const Track&);
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

// ---- Buses ---------------------------------------------------------------------
/** A track whose sound the app makes, so it can have effects, a bus and sends (not a MIDI port or a silent track). */
bool makesSound (const Track&) noexcept;
Bus& addBus (Song&, const std::string& name);
/** Removes the bus. Its tracks go to the master again and lose their sends to it. */
void removeBus (Song&, std::uint32_t id);
Bus* findBus (Song&, std::uint32_t id) noexcept;
const Bus* findBus (const Song&, std::uint32_t id) noexcept;
/** Sends the sound of a track to a bus (0 = the master). Does nothing for a track that makes no sound or a bus that is not there. */
void setTrackBus (Song&, TrackId, std::uint32_t bus);
/** The send level of a track to a bus, 0 when it does not send. */
int sendLevel (const Track&, std::uint32_t bus) noexcept;
/** Sets the send level of a track to a bus. Level 0 removes the send. */
void setSend (Song&, TrackId, std::uint32_t bus, int level);

// ---- Automation ------------------------------------------------------------------
/** `lane`: 0 volume, 1 pan, 2 reverb send, 3 chorus send, from 4 on the plugin control lines (see Track::findLine).
    A point at the same time is replaced. Returns the index of the point, or -1. */
int setAutomationPoint (Song&, TrackId, int lane, AutomationPoint);
void removeAutomationPoint (Song&, TrackId, int lane, int index);
/** Removes all points of a line. A plugin control line goes away with its points. */
void clearAutomation (Song&, TrackId, int lane);
/** Gives a control of a plugin of the track an automation line (with no points yet). `slot`: -1 the instrument,
    or the number of the effect. Returns the lane of the line (also when it was there already), or -1. */
int addControlLine (Song&, TrackId, int slot, int control, const std::string& name);

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

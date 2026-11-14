#pragma once

#include "arranger/core/SongEdit.h"

#include <vector>

namespace arranger::clipboard
{

/**
    What Cut or Copy keeps, to paste later: clips (MIDI and audio), notes, or
    the points of an automation line. Times are relative to the earliest item,
    so a paste puts the earliest item at the paste position.
*/
struct Content
{
    enum class Kind { empty, clips, notes, points };

    struct ClipItem
    {
        int trackOffset = 0; ///< tracks below the first track of the copy
        ClipPtr midi;        ///< one of midi or audio is set; its start is relative
        AudioClipPtr audio;
    };

    Kind kind = Kind::empty;
    std::vector<ClipItem> clips;
    std::vector<Note> notes;            ///< start relative to the earliest note
    std::vector<AutomationPoint> points; ///< at relative to the earliest point
    Tick span = 0;                      ///< from the earliest start to the latest end

    bool empty() const noexcept { return kind == Kind::empty; }
};

/** Copies Timeline clips (MIDI or audio; missing ids are skipped). */
Content copyClips (const Song&, const std::vector<ClipId>&);
/** Copies the clip in a Clip grid slot (MIDI or audio). Empty when the slot is empty. */
Content copySlot (const Song&, TrackId, int scene);
/** Copies notes; NoteKey starts are relative to their clip. */
Content copyNotes (const Song&, const std::vector<edit::NoteKey>&);
/** Copies the points of a line by their index. */
Content copyPoints (const AutomationLine&, const std::vector<int>& indexes);

/**
    Pastes clips on the Timeline: the earliest at `at`, the first track of the
    copy on `track`. A clip goes only on a track of its kind (audio clips on
    audio tracks); others are skipped. Returns the new clip ids.
*/
std::vector<ClipId> pasteClips (Song&, const Content&, TrackId track, Tick at);

/**
    Pastes notes on a track at a song position. They go into the clip there;
    with no clip there, a new clip is made from the bar line. A clip grows to
    hold the notes, up to the next clip. Returns the new notes.
*/
std::vector<edit::NoteKey> pasteNotes (Song&, const Content&, TrackId, Tick at);

/** Pastes notes into one clip, at a time relative to the clip start. Notes after the clip end are dropped. */
std::vector<edit::NoteKey> pasteNotesInClip (Song&, const Content&, ClipId, Tick at);

/**
    Pastes into a Clip grid slot, in place of what is there: the first copied clip
    of the kind of the track, or the notes as a new clip of whole bars.
    Returns the new clip id, or 0 when nothing fits.
*/
ClipId pasteSlot (Song&, const Content&, TrackId, int scene);

/** Pastes points into a line of a track (see edit::setAutomationPoint). Returns their times. */
std::vector<Tick> pastePoints (Song&, const Content&, TrackId, int lane, Tick at);

} // namespace arranger::clipboard

#pragma once

#include "arranger/core/TempoMap.h"
#include "arranger/core/Time.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace arranger
{

using TrackId = std::uint32_t;
using ClipId = std::uint32_t;

/** Track colours are names from the theme palette, so a theme can restyle them. */
inline constexpr const char* kTrackColours[] = { "coral", "amber", "teal", "blue", "violet", "green", "pink" };

struct Note
{
    Tick start = 0;   ///< relative to the clip start
    Tick length = 240;
    std::uint8_t pitch = 60;
    std::uint8_t velocity = 100;

    Tick end() const noexcept { return start + length; }
    bool operator== (const Note&) const = default;
};

/** Controller-type events in a clip: CC, pitch bend, program change, channel pressure. */
struct ControlEvent
{
    enum class Kind : std::uint8_t { controller, pitchBend, program, channelPressure };

    Tick at = 0;      ///< relative to the clip start
    Kind kind = Kind::controller;
    std::uint8_t number = 0; ///< CC number (controller only)
    int value = 0;           ///< 0..127, or -8192..8191 for pitch bend

    bool operator== (const ControlEvent&) const = default;
};

/**
    A MIDI clip: a stretch of the timeline with notes in it. Clips are
    immutable once shared; edits make a new copy (see SongEdit.h). That keeps
    undo snapshots and the engine's playback copy cheap.
*/
struct Clip
{
    ClipId id = 0;
    std::string name;
    Tick start = 0;
    Tick length = kTicksPerQuarter * 16;
    std::vector<Note> notes;            ///< sorted by start, then pitch
    std::vector<ControlEvent> controls; ///< sorted by time
    Tick loopLength = 0;                ///< more than 0: the first `loopLength` ticks repeat to fill the clip (beat patterns)

    Tick end() const noexcept { return start + length; }
    bool operator== (const Clip&) const = default;
};

using ClipPtr = std::shared_ptr<const Clip>;

/** Where a track's MIDI goes. */
struct TrackOutput
{
    enum class Kind : std::uint8_t { none, soundFont, midiPort, sampler };

    Kind kind = Kind::soundFont;
    std::string port;  ///< MIDI output port name (midiPort only)
    int channel = 1;   ///< 1..16

    bool operator== (const TrackOutput&) const = default;
};

/** The sound picked for a track. -1 means "don't send". */
struct TrackSound
{
    std::string name;  ///< as shown, for example "Strings > Orchestra Strings"
    int bankMsb = -1;
    int bankLsb = -1;
    int program = -1;  ///< 0..127

    bool operator== (const TrackSound&) const = default;
};

/** One pad of a sampler track's kit, as saved in the project. The audio stays in its file. */
struct KitPad
{
    std::string name;
    int note = 36;               ///< MIDI note that plays the pad
    std::string sampleFile;      ///< WAV file path
    std::uint64_t sliceStart = 0; ///< part of the file to play, in frames
    std::uint64_t sliceEnd = 0;   ///< 0 = to the end of the file
    float gainDb = 0.0f;
    float pan = 0.0f;            ///< -1 left .. 1 right
    float tuneSemitones = 0.0f;
    int chokeGroup = 0;          ///< pads in the same group (not 0) cut each other
    bool oneShot = true;         ///< false: the pad stops at note-off

    bool operator== (const KitPad&) const = default;
};

/** The kit of a sampler track: drum hits, or the slices of a loop. */
struct KitSettings
{
    std::string name;
    std::vector<KitPad> pads;

    const KitPad* padForNote (int note) const noexcept
    {
        for (const auto& p : pads)
            if (p.note == note)
                return &p;
        return nullptr;
    }

    bool operator== (const KitSettings&) const = default;
};

/** Mixer settings. Values are MIDI-scaled so they map 1:1 to hardware (CC7, CC10, CC91, CC93). */
struct MixerStrip
{
    int volume = 100;     ///< 0..127
    int pan = 64;         ///< 0..127, 64 = centre
    int reverbSend = 40;  ///< 0..127
    int chorusSend = 0;   ///< 0..127
    bool mute = false;
    bool solo = false;

    bool operator== (const MixerStrip&) const = default;
};

struct Track
{
    TrackId id = 0;
    std::string name = "Track";
    std::string colour = "teal";
    TrackOutput output;
    TrackSound sound;
    KitSettings kit;     ///< sampler tracks only
    MixerStrip mixer;
    bool armed = false;
    std::string inputPort; ///< record from this MIDI input only; empty = all inputs
    std::vector<ClipPtr> clips; ///< sorted by start
    std::vector<ClipPtr> slots; ///< Clip grid: slots[scene], null where empty. A slot clip loops over its length.

    bool operator== (const Track& o) const;
};

struct MetronomeSettings
{
    bool enabled = true;
    float level = 0.7f;       ///< 0..1
    bool accentDownbeat = true;

    bool operator== (const MetronomeSettings&) const = default;
};

struct LoopRange
{
    bool enabled = false;
    Tick start = 0;
    Tick end = kTicksPerQuarter * 16;

    bool isValid() const noexcept { return end > start && start >= 0; }
    bool operator== (const LoopRange&) const = default;
};

/** Master output, shown as the last mixer strip. */
struct MasterStrip
{
    float gainDb = 0.0f;
    bool operator== (const MasterStrip&) const = default;
};

/**
    The song: everything that is saved in a project file.
*/
struct Song
{
    std::string title = "Untitled";
    TempoMap tempo;
    TimeSignature timeSignature;
    std::string key = "C major";
    std::vector<Track> tracks;
    MetronomeSettings metronome;
    LoopRange loop;
    MasterStrip master;
    std::vector<std::string> scenes;          ///< Clip grid row names; rows without a name show their number
    Tick launchQuantise = kTicksPerQuarter * 4; ///< Clip grid: launches wait for the next multiple of this; 0 = at once
    std::string soundFont;   ///< path to the SF2 used by SoundFont tracks; empty = app default
    std::uint32_t nextId = 1; ///< for new track and clip ids

    /** Tempo at the start of the song. */
    double baseTempo() const noexcept { return tempo.getPoints().front().bpm; }

    /** Rows of the Clip grid: enough for every used slot and scene name, at least 8. */
    int sceneCount() const noexcept;

    /** End of the last clip, at least one bar. */
    Tick contentEnd() const noexcept;

    bool operator== (const Song&) const = default;
};

/** What a track's Clip grid column is doing. */
struct SlotStatus
{
    static constexpr int kNone = -2;
    int playing = -1;     ///< scene that plays; -1 = the Timeline plays
    int pending = kNone;  ///< scene waiting for its launch time; -1 = a stop is waiting; kNone = nothing
    double launchTick = 0.0; ///< song position where the playing slot started its first loop
};

} // namespace arranger

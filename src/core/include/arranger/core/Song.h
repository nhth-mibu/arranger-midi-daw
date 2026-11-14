#pragma once

#include "arranger/core/StyleRules.h"
#include "arranger/core/TempoMap.h"
#include "arranger/core/Time.h"

#include <cstdint>
#include <memory>
#include <optional>
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

/**
    A piece of an audio file on the Timeline of an audio track. The audio
    stays in its file. The clip plays the file at its own speed from `start`.
*/
struct AudioClip
{
    ClipId id = 0;
    std::string name;
    std::string file;       ///< WAV file path
    Tick start = 0;
    Tick length = kTicksPerQuarter * 4;
    double offset = 0.0;    ///< where in the file the clip starts, in seconds
    float gainDb = 0.0f;
    Tick fadeIn = 0;
    Tick fadeOut = 0;
    double bpm = 0.0;       ///< the tempo of the file. More than 0: the clip is stretched to follow the song tempo.

    Tick end() const noexcept { return start + length; }
    bool operator== (const AudioClip&) const = default;
};

using AudioClipPtr = std::shared_ptr<const AudioClip>;

/** One plugin in a slot: which plugin it is, and its settings as the plugin saved them. */
struct PluginSlot
{
    std::string format; ///< "VST3", "AudioUnit", or "Internal" for the app's own effects
    std::string id;     ///< what the plugin format uses to find the plugin again
    std::string name;   ///< as shown
    std::string state;  ///< the plugin's settings, base64; empty = the plugin's defaults
    bool bypass = false;

    bool empty() const noexcept { return id.empty(); }
    bool operator== (const PluginSlot&) const = default;
};

/** Where a track's MIDI goes. An `audio` track has no MIDI: it plays audio clips.
    A `plugin` track plays the instrument plugin in Track::instrument. */
struct TrackOutput
{
    enum class Kind : std::uint8_t { none, soundFont, midiPort, sampler, audio, plugin };

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

/** One point of an automation line: the value at a song position. Between points the value moves in a straight line. */
struct AutomationPoint
{
    Tick at = 0;
    float value = 100.0f; ///< 0..127, as the mixer: volume 100 = no change, pan 64 = centre

    bool operator== (const AutomationPoint&) const = default;
};

using AutomationLine = std::vector<AutomationPoint>; ///< sorted by time; empty = no automation

namespace automation
{
    /** The value of a line at a song position. Before the first point and after the last, the value stays level. */
    inline float valueAt (const AutomationLine& line, double tick, float whenEmpty) noexcept
    {
        if (line.empty())
            return whenEmpty;
        if (tick <= static_cast<double> (line.front().at))
            return line.front().value;
        for (std::size_t i = 1; i < line.size(); ++i)
            if (tick < static_cast<double> (line[i].at))
            {
                const auto& a = line[i - 1];
                const auto& b = line[i];
                const double span = static_cast<double> (b.at - a.at);
                const double t = span > 0.0 ? (tick - static_cast<double> (a.at)) / span : 1.0;
                return a.value + (b.value - a.value) * static_cast<float> (t);
            }
        return line.back().value;
    }
}

/** Something written on the score at a time: a chord symbol, a lyric, a dynamic or an articulation. */
struct ScoreMark
{
    enum class Kind : std::uint8_t { chord, lyric, dynamic, articulation };

    Kind kind = Kind::chord;
    Tick at = 0;      ///< song position
    std::string text; ///< "Cm7"; "Twin-"; "mf"; "staccato", "accent", "tenuto", "marcato" or "fermata"

    bool operator== (const ScoreMark&) const = default;
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
    int styleChannel = 0;       ///< 1..16: this track is a source channel of the song's style; 0 = an ordinary track
    std::vector<ScoreMark> marks; ///< for the Score view; sorted by time
    std::vector<AudioClipPtr> audio; ///< audio tracks only; sorted by start
    std::vector<AudioClipPtr> audioSlots; ///< Clip grid of an audio track: audioSlots[scene], null where empty. The clip starts at 0 and loops over its length.
    AutomationLine volumeAutomation; ///< with points, this line sets the volume in place of the mixer fader
    AutomationLine panAutomation;    ///< the same for pan
    PluginSlot instrument;           ///< plugin tracks only
    std::vector<PluginSlot> effects; ///< insert effects, in order. Not for MIDI port tracks: their sound is outside the app.

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
    std::vector<PluginSlot> effects; ///< insert effects on the whole mix
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
    std::optional<StyleData> style; ///< set: the Clip grid is an arranger style (scenes are its sections)

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

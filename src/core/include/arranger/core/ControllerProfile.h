#pragma once

#include "arranger/core/Song.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace arranger
{

/** What a controller's knob or button does in the app. */
enum class ControlAction : std::uint8_t
{
    none,
    volume, pan, reverb, chorus,   // per strip, value 0..127
    mute, solo, arm, select,       // per strip, on press
    panCentre,                     // per strip, on press
    masterVolume,                  // value 0..127
    play, stop, record, loop,      // on press
    bankLeft, bankRight,           // on press: move the 8 strips along the tracks
    launchClip,                    // per strip, on press: start the Clip grid slot under the pad
    launchScene, stopClips,        // on press: start the pads' top row as a scene; stop all clips
    sceneUp, sceneDown,            // on press: move the pads along the scenes
};

/** A controller message turned into something to do. */
struct SurfaceEvent
{
    ControlAction action = ControlAction::none;
    int strip = 0;   ///< 0-based position on the controller
    int value = 0;   ///< 0..127 for knobs and faders
    int row = 0;     ///< pad row for launchClip, 0 = top
    bool released = false; ///< a clip pad went up (only launchClip reports this)
};

/** A pad light to set: a note-on to the controller with the colour as velocity. */
struct PadLight
{
    int channel = 1, note = 0, colour = 0;
    bool operator== (const PadLight&) const = default;
};


/**
    A ready-made mixer page for a hardware controller: which knob, fader and
    button does what. Files are YAML in resources/controllers; see
    docs/controllers.md.
*/
struct ControllerProfile
{
    struct Binding
    {
        bool isNote = false;     ///< note on, otherwise control change
        int channel = 0;         ///< 1..16, 0 = any
        int number = 0;          ///< first CC or note number
        int count = 1;           ///< consecutive numbers, one per strip
        int firstStrip = 0;      ///< first strip this binding controls; count adds to it
        ControlAction action = ControlAction::none;
        std::string mode;        ///< only in this mode; empty = always
        int row = 0;             ///< launchClip: which pad row this is, 0 = top
    };

    /** How the pads show the Clip grid. Colours are the controller's own palette numbers. */
    struct Pads
    {
        int channel = 1, flashChannel = 1, pulseChannel = 1; ///< steady, flashing and pulsing light
        int empty = 0, clip = 3, playing = 21, waiting = 21;
        std::vector<std::pair<std::string, int>> colours;    ///< track colour name -> palette number
    };

    struct ModeSwitch
    {
        std::string name;
        bool isNote = false;
        int channel = 0;
        int number = 0;
        int value = -1;          ///< -1 = any value above 0
    };

    std::string id, name, maker, notes;
    std::vector<std::string> ports;   ///< parts of the MIDI port names (in and out)
    /** Parts of the names of the controller's other ports, its keys: they play tracks as a keyboard,
        but are turned on with the controller when it is plugged in. */
    std::vector<std::string> keyPorts;
    int channel = 0;                  ///< the controller's own channel; 0 = the whole port is the controller
    int strips = 8;
    std::string defaultMode;
    std::vector<std::vector<std::uint8_t>> connect, disconnect; ///< sent to the controller's output port
    std::vector<ModeSwitch> modes;
    std::vector<Binding> bindings;
    Pads pads;

    /** Number of pad rows that start clips (0 = this controller has none). */
    int padRows() const noexcept;

    bool matchesPort (const std::string& portName) const;
    /** True for the controller page's ports and for its key ports. */
    bool isPortOfDevice (const std::string& portName) const;

    /** True when this message belongs to the controller and must not reach tracks. */
    bool owns (std::uint8_t status) const noexcept;
};

namespace controller_profile
{
    /** Reads a profile from YAML. Throws device_profile::Error with the line number. */
    ControllerProfile parse (const std::string& yaml);

    /** Writes the profile as YAML, in the same form parse() reads. */
    std::string write (const ControllerProfile&);
}

/**
    The lights for a controller's clip pads: off for an empty slot, the track's
    colour for a clip, pulsing while it plays, flashing while it waits to start.
    `firstTrack` and `firstScene` say which part of the Clip grid the pads show;
    `status (trackIndex)` gives what each track plays.
*/
std::vector<PadLight> padLights (const ControllerProfile&, const Song&, int firstTrack, int firstScene,
                                 const std::function<SlotStatus (int trackIndex)>& status);

/** One connected controller: follows its mode and translates its messages. */
class ControllerSurface
{
public:
    explicit ControllerSurface (ControllerProfile);

    const ControllerProfile& profile() const noexcept { return prof; }
    const std::string& mode() const noexcept { return currentMode; }

    /** Translates a message. Empty when it isn't bound (or only changed the mode). */
    std::optional<SurfaceEvent> handle (const std::array<std::uint8_t, 3>& bytes);

private:
    ControllerProfile prof;
    std::string currentMode;
};

} // namespace arranger

#pragma once

#include <array>
#include <cstdint>
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
};

/** A controller message turned into something to do. */
struct SurfaceEvent
{
    ControlAction action = ControlAction::none;
    int strip = 0;   ///< 0-based position on the controller
    int value = 0;   ///< 0..127 for knobs and faders
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
        ControlAction action = ControlAction::none;
        std::string mode;        ///< only in this mode; empty = always
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
    int channel = 0;                  ///< the controller's own channel; 0 = the whole port is the controller
    int strips = 8;
    std::string defaultMode;
    std::vector<std::vector<std::uint8_t>> connect, disconnect; ///< sent to the controller's output port
    std::vector<ModeSwitch> modes;
    std::vector<Binding> bindings;

    bool matchesPort (const std::string& portName) const;

    /** True when this message belongs to the controller and must not reach tracks. */
    bool owns (std::uint8_t status) const noexcept;
};

namespace controller_profile
{
    /** Reads a profile from YAML. Throws device_profile::Error with the line number. */
    ControllerProfile parse (const std::string& yaml);
}

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

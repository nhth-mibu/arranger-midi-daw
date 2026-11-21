#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace arranger
{

/**
    Turns on the MIDI inputs of known instruments and controllers when they are
    plugged in, and remembers the inputs the user turned off by hand.

    The audio settings only keep the inputs that were on when the app last
    quit, so a keyboard or controller plugged in later stayed off. This class
    looks at the inputs every few seconds:

    - An input that appears (at start or plugged in later) is turned on, unless
      the user turned it off before. That holds for every keyboard, synth and
      controller, with or without a profile. Only software ports (IAC, network
      sessions, loopMIDI, MIDI Through) stay as they are, unless a profile or
      controller page names them: they can loop MIDI back into the app.
    - An input that changes between two looks was changed by the user (in the
      settings window or by the AI): turning it off is remembered, turning it
      on again forgets that.
*/
class MidiInputPolicy
{
public:
    struct Port
    {
        std::string name;
        bool enabled = false;
    };

    /** True for the name of a software MIDI port (a virtual bus, not an instrument). */
    static bool looksVirtual (const std::string& portName);

    /** Returns the inputs to turn on now. `isKnown` says if a profile or controller page names the port. */
    std::vector<std::string> update (const std::vector<Port>& ports, const std::function<bool (const std::string&)>& isKnown);

    /** An input update() asked for could not be turned on: it is still off, and not by the user. */
    void couldNotEnable (const std::string& name) { last[name] = false; }

    /** Inputs the user turned off by hand (kept in the app settings). */
    const std::set<std::string>& turnedOff() const noexcept { return off; }
    void setTurnedOff (std::set<std::string> names) { off = std::move (names); }

private:
    std::set<std::string> off;
    std::map<std::string, bool> last; ///< state at the last look, for the inputs present then
};

} // namespace arranger

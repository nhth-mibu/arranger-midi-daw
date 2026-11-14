#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace arranger::platform
{

struct AudioStatus
{
    bool running = false;
    std::string driver;     ///< "CoreAudio", "ASIO", "ALSA", ...
    std::string device;
    double sampleRate = 0.0;
    int bufferSize = 0;
    double outputLatencyMs = 0.0;
    int inputs = 0;         ///< open audio input channels (0: recording audio is not possible)
    double inputLatencyMs = 0.0;
    double cpuLoad = 0.0;   ///< 0..1, share of the callback time used
};

struct MidiPort
{
    std::string name;
    std::string id;
    bool enabled = false;
};

struct IncomingMidi
{
    std::array<std::uint8_t, 3> bytes {};
    std::uint8_t size = 0;
    std::string port;
};

/** Input that belongs to a control surface: kept away from tracks and recording. */
struct ControlFilter
{
    std::string port;  ///< part of the input port name, lower case
    int channel = 0;   ///< 1..16, or 0 for every message on the port
};

/**
    Audio and MIDI hardware, as the rest of the app sees it. The JUCE
    implementation lives in platform/juce; a different backend (for example
    RtAudio and RtMidi) only has to implement this interface.
*/
class DeviceHost
{
public:
    virtual ~DeviceHost() = default;

    virtual AudioStatus audioStatus() const = 0;
    virtual std::vector<MidiPort> midiInputs() const = 0;
    virtual std::vector<MidiPort> midiOutputs() const = 0;

    /** MIDI that arrived since the last call, oldest first. Call from the UI thread. */
    virtual std::vector<IncomingMidi> takeIncomingMidi() = 0;

    /** Messages matching a filter still arrive through takeIncomingMidi(), but don't reach the engine. */
    virtual void setControlFilters (std::vector<ControlFilter>) = 0;

    /** Live MIDI reaches the engine (thru and recording) only from inputs whose name contains
        `port` (lower case) and only on `channel` (1..16). Empty and 0 let everything through.
        The messages still arrive through takeIncomingMidi(). */
    virtual void setPlayFilter (const std::string& /*port*/, int /*channel*/) {}

    /** Turns a MIDI input on or off by its name. Returns false when no input has that name. */
    virtual bool setMidiInputEnabled (const std::string& /*name*/, bool /*on*/) { return false; }

    /** The hardware outputs the engine's port numbers refer to: names[i] is port i + 1. */
    virtual void setOutputPorts (const std::vector<std::string>& names) = 0;

    /** Sends raw bytes (SysEx included) to a named output right away, outside the engine. */
    virtual void sendNow (const std::string& port, const std::vector<std::uint8_t>& bytes) = 0;

    /** Stops calling the engine from the audio device (true), and starts again (false). For offline rendering. */
    virtual void pauseAudio (bool) {}
};

} // namespace arranger::platform

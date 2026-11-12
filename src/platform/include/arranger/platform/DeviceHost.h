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
};

} // namespace arranger::platform

#pragma once

#include "arranger/engine/Engine.h"
#include "arranger/platform/DeviceHost.h"
#include "arranger/engine/AudioCapture.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <mutex>

namespace arranger::platform
{

/**
    DeviceHost on top of juce::AudioDeviceManager. Feeds the engine from the
    audio callback, timestamps live MIDI into the block, and sends the
    engine's hardware MIDI from a dedicated thread at the moment the matching
    audio reaches the speakers.
*/
class JuceDeviceHost final : public DeviceHost,
                             private juce::AudioIODeviceCallback,
                             private juce::MidiInputCallback
{
public:
    explicit JuceDeviceHost (Engine&);
    ~JuceDeviceHost() override;

    /** Opens the audio device from saved settings (or the default) and enables MIDI inputs.
        Pass openDevices = false to run without hardware (screenshots, tests). */
    void start (const juce::XmlElement* savedState, bool openDevices);

    std::unique_ptr<juce::XmlElement> saveState() const;
    juce::AudioDeviceManager& deviceManager() noexcept { return manager; }

    /** Microphone audio for speech input, independent of the transport. */
    void startSpeechCapture();
    std::vector<float> stopSpeechCapture (double& sampleRate);
    bool isSpeechCapturing() const noexcept { return speechCaptureOn.load (std::memory_order_relaxed); }

    AudioStatus audioStatus() const override;
    std::vector<MidiPort> midiInputs() const override;
    std::vector<MidiPort> midiOutputs() const override;
    std::vector<IncomingMidi> takeIncomingMidi() override;
    void setControlFilters (std::vector<ControlFilter>) override;
    void setPlayFilter (const std::string& port, int channel) override;
    bool setMidiInputEnabled (const std::string& name, bool on) override;
    void setOutputPorts (const std::vector<std::string>& names) override;
    void sendNow (const std::string& port, const std::vector<std::uint8_t>& bytes) override;
    void pauseAudio (bool paused) override;
    int openAudioInput (int first, int count) override;
    std::vector<IncomingSysEx> takeIncomingSysEx() override;

private:
    struct TimedMessage
    {
        double dueMs = 0.0;
        std::uint16_t port = 0;
        std::array<std::uint8_t, 3> bytes {};
        std::uint8_t size = 0;
    };

    struct LiveInput
    {
        double timeMs = 0.0;
        std::array<std::uint8_t, 3> bytes {};
        std::uint8_t size = 0;
    };

    class Sender;

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    Engine& engine;
    juce::AudioDeviceManager manager;
    AudioCapture speechCapture { std::size_t { 1 } << 20 }; // about 20 seconds at 48 kHz
    std::atomic<bool> speechCaptureOn { false };
    MidiEventBuffer midiIn { 1024 }, midiOut { 4096 };
    bool started = false;
    double latencyMs = 0.0;
    double lastDueMs = 0.0; ///< audio thread only: hardware MIDI never leaves before what came earlier
    double sampleRate = 48000.0;

    // MIDI input -> audio thread. Several MIDI threads may write; the audio
    // thread only ever try-locks, so it never waits.
    juce::SpinLock liveLock;
    std::vector<LiveInput> live;

    std::mutex filterLock;
    std::vector<ControlFilter> filters;
    std::string playPort; ///< lower case; guarded by filterLock
    int playChannel = 0;

    // MIDI input -> UI monitor
    mutable std::mutex incomingLock;
    std::vector<IncomingMidi> incoming;
    std::vector<IncomingSysEx> incomingSysEx;

    // Engine -> hardware outputs
    SpscQueue<TimedMessage, 8192> outgoing;
    std::unique_ptr<Sender> sender;
};

} // namespace arranger::platform

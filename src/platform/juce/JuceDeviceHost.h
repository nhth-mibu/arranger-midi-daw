#pragma once

#include "arranger/engine/Engine.h"
#include "arranger/platform/DeviceHost.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <mutex>

namespace arranger::platform
{

/** DeviceHost on top of juce::AudioDeviceManager. Feeds the engine from the audio callback. */
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

    /** Current settings, to save between runs. */
    std::unique_ptr<juce::XmlElement> saveState() const;

    juce::AudioDeviceManager& deviceManager() noexcept { return manager; }

    AudioStatus audioStatus() const override;
    std::vector<MidiPort> midiInputs() const override;
    std::vector<MidiPort> midiOutputs() const override;
    std::vector<IncomingMidi> takeIncomingMidi() override;

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    Engine& engine;
    juce::AudioDeviceManager manager;
    MidiEventBuffer midiIn { 512 }, midiOut { 512 };
    bool started = false;

    mutable std::mutex incomingLock; // MIDI threads -> UI thread only, never the audio thread
    std::vector<IncomingMidi> incoming;
};

} // namespace arranger::platform

#include "JuceDeviceHost.h"

namespace arranger::platform
{

JuceDeviceHost::JuceDeviceHost (Engine& e) : engine (e) {}

JuceDeviceHost::~JuceDeviceHost()
{
    manager.removeMidiInputDeviceCallback ({}, this);
    manager.removeAudioCallback (this);
    manager.closeAudioDevice();
}

void JuceDeviceHost::start (const juce::XmlElement* savedState, bool openDevices)
{
    if (! openDevices || started)
        return;
    started = true;

    // Outputs only for now: audio recording arrives in Phase 6.
    const auto error = manager.initialise (0, 2, savedState, true);
    if (error.isNotEmpty())
        DBG ("Audio device: " << error);

    // First run: listen to every MIDI keyboard that is plugged in.
    if (savedState == nullptr)
        for (const auto& input : juce::MidiInput::getAvailableDevices())
            manager.setMidiInputDeviceEnabled (input.identifier, true);

    manager.addMidiInputDeviceCallback ({}, this);
    manager.addAudioCallback (this);
}

std::unique_ptr<juce::XmlElement> JuceDeviceHost::saveState() const
{
    return manager.createStateXml();
}

AudioStatus JuceDeviceHost::audioStatus() const
{
    AudioStatus s;
    if (auto* device = manager.getCurrentAudioDevice(); device != nullptr && device->isOpen())
    {
        s.running = device->isPlaying();
        s.driver = device->getTypeName().toStdString();
        s.device = device->getName().toStdString();
        s.sampleRate = device->getCurrentSampleRate();
        s.bufferSize = device->getCurrentBufferSizeSamples();
        if (s.sampleRate > 0)
            s.outputLatencyMs = 1000.0 * (device->getOutputLatencyInSamples() + s.bufferSize) / s.sampleRate;
        s.cpuLoad = manager.getCpuUsage();
    }
    return s;
}

std::vector<MidiPort> JuceDeviceHost::midiInputs() const
{
    std::vector<MidiPort> ports;
    for (const auto& d : juce::MidiInput::getAvailableDevices())
        ports.push_back ({ d.name.toStdString(), d.identifier.toStdString(), manager.isMidiInputDeviceEnabled (d.identifier) });
    return ports;
}

std::vector<MidiPort> JuceDeviceHost::midiOutputs() const
{
    std::vector<MidiPort> ports;
    const auto defaultOut = manager.getDefaultMidiOutputIdentifier();
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        ports.push_back ({ d.name.toStdString(), d.identifier.toStdString(), d.identifier == defaultOut });
    return ports;
}

std::vector<IncomingMidi> JuceDeviceHost::takeIncomingMidi()
{
    std::lock_guard lock (incomingLock);
    std::vector<IncomingMidi> taken;
    taken.swap (incoming);
    return taken;
}

void JuceDeviceHost::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m)
{
    if (m.getRawDataSize() > 3 || m.isActiveSense() || m.isMidiClock())
        return; // SysEx, clock and active sensing are handled in later phases

    IncomingMidi in;
    in.size = static_cast<std::uint8_t> (m.getRawDataSize());
    std::copy (m.getRawData(), m.getRawData() + in.size, in.bytes.begin());
    in.port = source != nullptr ? source->getName().toStdString() : std::string {};

    std::lock_guard lock (incomingLock);
    if (incoming.size() < 4096) // the UI drains this many times a second
        incoming.push_back (std::move (in));
}

void JuceDeviceHost::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    engine.prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void JuceDeviceHost::audioDeviceStopped() {}

void JuceDeviceHost::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                       float* const* outputs, int numOutputs, int numSamples,
                                                       const juce::AudioIODeviceCallbackContext&)
{
    AudioBlock block { outputs, numOutputs, numSamples };
    midiIn.clear();
    midiOut.clear();
    engine.process (block, midiIn, midiOut);
    // midiOut (the metronome's MIDI click) is routed to hardware ports in Phase 1.
}

} // namespace arranger::platform

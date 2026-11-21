#include "JuceDeviceHost.h"

#include <algorithm>

namespace arranger::platform
{

// ---------------------------------------------------------------------------
/** Sends timed MIDI to hardware ports when it falls due. Owns the open outputs. */
class JuceDeviceHost::Sender final : public juce::Thread
{
public:
    explicit Sender (SpscQueue<TimedMessage, 8192>& q) : juce::Thread ("MIDI out"), queue (q) { pending.reserve (4096); }
    ~Sender() override { stopThread (2000); }

    void setPorts (const std::vector<std::string>& names)
    {
        std::lock_guard lock (portsLock);
        std::vector<std::unique_ptr<juce::MidiOutput>> next;
        const auto available = juce::MidiOutput::getAvailableDevices();
        for (const auto& name : names)
        {
            std::unique_ptr<juce::MidiOutput> out;
            // Reuse an already open port of the same name.
            for (auto& existing : outputs)
                if (existing != nullptr && existing->getName().toStdString() == name)
                    out = std::move (existing);
            if (out == nullptr)
                for (const auto& d : available)
                    if (d.name.toStdString() == name)
                        out = juce::MidiOutput::openDevice (d.identifier);
            next.push_back (std::move (out));
        }
        outputs = std::move (next);
    }

    void sendNow (const std::string& name, const std::vector<std::uint8_t>& bytes)
    {
        std::lock_guard lock (portsLock);
        for (auto& out : outputs)
            if (out != nullptr && out->getName().toStdString() == name)
            {
                out->sendMessageNow (juce::MidiMessage (bytes.data(), static_cast<int> (bytes.size())));
                return;
            }
        // Not one of the engine's ports: open it just for this.
        for (const auto& d : juce::MidiOutput::getAvailableDevices())
            if (d.name.toStdString() == name)
                if (auto out = juce::MidiOutput::openDevice (d.identifier))
                    out->sendMessageNow (juce::MidiMessage (bytes.data(), static_cast<int> (bytes.size())));
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            while (auto m = queue.pop())
                pending.push_back (*m);

            if (! pending.empty())
            {
                const double now = juce::Time::getMillisecondCounterHiRes();
                std::stable_sort (pending.begin(), pending.end(), [] (auto& a, auto& b) { return a.dueMs < b.dueMs; });
                std::size_t sent = 0;
                {
                    std::lock_guard lock (portsLock);
                    for (; sent < pending.size() && pending[sent].dueMs <= now + 0.5; ++sent)
                    {
                        const auto& m = pending[sent];
                        const auto index = static_cast<std::size_t> (m.port - 1);
                        if (m.port >= 1 && index < outputs.size() && outputs[index] != nullptr)
                            outputs[index]->sendMessageNow (juce::MidiMessage (m.bytes.data(), m.size));
                    }
                }
                pending.erase (pending.begin(), pending.begin() + static_cast<std::ptrdiff_t> (sent));
            }
            wait (1);
        }
    }

private:
    SpscQueue<TimedMessage, 8192>& queue;
    std::vector<TimedMessage> pending;
    std::mutex portsLock;
    std::vector<std::unique_ptr<juce::MidiOutput>> outputs;
};

// ---------------------------------------------------------------------------
JuceDeviceHost::JuceDeviceHost (Engine& e) : engine (e)
{
    live.reserve (1024);
    sender = std::make_unique<Sender> (outgoing);
}

JuceDeviceHost::~JuceDeviceHost()
{
    manager.removeMidiInputDeviceCallback ({}, this);
    manager.removeAudioCallback (this);
    manager.closeAudioDevice();
    sender = nullptr;
}

void JuceDeviceHost::start (const juce::XmlElement* savedState, bool openDevices)
{
    if (! openDevices || started)
        return;
    started = true;

    // Two inputs for audio recording. Settings saved without an input keep it closed.
    const auto error = manager.initialise (2, 2, savedState, true);
    if (error.isNotEmpty())
        DBG ("Audio device: " << error);

    // First run: listen to every MIDI keyboard that is plugged in.
    if (savedState == nullptr)
        for (const auto& input : juce::MidiInput::getAvailableDevices())
            manager.setMidiInputDeviceEnabled (input.identifier, true);

    manager.addMidiInputDeviceCallback ({}, this);
    manager.addAudioCallback (this);
    sender->startThread (juce::Thread::Priority::high);
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
        s.inputs = device->getActiveInputChannels().countNumberOfSetBits();
        for (const auto& n : device->getInputChannelNames())
            s.inputNames.push_back (n.toStdString());
        if (s.sampleRate > 0)
            s.inputLatencyMs = 1000.0 * device->getInputLatencyInSamples() / s.sampleRate;
        s.cpuLoad = manager.getCpuUsage();
    }
    return s;
}

int JuceDeviceHost::openAudioInput (int first, int count)
{
    auto* device = manager.getCurrentAudioDevice();
    if (device == nullptr || ! device->isOpen() || first < 0 || count < 1 || first + count > device->getInputChannelNames().size())
        return -1;
    auto active = device->getActiveInputChannels();
    bool missing = false;
    for (int c = first; c < first + count; ++c)
        missing = missing || ! active[c];
    if (missing)
    {
        // Keep the inputs that are open, add the ones the track wants.
        auto setup = manager.getAudioDeviceSetup();
        setup.useDefaultInputChannels = false;
        setup.inputChannels = active;
        setup.inputChannels.setRange (first, count, true);
        if (manager.setAudioDeviceSetup (setup, true).isNotEmpty())
            return -1;
        device = manager.getCurrentAudioDevice();
        if (device == nullptr)
            return -1;
        active = device->getActiveInputChannels();
        if (! active[first])
            return -1;
    }
    // The callback gets only the open inputs, in order.
    int index = 0;
    for (int c = 0; c < first; ++c)
        index += active[c] ? 1 : 0;
    return index;
}

void JuceDeviceHost::setPlayFilter (const std::string& port, int channel)
{
    std::lock_guard lock (filterLock);
    playPort = juce::String (port).toLowerCase().toStdString();
    playChannel = channel;
}

bool JuceDeviceHost::setMidiInputEnabled (const std::string& name, bool on)
{
    for (const auto& d : juce::MidiInput::getAvailableDevices())
        if (d.name.toStdString() == name)
        {
            manager.setMidiInputDeviceEnabled (d.identifier, on);
            return true;
        }
    return false;
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
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        ports.push_back ({ d.name.toStdString(), d.identifier.toStdString(), true });
    return ports;
}

std::vector<IncomingMidi> JuceDeviceHost::takeIncomingMidi()
{
    std::lock_guard lock (incomingLock);
    std::vector<IncomingMidi> taken;
    taken.swap (incoming);
    return taken;
}

void JuceDeviceHost::setOutputPorts (const std::vector<std::string>& names)
{
    sender->setPorts (names);
}

void JuceDeviceHost::sendNow (const std::string& port, const std::vector<std::uint8_t>& bytes)
{
    sender->sendNow (port, bytes);
}

std::vector<IncomingSysEx> JuceDeviceHost::takeIncomingSysEx()
{
    std::lock_guard lock (incomingLock);
    std::vector<IncomingSysEx> taken;
    taken.swap (incomingSysEx);
    return taken;
}

void JuceDeviceHost::pauseAudio (bool paused)
{
    if (! started)
        return;
    if (paused)
        manager.removeAudioCallback (this); // waits for a running callback to end
    else
        manager.addAudioCallback (this);    // prepares the engine again for the device
}

void JuceDeviceHost::setControlFilters (std::vector<ControlFilter> f)
{
    std::lock_guard lock (filterLock);
    filters = std::move (f);
}

void JuceDeviceHost::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m)
{
    if (m.isSysEx())
    {
        // Kept for the sound editors: synth dumps and answers to requests.
        IncomingSysEx x;
        x.bytes.assign (m.getRawData(), m.getRawData() + m.getRawDataSize());
        x.port = source != nullptr ? source->getName().toStdString() : std::string();
        std::lock_guard lock (incomingLock);
        if (incomingSysEx.size() < 1024)
            incomingSysEx.push_back (std::move (x));
        return;
    }
    if (m.getRawDataSize() > 3 || m.isActiveSense() || m.isMidiClock())
        return;

    const auto portName = source != nullptr ? source->getName() : juce::String();
    bool notForEngine = false;
    {
        std::lock_guard lock (filterLock);
        const auto lowerName = portName.toLowerCase().toStdString();
        const int channel = m.getChannel(); // 0 for system messages
        for (const auto& f : filters)
            if (lowerName.find (f.port) != std::string::npos && (f.channel == 0 || f.channel == channel))
                notForEngine = true;
        // Not from the input the track records from: the UI still sees it, the engine doesn't.
        if (! playPort.empty() && lowerName.find (playPort) == std::string::npos)
            notForEngine = true;
        if (playChannel != 0 && channel != 0 && channel != playChannel)
            notForEngine = true;
    }

    LiveInput in;
    in.timeMs = juce::Time::getMillisecondCounterHiRes();
    in.size = static_cast<std::uint8_t> (m.getRawDataSize());
    std::copy (m.getRawData(), m.getRawData() + in.size, in.bytes.begin());

    if (! notForEngine)
    {
        const juce::SpinLock::ScopedLockType lock (liveLock);
        if (live.size() < 1024)
            live.push_back (in);
    }

    IncomingMidi ui;
    ui.bytes = in.bytes;
    ui.size = in.size;
    ui.port = portName.toStdString();
    std::lock_guard lock (incomingLock);
    if (incoming.size() < 4096)
        incoming.push_back (std::move (ui));
}

void JuceDeviceHost::startSpeechCapture()
{
    std::vector<float> discardL, discardR;
    speechCapture.pop (discardL, discardR);
    speechCaptureOn.store (true, std::memory_order_relaxed);
}

std::vector<float> JuceDeviceHost::stopSpeechCapture (double& rate)
{
    speechCaptureOn.store (false, std::memory_order_relaxed);
    rate = sampleRate;
    std::vector<float> left, right;
    speechCapture.pop (left, right);
    std::vector<float> mono (left.size());
    for (std::size_t i = 0; i < left.size(); ++i)
        mono[i] = (left[i] + right[i]) * 0.5f;
    return mono;
}

void JuceDeviceHost::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate = device->getCurrentSampleRate();
    const int bufferSize = device->getCurrentBufferSizeSamples();
    latencyMs = sampleRate > 0 ? 1000.0 * (device->getOutputLatencyInSamples() + bufferSize) / sampleRate : 0.0;
    engine.prepare (sampleRate, bufferSize);
}

void JuceDeviceHost::audioDeviceStopped()
{
    engine.released();
}

void JuceDeviceHost::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                       float* const* outputs, int numOutputs, int numSamples,
                                                       const juce::AudioIODeviceCallbackContext&)
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    midiIn.clear();
    midiOut.clear();

    // Live MIDI that arrived during the last block lands at the matching place in this one.
    if (liveLock.tryEnter())
    {
        for (const auto& in : live)
        {
            const double ageSamples = (nowMs - in.timeMs) * sampleRate / 1000.0;
            const int offset = std::clamp (numSamples - 1 - static_cast<int> (ageSamples), 0, std::max (0, numSamples - 1));
            midiIn.add (MidiEvent::make (offset, in.bytes[0], in.bytes[1], in.bytes[2], 0));
        }
        live.clear();
        liveLock.exit();
        midiIn.sortByTime();
    }

    AudioBlock block { outputs, numOutputs, numSamples };
    const AudioBlock input { const_cast<float* const*> (inputs), numInputs, numSamples };
    engine.process (block, midiIn, midiOut, input);

    // The speech input is separate from the transport. Keep it independent of the
    // recording path, which only captures while the song plays.
    if (speechCaptureOn.load (std::memory_order_relaxed) && numInputs > 0 && inputs != nullptr
        && inputs[0] != nullptr && numSamples > 0)
        speechCapture.push (inputs[0], numInputs > 1 ? inputs[1] : nullptr, numSamples);

    // Hardware MIDI leaves when the audio rendered alongside it reaches the speakers.
    // Callbacks do not come at exact times. Without the max below, a short note at the end of
    // one block could get a later time than its note-off early in the next block: the synth then
    // gets the note-off first and the note hangs.
    for (const auto& e : midiOut)
    {
        TimedMessage m;
        m.dueMs = std::max (lastDueMs, nowMs + latencyMs + 1000.0 * e.sampleOffset / sampleRate);
        lastDueMs = m.dueMs;
        m.port = e.port;
        m.bytes = e.bytes;
        m.size = e.size;
        outgoing.push (m);
    }
}

} // namespace arranger::platform

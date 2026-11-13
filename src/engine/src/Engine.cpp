#include "arranger/engine/Engine.h"

#include <algorithm>
#include <cmath>

namespace arranger
{

Engine::Engine()
{
    active.resize (kMaxTracks);
    snapshot = &emptySnapshot;
    transport.setTempoMap (&emptySnapshot.tempo);
    metronome.prepare ({});
    metronome.setSettings (emptySnapshot.metronome);
}

// ---- UI thread -----------------------------------------------------------------
Engine::Command Engine::make (Command::Type type)
{
    Command c;
    c.type = type;
    return c;
}

void Engine::send (const Command& c)
{
    // The audio thread drains the queue every block. If no device is running
    // and it fills up, commands are dropped; the UI re-sends state when a device starts.
    commands.push (c);
}

void Engine::play()       { send (make (Command::Type::play)); }
void Engine::stop()       { send (make (Command::Type::stop)); }
void Engine::togglePlay() { send (make (Command::Type::togglePlay)); }

void Engine::locate (Tick position)
{
    auto c = make (Command::Type::locate);
    c.number = static_cast<double> (position);
    send (c);
}

void Engine::setRecording (bool on)
{
    auto c = make (Command::Type::record);
    c.number = on ? 1.0 : 0.0;
    send (c);
}

void Engine::setThru (std::uint16_t port, int channel)
{
    auto c = make (Command::Type::thru);
    c.port = port;
    c.channel = std::clamp (channel, 0, 16);
    send (c);
}

void Engine::sendMidi (std::uint16_t port, std::uint8_t b0, std::uint8_t b1, std::uint8_t b2)
{
    auto c = make (Command::Type::midi);
    c.port = port;
    c.bytes = { b0, b1, b2 };
    send (c);
}

void Engine::setClickPort (std::uint16_t port)
{
    auto c = make (Command::Type::clickPort);
    c.port = port;
    send (c);
}

void Engine::setSnapshot (std::shared_ptr<const PlaybackSnapshot> s)
{
    snapshots.publish (std::move (s));
}

void Engine::setSamplers (std::shared_ptr<const SamplerSet> s)
{
    samplerSets.publish (std::move (s));
}

std::uint16_t Engine::samplerPort (TrackId id) const
{
    const auto snap = snapshots.current();
    return snap != nullptr ? snap->samplerPort (id) : MidiEvent::kNoPort;
}

void Engine::setInstrument (std::shared_ptr<Processor> p)
{
    instruments.publish (std::move (p));
}

void Engine::collectGarbage()
{
    snapshots.collectGarbage();
    instruments.collectGarbage();
    samplerSets.collectGarbage();
}

float Engine::takeMasterPeak (int channel) noexcept
{
    return masterPeak[static_cast<std::size_t> (channel & 1)].exchange (0.0f, std::memory_order_relaxed);
}

// ---- audio thread ----------------------------------------------------------------
void Engine::prepare (double rate, int maxBlockSize)
{
    blockRate = rate;
    transport.prepare (rate);
    metronome.prepare ({ rate, maxBlockSize });
    if (auto inst = instruments.current())
        inst->prepare ({ rate, maxBlockSize });
    if (const auto set = samplerSets.current())
        for (const auto& entry : set->entries)
            entry.sampler->prepare ({ rate, maxBlockSize });
    statusSampleRate.store (rate);
}

void Engine::released() noexcept
{
    snapshots.release();
    instruments.release();
    samplerSets.release();
}

void Engine::emit (const MidiEvent& e) noexcept
{
    events.add (e);
}

void Engine::apply (const Command& c) noexcept
{
    switch (c.type)
    {
        case Command::Type::play: startPlayback(); break;
        case Command::Type::stop: stopPlayback (0); break;
        case Command::Type::togglePlay:
            if (transport.isPlaying())
                stopPlayback (0);
            else
                startPlayback();
            break;
        case Command::Type::locate:
            releaseAllNotes (0);
            transport.locate (std::max (0.0, c.number));
            metronome.reset();
            break;
        case Command::Type::record: recording = c.number > 0.5; break;
        case Command::Type::thru:
            thruPort = c.port;
            thruChannel = c.channel;
            break;
        case Command::Type::clickPort: clickPort = c.port; break;
        case Command::Type::midi:
            emit (MidiEvent::make (0, c.bytes[0], c.bytes[1], c.bytes[2], c.port));
            break;
    }
}

void Engine::startPlayback() noexcept
{
    if (transport.isPlaying())
        return;
    transport.play();
    sendSetupOnPlay = true;
}

void Engine::stopPlayback (int offset) noexcept
{
    if (! transport.isPlaying())
        return;
    transport.stop();
    releaseAllNotes (offset);
    metronome.reset();
}

void Engine::releaseNotes (ActiveNotes& a, int offset) noexcept
{
    if (a.on.none())
        return;
    for (int pitch = 0; pitch < 128; ++pitch)
        if (a.on.test (static_cast<std::size_t> (pitch)))
            emit (MidiEvent::make (offset, static_cast<std::uint8_t> (0x80 | a.channel), static_cast<std::uint8_t> (pitch), 0, a.port));
    a.on.reset();
}

void Engine::releaseAllNotes (int offset) noexcept
{
    const auto n = std::min<std::size_t> (snapshot->tracks.size(), active.size());
    for (std::size_t i = 0; i < n; ++i)
        releaseNotes (active[i], offset);
}

void Engine::switchSnapshot (const PlaybackSnapshot* next, int offset) noexcept
{
    // Carry held notes over to tracks whose content didn't change; release the rest.
    std::array<std::bitset<128>, 64> carried {};
    std::array<TrackId, 64> carriedIds {};
    std::size_t carriedCount = 0;

    const auto oldCount = std::min<std::size_t> (snapshot->tracks.size(), active.size());
    for (std::size_t i = 0; i < oldCount; ++i)
    {
        auto& a = active[i];
        if (a.on.none())
            continue;
        bool keep = false;
        for (const auto& t : next->tracks)
            if (t.id == a.id && t.contentKey == a.contentKey)
                keep = true;
        if (keep && carriedCount < carried.size())
        {
            carried[carriedCount] = a.on;
            carriedIds[carriedCount++] = a.id;
        }
        else
        {
            releaseNotes (a, offset);
        }
    }

    snapshot = next;
    const auto newCount = std::min<std::size_t> (snapshot->tracks.size(), active.size());
    for (std::size_t i = 0; i < newCount; ++i)
    {
        const auto& t = snapshot->tracks[i];
        auto& a = active[i];
        a.id = t.id;
        a.port = t.port;
        a.channel = t.channel;
        a.contentKey = t.contentKey;
        a.on.reset();
        for (std::size_t k = 0; k < carriedCount; ++k)
            if (carriedIds[k] == t.id)
                a.on = carried[k];
    }

    transport.setTempoMap (&snapshot->tempo);
    transport.setTimeSignature (snapshot->timeSignature);
    metronome.setSettings (snapshot->metronome);
}

void Engine::sequence (const ProcessContext& ctx) noexcept
{
    if (! ctx.playing)
        return;

    const auto n = std::min<std::size_t> (snapshot->tracks.size(), active.size());
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto& track = snapshot->tracks[i];
        auto& a = active[i];

        if (sendSetupOnPlay)
            for (const auto& e : track.setup)
                emit (MidiEvent::make (ctx.blockOffset, e.bytes[0], e.bytes[1], e.bytes[2], track.port));

        const auto& ev = track.events;
        auto it = std::lower_bound (ev.begin(), ev.end(), ctx.tickAtStart,
                                    [] (const SequenceEvent& e, double t) { return static_cast<double> (e.at) < t; });
        for (; it != ev.end() && static_cast<double> (it->at) < ctx.tickAtEnd; ++it)
        {
            const auto s = ctx.sampleForTick (static_cast<double> (it->at)) - ctx.sampleTime;
            const int offset = ctx.blockOffset + static_cast<int> (std::clamp<std::int64_t> (s, 0, ctx.numSamples - 1));
            const int kind = it->bytes[0] & 0xf0;
            const auto pitch = static_cast<std::size_t> (it->bytes[1] & 0x7f);

            if (kind == 0x90 && it->bytes[2] > 0)
            {
                if (! track.audible)
                    continue;
                if (a.on.test (pitch)) // retrigger: end the old note first
                    emit (MidiEvent::make (offset, static_cast<std::uint8_t> (0x80 | track.channel), it->bytes[1], 0, track.port));
                a.on.set (pitch);
            }
            else if (kind == 0x80 || kind == 0x90)
            {
                if (! a.on.test (pitch))
                    continue;
                a.on.reset (pitch);
            }
            emit (MidiEvent::make (offset, it->bytes[0], it->bytes[1], it->bytes[2], track.port));
        }
    }
    sendSetupOnPlay = false;
}

void Engine::recordInput (const ProcessContext& ctx, const MidiEventBuffer& midiIn) noexcept
{
    if (! recording || ! ctx.playing)
        return;
    for (const auto& e : midiIn)
    {
        if (e.sampleOffset < ctx.blockOffset || e.sampleOffset >= ctx.blockOffset + ctx.numSamples)
            continue;
        RecordedEvent r;
        r.tick = ctx.tickAtSample (ctx.sampleTime + (e.sampleOffset - ctx.blockOffset));
        r.bytes = e.bytes;
        r.size = e.size;
        recorded.push (r);
    }
}

void Engine::process (AudioBlock& audio, const MidiEventBuffer& midiIn, MidiEventBuffer& midiOut) noexcept
{
    events.clear();

    if (auto* next = snapshots.acquire(); next != nullptr && next != snapshot)
        switchSnapshot (next, 0);
    if (auto* inst = instruments.acquire(); inst != currentInstrument)
    {
        if (currentInstrument != nullptr)
            currentInstrument->reset();
        currentInstrument = inst;
    }

    currentSamplers = samplerSets.acquire();

    while (auto c = commands.pop())
        apply (*c);

    audio.clear();

    // Live MIDI thru to the selected track's output.
    if (thruChannel > 0)
        for (const auto& e : midiIn)
        {
            if (e.size == 0 || e.bytes[0] >= 0xf0)
                continue;
            auto out = e;
            out.bytes[0] = static_cast<std::uint8_t> ((e.bytes[0] & 0xf0) | ((thruChannel - 1) & 0x0f));
            out.port = thruPort;
            emit (out);
        }

    // Render in spans, split where the loop wraps.
    const auto& loop = snapshot->loop;
    int offset = 0;
    for (int guard = 0; offset < audio.numSamples && guard < 64; ++guard)
    {
        int span = audio.numSamples - offset;
        bool wrap = false;

        if (transport.isPlaying() && loop.enabled && loop.isValid() && transport.positionTicks() < static_cast<double> (loop.end))
        {
            const auto probe = transport.contextFor (span, offset);
            const auto loopEndSample = probe.sampleForTick (static_cast<double> (loop.end)) - probe.sampleTime;
            if (loopEndSample < span)
            {
                span = static_cast<int> (std::max<std::int64_t> (0, loopEndSample));
                wrap = true;
            }
        }

        if (span > 0)
        {
            const auto ctx = transport.contextFor (span, offset);
            sequence (ctx);
            recordInput (ctx, midiIn);

            const auto numChannels = std::min<int> (audio.numChannels, static_cast<int> (spanChannels.size()));
            for (int c = 0; c < numChannels; ++c)
                spanChannels[static_cast<std::size_t> (c)] = audio.channels[c] + offset;
            AudioBlock spanAudio { spanChannels.data(), numChannels, span };

            scratch.clear();
            metronome.process (ctx, spanAudio, midiIn, scratch);
            for (auto e : scratch)
            {
                e.sampleOffset += offset;
                e.port = clickPort;
                emit (e);
            }

            transport.advance (span);
            offset += span;
        }

        if (wrap)
        {
            releaseAllNotes (std::min (offset, audio.numSamples - 1));
            transport.locate (static_cast<double> (loop.start));
            metronome.reset();
        }
    }

    // Built-in instrument plays port 0; hardware ports go back to the platform.
    events.sortByTime();
    instrumentIn.clear();
    for (const auto& e : events)
    {
        if (e.port == MidiEvent::kSoundFontPort)
            instrumentIn.add (e);
        else if (e.port != MidiEvent::kNoPort && ! MidiEvent::isSamplerPort (e.port))
            midiOut.add (e);
    }

    // Each sampler track plays its own sampler.
    if (currentSamplers != nullptr)
    {
        const auto ctx = transport.contextFor (audio.numSamples, 0);
        for (const auto& entry : currentSamplers->entries)
        {
            const auto port = snapshot->samplerPort (entry.track);
            samplerIn.clear();
            if (port != MidiEvent::kNoPort)
                for (const auto& e : events)
                    if (e.port == port)
                        samplerIn.add (e);
            scratch.clear();
            entry.sampler->process (ctx, audio, samplerIn, scratch);
        }
    }

    if (currentInstrument != nullptr)
    {
        const auto ctx = transport.contextFor (audio.numSamples, 0);
        scratch.clear();
        currentInstrument->process (ctx, audio, instrumentIn, scratch);
    }

    // Master gain and meters.
    const float gain = snapshot->masterGain;
    for (int c = 0; c < audio.numChannels; ++c)
    {
        float peak = 0.0f;
        auto* data = audio.channels[c];
        for (int i = 0; i < audio.numSamples; ++i)
        {
            data[i] *= gain;
            peak = std::max (peak, std::abs (data[i]));
        }
        auto& meter = masterPeak[static_cast<std::size_t> (std::min (c, 1))];
        if (peak > meter.load (std::memory_order_relaxed))
            meter.store (peak, std::memory_order_relaxed);
    }

    statusPlaying.store (transport.isPlaying(), std::memory_order_relaxed);
    statusRecording.store (recording, std::memory_order_relaxed);
    statusTicks.store (transport.positionTicks(), std::memory_order_relaxed);
}

} // namespace arranger

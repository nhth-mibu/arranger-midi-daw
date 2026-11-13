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

void Engine::launchSlot (TrackId track, int scene, Tick quantise)
{
    auto c = make (Command::Type::launchSlot);
    c.track = track;
    c.channel = scene;
    c.number = static_cast<double> (quantise);
    send (c);
}

void Engine::stopSlot (TrackId track, Tick quantise)
{
    auto c = make (Command::Type::stopSlot);
    c.track = track;
    c.number = static_cast<double> (quantise);
    send (c);
}

void Engine::launchScene (int scene, Tick quantise)
{
    auto c = make (Command::Type::launchScene);
    c.channel = scene;
    c.number = static_cast<double> (quantise);
    send (c);
}

void Engine::stopAllSlots (Tick quantise)
{
    auto c = make (Command::Type::stopAllSlots);
    c.number = static_cast<double> (quantise);
    send (c);
}

SlotStatus Engine::slotStatus (TrackId track) const noexcept
{
    for (const auto& s : slotStates)
    {
        const auto id = s.id.load (std::memory_order_relaxed);
        if (id == 0)
            break;
        if (id == track)
            return { s.shownPlaying.load (std::memory_order_relaxed), s.shownPending.load (std::memory_order_relaxed), s.shownLaunch.load (std::memory_order_relaxed) };
    }
    return {};
}

void Engine::setSnapshot (std::shared_ptr<const PlaybackSnapshot> s)
{
    snapshots.publish (std::move (s));
}

void Engine::setSamplers (std::shared_ptr<const SamplerSet> s)
{
    samplerSets.publish (std::move (s));
}

void Engine::setRacks (std::shared_ptr<const RackSet> r)
{
    rackSets.publish (std::move (r));
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
    rackSets.collectGarbage();
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
    if (const auto set = rackSets.current())
    {
        // Plugins keep the block size they were made with if it is larger: their buses have that size.
        const auto prepareAll = [&] (const std::vector<std::shared_ptr<Processor>>& list, int block) {
            for (const auto& p : list)
                p->prepare ({ rate, block });
        };
        for (const auto& bus : set->buses)
        {
            const int block = std::max (maxBlockSize, static_cast<int> (bus.left.size()));
            if (bus.instrument != nullptr)
                bus.instrument->prepare ({ rate, block });
            prepareAll (bus.effects, block);
        }
        prepareAll (set->master, std::max (maxBlockSize, 4096));
    }
    statusSampleRate.store (rate);
}

void Engine::released() noexcept
{
    snapshots.release();
    instruments.release();
    samplerSets.release();
    rackSets.release();
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
        case Command::Type::launchSlot: queueSlot (c.track, c.channel, c.number); break;
        case Command::Type::stopSlot:
            if (auto* st = slotState (c.track, false); st != nullptr && (st->playing >= 0 || st->pending >= 0))
                queueSlot (c.track, -1, c.number);
            break;
        case Command::Type::launchScene:
            for (const auto& t : snapshot->tracks)
            {
                const bool has = c.channel >= 0 && static_cast<std::size_t> (c.channel) < t.slots.size() && t.slots[static_cast<std::size_t> (c.channel)].length > 0;
                if (has)
                    queueSlot (t.id, c.channel, c.number);
                else if (auto* st = slotState (t.id, false); st != nullptr && (st->playing >= 0 || st->pending >= 0))
                    queueSlot (t.id, -1, c.number);
            }
            break;
        case Command::Type::stopAllSlots:
            for (auto& st : slotStates)
            {
                const auto id = st.id.load (std::memory_order_relaxed);
                if (id == 0)
                    break;
                if (st.playing >= 0 || st.pending >= 0)
                    queueSlot (id, -1, c.number);
            }
            break;
    }
}

Engine::SlotState* Engine::slotState (TrackId track, bool create) noexcept
{
    for (auto& s : slotStates)
    {
        const auto id = s.id.load (std::memory_order_relaxed);
        if (id == track)
            return &s;
        if (id == 0)
        {
            if (! create)
                return nullptr;
            s.id.store (track, std::memory_order_relaxed);
            return &s;
        }
    }
    return nullptr;
}

void Engine::queueSlot (TrackId track, int scene, double quantise) noexcept
{
    auto* st = slotState (track, true);
    if (st == nullptr)
        return;
    const double now = transport.positionTicks();
    double at = now;
    if (transport.isPlaying() && quantise > 0.0)
        at = std::ceil ((now - 1.0e-6) / quantise) * quantise;
    st->pending = scene;
    st->pendingTick = at;
    st->shownPending.store (scene, std::memory_order_relaxed);
    if (scene >= 0)
        startPlayback();
}

int Engine::offsetFor (const ProcessContext& ctx, double tick) noexcept
{
    const auto s = ctx.sampleForTick (tick) - ctx.sampleTime;
    return ctx.blockOffset + static_cast<int> (std::clamp<std::int64_t> (s, 0, ctx.numSamples - 1));
}

void Engine::emitSequenced (const PlaybackTrack& track, ActiveNotes& a, const ProcessContext& ctx, const SequenceEvent& e, double atTick) noexcept
{
    const int offset = offsetFor (ctx, atTick);
    const int kind = e.bytes[0] & 0xf0;
    const auto pitch = static_cast<std::size_t> (e.bytes[1] & 0x7f);

    if (kind == 0x90 && e.bytes[2] > 0)
    {
        if (! track.audible)
            return;
        if (a.on.test (pitch)) // retrigger: end the old note first
            emit (MidiEvent::make (offset, static_cast<std::uint8_t> (0x80 | track.channel), e.bytes[1], 0, track.port));
        a.on.set (pitch);
    }
    else if (kind == 0x80 || kind == 0x90)
    {
        if (! a.on.test (pitch))
            return;
        a.on.reset (pitch);
    }
    emit (MidiEvent::make (offset, e.bytes[0], e.bytes[1], e.bytes[2], track.port));
}

void Engine::playRange (const PlaybackTrack& track, ActiveNotes& a, const SlotState* st, const ProcessContext& ctx, double from, double to) noexcept
{
    if (to <= from)
        return;
    const auto before = [] (const SequenceEvent& e, double t) { return static_cast<double> (e.at) < t; };

    if (st == nullptr || st->playing < 0)
    {
        // The Timeline.
        const auto& ev = track.events;
        for (auto it = std::lower_bound (ev.begin(), ev.end(), from, before); it != ev.end() && static_cast<double> (it->at) < to; ++it)
            emitSequenced (track, a, ctx, *it, static_cast<double> (it->at));
        return;
    }

    // A Clip grid slot, looping from where it was launched.
    if (static_cast<std::size_t> (st->playing) >= track.slots.size())
        return;
    const auto& slot = track.slots[static_cast<std::size_t> (st->playing)];
    if (slot.length <= 0)
        return;
    const auto length = static_cast<double> (slot.length);
    double base = st->launchTick + std::floor ((from - st->launchTick) / length) * length;
    for (int guard = 0; guard < 64 && base < to; ++guard, base += length)
    {
        if (base >= from && base != st->launchTick)
            releaseNotes (a, offsetFor (ctx, base)); // the slot wraps: end what still sounds
        const double lo = std::max (from, base) - base, hi = std::min (to, base + length) - base;
        const auto& ev = slot.events;
        for (auto it = std::lower_bound (ev.begin(), ev.end(), lo, before); it != ev.end() && static_cast<double> (it->at) < hi; ++it)
            emitSequenced (track, a, ctx, *it, base + static_cast<double> (it->at));
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

        auto* st = slotState (track.id, false);
        double from = ctx.tickAtStart;
        if (st != nullptr && st->pending != SlotStatus::kNone && st->pendingTick < ctx.tickAtEnd)
        {
            // A launch or stop falls in this span: play up to it, then switch.
            const double at = std::max (st->pendingTick, from);
            playRange (track, a, st, ctx, from, at);
            releaseNotes (a, offsetFor (ctx, at));
            st->playing = st->pending;
            st->launchTick = at;
            st->pending = SlotStatus::kNone;
            st->shownLaunch.store (at, std::memory_order_relaxed);
            st->shownPlaying.store (st->playing, std::memory_order_relaxed);
            st->shownPending.store (SlotStatus::kNone, std::memory_order_relaxed);
            slotChanges.push ({ track.id, st->playing, at });
            from = at;
        }
        playRange (track, a, st, ctx, from, ctx.tickAtEnd);
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

/** Adds the audio clips that sound in this span. A clip plays its file at the file's own speed. */
void Engine::playAudio (const ProcessContext& ctx, AudioBlock& out) noexcept
{
    if (out.numChannels <= 0 || ctx.sampleRate <= 0.0)
        return;
    const double spanStart = snapshot->tempo.secondsAt (ctx.tickAtStart);
    const double step = 1.0 / ctx.sampleRate;
    const double spanEnd = spanStart + step * static_cast<double> (ctx.numSamples);
    constexpr double kDeclick = 0.003; // seconds: every clip edge gets at least this fade

    for (const auto& r : snapshot->audio)
    {
        if (r.startSeconds >= spanEnd)
            break; // sorted by start
        if (r.endSeconds <= spanStart)
            continue;
        const auto& sample = *r.sample;
        const auto frames = static_cast<double> (sample.frames());
        const float* left = sample.channels[0].data();
        const float* right = sample.channels.size() > 1 ? sample.channels[1].data() : left;
        const double fadeIn = std::max (kDeclick, r.fadeIn), fadeOut = std::max (kDeclick, r.fadeOut);
        // A track with effects plays into its bus; the others into the mix.
        const auto* bus = currentRacks != nullptr ? currentRacks->busFor (r.track) : nullptr;
        float* const outLeft = bus != nullptr ? bus->left.data() + ctx.blockOffset : out.channels[0];
        float* const outRight = bus != nullptr ? bus->right.data() + ctx.blockOffset : (out.numChannels > 1 ? out.channels[1] : nullptr);

        const int first = std::max (0, static_cast<int> (std::ceil ((r.startSeconds - spanStart) * ctx.sampleRate)));
        for (int i = first; i < ctx.numSamples; ++i)
        {
            const double t = spanStart + step * static_cast<double> (i);
            if (t >= r.endSeconds)
                break;
            const double position = (t - r.startSeconds + r.offset) * sample.sampleRate;
            if (position >= frames - 1.0)
                break;
            const auto index = static_cast<std::size_t> (position);
            const auto frac = static_cast<float> (position - static_cast<double> (index));
            float gain = 1.0f;
            if (t - r.startSeconds < fadeIn)
                gain = static_cast<float> ((t - r.startSeconds) / fadeIn);
            if (r.endSeconds - t < fadeOut)
                gain = std::min (gain, static_cast<float> ((r.endSeconds - t) / fadeOut));
            const float l = (left[index] + (left[index + 1] - left[index]) * frac) * gain * r.gainLeft;
            const float rr = (right[index] + (right[index + 1] - right[index]) * frac) * gain * r.gainRight;
            if (outRight != nullptr)
            {
                outLeft[i] += l;
                outRight[i] += rr;
            }
            else
                outLeft[i] += 0.5f * (l + rr);
        }
    }
}

void Engine::process (AudioBlock& audio, const MidiEventBuffer& midiIn, MidiEventBuffer& midiOut, const AudioBlock& input) noexcept
{
    // Keep the input before the output is made: the device may use the same buffers for both.
    const bool wanted = captureOn.load (std::memory_order_relaxed);
    if (! wanted)
        capturing = false;
    else if (transport.isPlaying() && input.numChannels > 0 && input.channels[0] != nullptr)
    {
        if (! capturing)
        {
            capturing = true;
            captureStart.store (transport.positionTicks(), std::memory_order_relaxed);
        }
        capture.push (input.channels[0], input.numChannels > 1 ? input.channels[1] : nullptr, input.numSamples);
    }
    process (audio, midiIn, midiOut);
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
    currentRacks = rackSets.acquire();
    if (currentRacks != nullptr)
        for (const auto& bus : currentRacks->buses)
        {
            // A block that is longer than the buses were made for plays without them.
            if (bus.left.size() < static_cast<std::size_t> (audio.numSamples))
            {
                currentRacks = nullptr;
                break;
            }
            std::fill_n (bus.left.begin(), audio.numSamples, 0.0f);
            std::fill_n (bus.right.begin(), audio.numSamples, 0.0f);
        }

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

            if (ctx.playing && ! snapshot->audio.empty())
                playAudio (ctx, spanAudio);

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
            // Slots keep their phase across the jump back.
            const auto jump = static_cast<double> (loop.end - loop.start);
            for (auto& st : slotStates)
            {
                if (st.id.load (std::memory_order_relaxed) == 0)
                    break;
                st.launchTick -= jump;
                st.shownLaunch.store (st.launchTick, std::memory_order_relaxed);
                st.pendingTick -= jump;
            }
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

    const auto blockContext = transport.contextFor (audio.numSamples, 0);
    std::array<float*, 2> busChannels {};
    const auto blockOf = [&] (const RackSet::Bus* bus) {
        if (bus == nullptr)
            return audio;
        busChannels = { bus->left.data(), bus->right.data() };
        return AudioBlock { busChannels.data(), 2, audio.numSamples };
    };
    const auto busOf = [&] (TrackId id) { return currentRacks != nullptr ? currentRacks->busFor (id) : nullptr; };

    // Each sampler track plays its own sampler.
    if (currentSamplers != nullptr)
        for (const auto& entry : currentSamplers->entries)
        {
            const auto port = snapshot->samplerPort (entry.track);
            samplerIn.clear();
            if (port != MidiEvent::kNoPort)
                for (const auto& e : events)
                    if (e.port == port)
                        samplerIn.add (e);
            scratch.clear();
            auto block = blockOf (busOf (entry.track));
            entry.sampler->process (blockContext, block, samplerIn, scratch);
        }

    if (currentInstrument != nullptr)
    {
        scratch.clear();
        auto block = blockOf (busOf (0));
        currentInstrument->process (blockContext, block, instrumentIn, scratch);
    }

    if (currentRacks != nullptr)
    {
        for (const auto& bus : currentRacks->buses)
        {
            auto block = blockOf (&bus);
            // A plugin track: its instrument plays the track's MIDI, then the track's volume and pan.
            if (bus.instrument != nullptr)
            {
                const auto port = snapshot->samplerPort (bus.track);
                samplerIn.clear();
                if (port != MidiEvent::kNoPort)
                    for (const auto& e : events)
                        if (e.port == port)
                            samplerIn.add (e);
                scratch.clear();
                bus.instrument->process (blockContext, block, samplerIn, scratch);
            }
            for (const auto& effect : bus.effects)
            {
                scratch.clear();
                effect->process (blockContext, block, noMidi, scratch);
            }
            float left = 1.0f, right = 1.0f;
            if (bus.instrument != nullptr)
                for (const auto& g : snapshot->busGains)
                    if (g.track == bus.track)
                    {
                        left = g.left;
                        right = g.right;
                    }
            // The bus joins the mix.
            for (int i = 0; i < audio.numSamples; ++i)
            {
                const float l = bus.left[static_cast<std::size_t> (i)] * left, r = bus.right[static_cast<std::size_t> (i)] * right;
                if (audio.numChannels > 1)
                {
                    audio.channels[0][i] += l;
                    audio.channels[1][i] += r;
                }
                else if (audio.numChannels == 1)
                    audio.channels[0][i] += 0.5f * (l + r);
            }
        }
        for (const auto& effect : currentRacks->master)
        {
            scratch.clear();
            effect->process (blockContext, audio, noMidi, scratch);
        }
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

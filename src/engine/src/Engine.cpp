#include "arranger/engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <tuple>

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
void Engine::panic()      { send (make (Command::Type::panic)); }
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
    monitorLeft.assign (static_cast<std::size_t> (std::max (1, maxBlockSize)), 0.0f);
    monitorRight.assign (monitorLeft.size(), 0.0f);
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
        int busBlock = maxBlockSize;
        for (const auto& bus : set->buses)
        {
            const int block = std::max (maxBlockSize, static_cast<int> (bus.left.size()));
            busBlock = std::max (busBlock, block);
            if (bus.instrument != nullptr)
                bus.instrument->prepare ({ rate, block });
            prepareAll (bus.effects, block);
        }
        for (const auto& bus : set->mixBuses)
            prepareAll (bus.effects, busBlock);
        prepareAll (set->master, std::max (maxBlockSize, 4096));
        set->prepare (busBlock); // latencies can change with the sample rate
    }
    statusSampleRate.store (rate);
}

void Engine::released() noexcept
{
    // These raw pointers are used again when the device restarts. Detach them
    // before releasing the objects that own their storage.
    transport.setTempoMap (&emptySnapshot.tempo);
    snapshot = &emptySnapshot;
    currentInstrument = nullptr;
    currentSamplers = nullptr;
    currentRacks = nullptr;
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
        case Command::Type::panic:
            stopPlayback (0);
            releaseAllNotes (0);
            sendToTrackChannels (0, 64, 0);  // sustain off
            sendToTrackChannels (0, 120, 0); // all sound off
            sendToTrackChannels (0, 123, 0); // all notes off
            // Live keys still held through MIDI thru.
            for (std::size_t in = 0; in < thruHeld.size(); ++in)
                for (std::size_t key = 0; key < 128; ++key)
                    if (const auto dest = thruHeld[in][key]; dest != 0)
                    {
                        emit (MidiEvent::make (0, static_cast<std::uint8_t> (0x80 | ((static_cast<int> (dest & 0xff) - 1) & 0x0f)), static_cast<std::uint8_t> (key), 0, static_cast<std::uint16_t> (dest >> 8)));
                        thruHeld[in][key] = 0;
                    }
            for (auto& dest : thruPedal)
                if (dest != 0)
                {
                    emit (MidiEvent::make (0, static_cast<std::uint8_t> (0xb0 | ((static_cast<int> (dest & 0xff) - 1) & 0x0f)), 64, 0, static_cast<std::uint16_t> (dest >> 8)));
                    dest = 0;
                }
            break;
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
                else
                    queueSlot (t.id, -1, c.number); // no clip in this scene: stop the track, even the Timeline
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
    else if (kind == 0xb0 && e.bytes[1] == 64)
        a.pedal = e.bytes[2] >= 64;
    emit (MidiEvent::make (offset, e.bytes[0], e.bytes[1], e.bytes[2], track.port));
}

bool Engine::clipGridActive() const noexcept
{
    for (const auto& s : slotStates)
    {
        const auto id = s.id.load (std::memory_order_relaxed);
        if (id == 0)
            break;
        if (s.playing >= 0 || s.pending != SlotStatus::kNone)
            return true;
    }
    return false;
}

void Engine::playRange (const PlaybackTrack& track, ActiveNotes& a, const SlotState* st, const ProcessContext& ctx, double from, double to) noexcept
{
    if (to <= from)
        return;
    const auto before = [] (const SequenceEvent& e, double t) { return static_cast<double> (e.at) < t; };

    if (st == nullptr || st->playing < 0)
    {
        // The Timeline. While the Clip grid is active it stays silent on every track.
        if (clipMode)
            return;
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
    // A safety net for notes the engine did not start (or a synth that missed a note-off).
    sendToTrackChannels (offset, 123, 0);
    metronome.reset();
}

void Engine::releaseNotes (ActiveNotes& a, int offset) noexcept
{
    for (int pitch = 0; a.on.any() && pitch < 128; ++pitch)
        if (a.on.test (static_cast<std::size_t> (pitch)))
            emit (MidiEvent::make (offset, static_cast<std::uint8_t> (0x80 | a.channel), static_cast<std::uint8_t> (pitch), 0, a.port));
    a.on.reset();
    // A held sustain pedal would keep the ended notes sounding.
    if (a.pedal)
        emit (MidiEvent::make (offset, static_cast<std::uint8_t> (0xb0 | a.channel), 64, 0, a.port));
    a.pedal = false;
}

void Engine::sendToTrackChannels (int offset, std::uint8_t controller, std::uint8_t value) noexcept
{
    const auto& tracks = snapshot->tracks;
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& t = tracks[i];
        if (t.port == MidiEvent::kNoPort)
            continue;
        bool seen = false;
        for (std::size_t k = 0; k < i && ! seen; ++k)
            seen = tracks[k].port == t.port && tracks[k].channel == t.channel;
        if (! seen)
            emit (MidiEvent::make (offset, static_cast<std::uint8_t> (0xb0 | t.channel), controller, value, t.port));
    }
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
    std::array<bool, 64> carriedPedal {};
    std::array<TrackId, 64> carriedIds {};
    std::size_t carriedCount = 0;

    const auto oldCount = std::min<std::size_t> (snapshot->tracks.size(), active.size());
    for (std::size_t i = 0; i < oldCount; ++i)
    {
        auto& a = active[i];
        if (a.on.none() && ! a.pedal)
            continue;
        bool keep = false;
        for (const auto& t : next->tracks)
            if (t.id == a.id && t.contentKey == a.contentKey)
                keep = true;
        if (keep && carriedCount < carried.size())
        {
            carriedPedal[carriedCount] = a.pedal;
            carried[carriedCount] = a.on;
            carriedIds[carriedCount++] = a.id;
        }
        else
        {
            // Events go to samplers and plugins by the next snapshot's numbers: a track's number can move.
            if (MidiEvent::isSamplerPort (a.port))
                a.port = next->samplerPort (a.id);
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
        a.pedal = false;
        for (std::size_t k = 0; k < carriedCount; ++k)
            if (carriedIds[k] == t.id)
            {
                a.on = carried[k];
                a.pedal = carriedPedal[k];
            }
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
            clipMode = clipGridActive();
            from = at;
        }
        playRange (track, a, st, ctx, from, ctx.tickAtEnd);
    }
    sendSetupOnPlay = false;
}

void Engine::playThru (const MidiEventBuffer& midiIn) noexcept
{
    // Live MIDI thru to the selected track's output. A key's note-off (and the pedal's release)
    // goes where its note-on went, even when another track was chosen in between.
    const auto pack = [] (std::uint16_t port, int channel) { return (static_cast<std::uint32_t> (port) << 8) | static_cast<std::uint32_t> (channel + 1); };
    const auto sendTo = [this] (const MidiEvent& e, std::uint32_t dest) {
        auto out = e;
        out.bytes[0] = static_cast<std::uint8_t> ((e.bytes[0] & 0xf0) | ((static_cast<int> (dest & 0xff) - 1) & 0x0f));
        out.port = static_cast<std::uint16_t> (dest >> 8);
        emit (out);
    };
    const std::uint32_t current = thruChannel > 0 ? pack (thruPort, thruChannel - 1) : 0;
    for (const auto& e : midiIn)
    {
        if (e.size == 0 || e.bytes[0] < 0x80 || e.bytes[0] >= 0xf0)
            continue;
        const auto in = static_cast<std::size_t> (e.bytes[0] & 0x0f);
        const auto key = static_cast<std::size_t> (e.bytes[1] & 0x7f);
        const int kind = e.bytes[0] & 0xf0;
        if (e.isNoteOn())
        {
            auto& held = thruHeld[in][key];
            if (held != 0 && held != current) // the same key still sounds somewhere else: end it there
                sendTo (MidiEvent::make (e.sampleOffset, static_cast<std::uint8_t> (0x80), e.bytes[1], 0, 0), held);
            held = current;
            if (current != 0)
                sendTo (e, current);
        }
        else if (e.isNoteOff())
        {
            auto& held = thruHeld[in][key];
            if (held != 0)
                sendTo (e, held);
            else if (current != 0)
                sendTo (e, current);
            held = 0;
        }
        else if (kind == 0xb0 && e.bytes[1] == 64)
        {
            auto& pedal = thruPedal[in];
            if (e.bytes[2] >= 64)
                pedal = current;
            else
            {
                if (pedal != 0 && pedal != current)
                    sendTo (e, pedal);
                pedal = 0;
            }
            if (current != 0)
                sendTo (e, current);
        }
        else if (current != 0)
            sendTo (e, current);
    }
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
        // While the Clip grid plays, no Timeline audio plays.
        if (clipMode)
            continue;
        const auto& sample = *r.sample;
        const auto frames = static_cast<double> (sample.frames());
        const float* left = sample.channels[0].data();
        const float* right = sample.channels.size() > 1 ? sample.channels[1].data() : left;
        const double fadeIn = std::max (kDeclick, r.fadeIn), fadeOut = std::max (kDeclick, r.fadeOut);
        // A track with effects plays into its bus; the others into the mix.
        const auto* bus = currentRacks != nullptr ? currentRacks->busFor (r.track) : nullptr;
        // Automation: the level at the start and the end of this span, and a straight line between.
        const auto* lane = snapshot->laneFor (r.track);
        const auto laneStart = lane != nullptr ? lane->at (ctx.tickAtStart) : std::pair<float, float> { 1.0f, 1.0f };
        const auto laneEnd = lane != nullptr ? lane->at (ctx.tickAtEnd) : laneStart;
        const float laneStep = 1.0f / static_cast<float> (std::max (1, ctx.numSamples));
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
            const float along = static_cast<float> (i) * laneStep;
            const float l = (left[index] + (left[index + 1] - left[index]) * frac) * gain * r.gainLeft * (laneStart.first + (laneEnd.first - laneStart.first) * along);
            const float rr = (right[index] + (right[index + 1] - right[index]) * frac) * gain * r.gainRight * (laneStart.second + (laneEnd.second - laneStart.second) * along);
            if (outRight != nullptr)
            {
                outLeft[i] += l;
                outRight[i] += rr;
            }
            else
                outLeft[i] += 0.5f * (l + rr);
        }
    }

    // Clip grid slots: the launched slot of a track loops from where it started, in time with the song.
    const double spanTicks = ctx.tickAtEnd - ctx.tickAtStart;
    if (spanTicks <= 0.0)
        return;
    const double secondsPerTick = (spanEnd - spanStart) / spanTicks;
    const double tickStep = spanTicks / static_cast<double> (ctx.numSamples);
    for (const auto& r : snapshot->slotAudio)
    {
        const auto* st = slotState (r.track, false);
        if (st == nullptr || st->playing != r.scene || r.end <= 0)
            continue;
        const auto& sample = *r.sample;
        const auto frames = static_cast<double> (sample.frames());
        const float* left = sample.channels[0].data();
        const float* right = sample.channels.size() > 1 ? sample.channels[1].data() : left;
        const auto* bus = currentRacks != nullptr ? currentRacks->busFor (r.track) : nullptr;
        const auto* lane = snapshot->laneFor (r.track);
        const auto laneStart = lane != nullptr ? lane->at (ctx.tickAtStart) : std::pair<float, float> { 1.0f, 1.0f };
        const auto laneEnd = lane != nullptr ? lane->at (ctx.tickAtEnd) : laneStart;
        const float laneStep = 1.0f / static_cast<float> (std::max (1, ctx.numSamples));
        float* const outLeft = bus != nullptr ? bus->left.data() + ctx.blockOffset : out.channels[0];
        float* const outRight = bus != nullptr ? bus->right.data() + ctx.blockOffset : (out.numChannels > 1 ? out.channels[1] : nullptr);
        const auto length = static_cast<double> (r.end);
        const double loopSeconds = length * secondsPerTick;

        for (int i = 0; i < ctx.numSamples; ++i)
        {
            const double since = ctx.tickAtStart + tickStep * static_cast<double> (i) - st->launchTick;
            if (since < 0.0)
                continue; // the slot starts later in this span
            const double phase = (since - std::floor (since / length) * length) * secondsPerTick; // seconds into the loop
            const double position = (phase + r.offset) * sample.sampleRate;
            if (position >= frames - 1.0)
                continue; // the file is shorter than the loop
            const auto index = static_cast<std::size_t> (position);
            const auto frac = static_cast<float> (position - static_cast<double> (index));
            // A short fade at each end of the loop, so that the jump back does not click.
            const float gain = static_cast<float> (std::min ({ 1.0, phase / kDeclick, (loopSeconds - phase) / kDeclick }));
            const float along = static_cast<float> (i) * laneStep;
            const float l = (left[index] + (left[index + 1] - left[index]) * frac) * gain * r.gainLeft * (laneStart.first + (laneEnd.first - laneStart.first) * along);
            const float rr = (right[index] + (right[index + 1] - right[index]) * frac) * gain * r.gainRight * (laneStart.second + (laneEnd.second - laneStart.second) * along);
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
    // The track's input: one channel or a pair among the open inputs. Past the end: the first ones.
    int first = inputFirst.load (std::memory_order_relaxed);
    const int count = inputCount.load (std::memory_order_relaxed);
    if (first + count > input.numChannels)
        first = 0;
    const float* inLeft = first < input.numChannels ? input.channels[first] : nullptr;
    const float* inRightOrNull = count == 2 && first + 1 < input.numChannels ? input.channels[first + 1] : nullptr;

    const bool wanted = captureOn.load (std::memory_order_relaxed);
    if (! wanted)
        capturing = false;
    else if (transport.isPlaying() && inLeft != nullptr)
    {
        if (! capturing)
        {
            capturing = true;
            captureStart.store (transport.positionTicks(), std::memory_order_relaxed);
        }
        capture.push (inLeft, inRightOrNull, input.numSamples);
    }

    // Input monitoring. The level moves over the block when the switch changes.
    const float target = monitorOn.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    const bool monitor = (target > 0.0f || monitorLevel > 0.0f) && inLeft != nullptr
                         && input.numSamples > 0 && static_cast<std::size_t> (input.numSamples) <= monitorLeft.size();
    if (monitor)
    {
        const float* inRight = inRightOrNull != nullptr ? inRightOrNull : inLeft;
        std::copy_n (inLeft, input.numSamples, monitorLeft.data());
        std::copy_n (inRight, input.numSamples, monitorRight.data());
    }
    else
        monitorLevel = target > 0.0f ? monitorLevel : 0.0f;

    process (audio, midiIn, midiOut);

    if (monitor && audio.numChannels > 0)
    {
        const int n = std::min (audio.numSamples, input.numSamples);
        const float step = (target - monitorLevel) / static_cast<float> (std::max (1, n));
        for (int i = 0; i < n; ++i)
        {
            const float g = monitorLevel + step * static_cast<float> (i + 1);
            if (audio.numChannels > 1)
            {
                audio.channels[0][i] += monitorLeft[static_cast<std::size_t> (i)] * g;
                audio.channels[1][i] += monitorRight[static_cast<std::size_t> (i)] * g;
            }
            else
                audio.channels[0][i] += 0.5f * (monitorLeft[static_cast<std::size_t> (i)] + monitorRight[static_cast<std::size_t> (i)]) * g;
        }
        monitorLevel = target;
    }
}

void Engine::process (AudioBlock& audio, const MidiEventBuffer& midiIn, MidiEventBuffer& midiOut) noexcept
{
    events.clear();

    if (auto* next = snapshots.acquire(); next != nullptr && next != snapshot)
        switchSnapshot (next, 0);
    snapshots.finishTransition();
    if (auto* inst = instruments.acquire(); inst != currentInstrument)
    {
        if (currentInstrument != nullptr)
            currentInstrument->reset();
        currentInstrument = inst;
    }
    instruments.finishTransition();

    currentSamplers = samplerSets.acquire();
    samplerSets.finishTransition();
    currentRacks = rackSets.acquire();
    rackSets.finishTransition();
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
    if (currentRacks != nullptr)
        for (const auto& bus : currentRacks->mixBuses)
        {
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

    // The Clip grid takes over the song while any slot is pending or playing.
    const bool clipsActive = clipGridActive();
    if (clipsActive && ! clipMode)
        releaseAllNotes (0); // end held Timeline notes before they go silent
    clipMode = clipsActive;

    playThru (midiIn);

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
            // Wrap when the end lands exactly on the block boundary too. Otherwise the next
            // block starts at loop.end, where the test below is false, and playback runs on.
            if (loopEndSample <= span)
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

            if (ctx.playing && ! (snapshot->audio.empty() && snapshot->slotAudio.empty()))
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

    const int hardwareDelay = currentRacks != nullptr ? currentRacks->totalLatency() : 0;
    // Built-in instrument plays port 0; hardware ports go back to the platform.
    events.sortByTime();
    instrumentIn.clear();
    for (const auto& e : events)
    {
        if (e.port == MidiEvent::kSoundFontPort)
            instrumentIn.add (e);
        else if (e.port != MidiEvent::kNoPort && ! MidiEvent::isSamplerPort (e.port))
        {
            // Hardware synths wait as long as the plugins make the app's own sound wait.
            auto late = e;
            late.sampleOffset += hardwareDelay;
            midiOut.add (late);
        }
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
        // A SoundFont track with effects gets its MIDI channel on its own bus.
        ChannelOutputs channels;
        bool any = false;
        if (currentRacks != nullptr)
            for (const auto& bus : currentRacks->buses)
                for (const auto& t : snapshot->tracks)
                    if (t.id == bus.track && t.port == MidiEvent::kSoundFontPort && channels.left[t.channel] == nullptr)
                    {
                        channels.left[t.channel] = bus.left.data();
                        channels.right[t.channel] = bus.right.data();
                        any = true;
                    }
        scratch.clear();
        auto block = blockOf (busOf (0));
        currentInstrument->setChannelOutputs (any ? &channels : nullptr);
        currentInstrument->process (blockContext, block, instrumentIn, scratch);
        currentInstrument->setChannelOutputs (nullptr);
    }

    if (currentRacks != nullptr)
    {
        // What is in the mix so far did not go through a bus: it waits for the slowest bus.
        currentRacks->directWait.process (audio.channels[0], audio.numChannels > 1 ? audio.channels[1] : nullptr, audio.numSamples);
        for (const auto& bus : currentRacks->buses)
        {
            auto block = blockOf (&bus);
            // Automation of plugin controls: each line sets its control for this block.
            if (blockContext.playing)
                for (const auto& lane : snapshot->controlLanes)
                {
                    if (lane.track != bus.track)
                        continue;
                    Processor* target = nullptr;
                    if (lane.slot < 0)
                        target = bus.instrument.get();
                    else if (bus.effectSlots.empty())
                        target = static_cast<std::size_t> (lane.slot) < bus.effects.size() ? bus.effects[static_cast<std::size_t> (lane.slot)].get() : nullptr;
                    else
                        for (std::size_t i = 0; i < bus.effects.size() && i < bus.effectSlots.size(); ++i)
                            if (bus.effectSlots[i] == lane.slot)
                                target = bus.effects[i].get();
                    if (target != nullptr)
                        target->setControl (lane.control, automation::valueAt (lane.line, blockContext.tickAtStart, 0.0f) / 127.0f);
                }
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
            bus.wait.process (bus.left.data(), bus.right.data(), audio.numSamples);
            float left = 1.0f, right = 1.0f;
            if (bus.instrument != nullptr)
                for (const auto& g : snapshot->busGains)
                    if (g.track == bus.track)
                    {
                        left = g.left;
                        right = g.right;
                    }
            // A plugin track with automation: the level moves through the block.
            const auto* lane = bus.instrument != nullptr ? snapshot->laneFor (bus.track) : nullptr;
            float leftEnd = left, rightEnd = right;
            if (lane != nullptr)
            {
                std::tie (left, right) = lane->at (blockContext.tickAtStart);
                std::tie (leftEnd, rightEnd) = lane->at (blockContext.tickAtEnd);
            }
            const float leftStep = (leftEnd - left) / static_cast<float> (audio.numSamples), rightStep = (rightEnd - right) / static_cast<float> (audio.numSamples);
            // The bus joins the mix, or a mix bus; its sends go to their mix buses.
            const auto* route = snapshot->routeFor (bus.track);
            const auto* target = route != nullptr && route->bus != 0 ? currentRacks->mixBusFor (route->bus) : nullptr;
            for (int i = 0; i < audio.numSamples; ++i)
            {
                const auto at = static_cast<std::size_t> (i);
                const float l = bus.left[at] * (left + leftStep * static_cast<float> (i)), r = bus.right[at] * (right + rightStep * static_cast<float> (i));
                bus.left[at] = l;
                bus.right[at] = r;
                if (target != nullptr)
                {
                    target->left[at] += l;
                    target->right[at] += r;
                }
                else if (audio.numChannels > 1)
                {
                    audio.channels[0][i] += l;
                    audio.channels[1][i] += r;
                }
                else if (audio.numChannels == 1)
                    audio.channels[0][i] += 0.5f * (l + r);
            }
            if (route != nullptr)
                for (const auto& [id, sendGain] : route->sends)
                    if (const auto* to = currentRacks->mixBusFor (id))
                        for (std::size_t i = 0; i < static_cast<std::size_t> (audio.numSamples); ++i)
                        {
                            to->left[i] += bus.left[i] * sendGain;
                            to->right[i] += bus.right[i] * sendGain;
                        }
        }
        if (! currentRacks->mixBuses.empty())
        {
            // The mix buses: their effects, then their faders, into the mix. The rest waits for the slowest one.
            currentRacks->mixWait.process (audio.channels[0], audio.numChannels > 1 ? audio.channels[1] : nullptr, audio.numSamples);
            for (const auto& bus : currentRacks->mixBuses)
            {
                std::array<float*, 2> mixChannels { bus.left.data(), bus.right.data() };
                AudioBlock block { mixChannels.data(), 2, audio.numSamples };
                for (const auto& effect : bus.effects)
                {
                    scratch.clear();
                    effect->process (blockContext, block, noMidi, scratch);
                }
                bus.wait.process (bus.left.data(), bus.right.data(), audio.numSamples);
                float left = 1.0f, right = 1.0f;
                for (const auto& level : snapshot->busLevels)
                    if (level.bus == bus.id)
                    {
                        left = level.left;
                        right = level.right;
                    }
                float peakL = 0.0f, peakR = 0.0f;
                for (int i = 0; i < audio.numSamples; ++i)
                {
                    const auto at = static_cast<std::size_t> (i);
                    const float l = bus.left[at] * left, r = bus.right[at] * right;
                    peakL = std::max (peakL, std::abs (l));
                    peakR = std::max (peakR, std::abs (r));
                    if (audio.numChannels > 1)
                    {
                        audio.channels[0][i] += l;
                        audio.channels[1][i] += r;
                    }
                    else if (audio.numChannels == 1)
                        audio.channels[0][i] += 0.5f * (l + r);
                }
                if (peakL > bus.meter->left.load (std::memory_order_relaxed))
                    bus.meter->left.store (peakL, std::memory_order_relaxed);
                if (peakR > bus.meter->right.load (std::memory_order_relaxed))
                    bus.meter->right.store (peakR, std::memory_order_relaxed);
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

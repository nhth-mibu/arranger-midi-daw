#pragma once

#include "arranger/core/Song.h"
#include "arranger/engine/AudioBlock.h"
#include "arranger/engine/AudioCapture.h"
#include "arranger/engine/DrumSampler.h"
#include "arranger/engine/Handoff.h"
#include "arranger/engine/Metronome.h"
#include "arranger/engine/Playback.h"
#include "arranger/engine/SpscQueue.h"
#include "arranger/engine/Transport.h"

#include <array>
#include <atomic>
#include <bitset>
#include <memory>
#include <optional>

namespace arranger
{

/** A MIDI message recorded from an input, at the song position it arrived. */
struct RecordedEvent
{
    double tick = 0.0;
    std::array<std::uint8_t, 3> bytes {};
    std::uint8_t size = 0;
};

/** A Clip grid slot started or stopped on a track (scene -1 = stopped), at this song position. */
struct SlotChange
{
    TrackId track = 0;
    int scene = -1;
    double tick = 0.0;
};

/** Holds stereo audio back by a fixed number of samples. */
struct DelayLine
{
    void setLength (int samples)
    {
        left.assign (static_cast<std::size_t> (std::max (0, samples)), 0.0f);
        right.assign (left.size(), 0.0f);
        position = 0;
    }

    /** In place. `r` may be null. */
    void process (float* l, float* r, int count) noexcept
    {
        if (left.empty())
            return;
        for (int i = 0; i < count; ++i)
        {
            std::swap (l[i], left[position]);
            if (r != nullptr)
                std::swap (r[i], right[position]);
            position = position + 1 < left.size() ? position + 1 : 0;
        }
    }

    std::vector<float> left, right;
    std::size_t position = 0;
};

/**
    Plugins and effects, ready to play. A bus is the audio of one track before
    it joins the mix: its instrument (plugin tracks) and its insert effects.
    Track 0 is the bus of the built-in SoundFont player, which all SoundFont
    tracks share. Tracks without a bus go straight to the mix.

    Build it on the UI thread, prepare every processor, call prepare() and
    hand it to Engine::setRacks().
*/
struct RackSet
{
    struct Bus
    {
        TrackId track = 0;
        std::shared_ptr<Processor> instrument;            ///< plugin tracks: makes the sound from the track's MIDI
        std::vector<std::shared_ptr<Processor>> effects;  ///< change the audio in place, in order
        mutable std::vector<float> left, right;           ///< the audio thread's work space
        mutable DelayLine wait;                           ///< delay compensation: holds this bus back to the slowest one
        std::vector<int> effectSlots;                     ///< for each effect, its number in Track::effects (a slot that did not load has no effect here). Empty: the same order.
    };
    std::vector<Bus> buses;

    /** The peak levels of a mix bus since the UI last read them. */
    struct Meter
    {
        std::atomic<float> left { 0.0f }, right { 0.0f };
    };

    /** A bus of the song (Song::buses): tracks go to it or send to it. Its effects run after all tracks. */
    struct MixBus
    {
        std::uint32_t id = 0;
        std::vector<std::shared_ptr<Processor>> effects;
        mutable std::vector<float> left, right;
        mutable DelayLine wait;                     ///< delay compensation: holds this bus back to the slowest mix bus
        std::shared_ptr<Meter> meter = std::make_shared<Meter>();
    };
    std::vector<MixBus> mixBuses;
    std::vector<std::shared_ptr<Processor>> master; ///< effects on the whole mix, before the master level

    /** Delay compensation. Some plugins give their sound late (latency). Every other
        path waits the same time, so all tracks stay together. */
    mutable DelayLine directWait;  ///< for the sound that goes to the mix without a bus
    mutable int busLatency = 0;    ///< the latency of the slowest bus, in samples
    mutable DelayLine mixWait;     ///< for the sound that goes to the master without a mix bus
    mutable int mixLatency = 0;    ///< the latency of the slowest mix bus
    mutable int masterLatency = 0; ///< the latency of the master effects together

    /** How late the sound leaves the engine because of plugins, in samples. */
    int totalLatency() const noexcept { return busLatency + mixLatency + masterLatency; }

    /** Sizes the buses and the delay compensation. Call after the processors are prepared, not while the audio runs. */
    void prepare (int maxBlockSize) const
    {
        const auto latencyOf = [] (const Bus& b) {
            int n = b.instrument != nullptr ? b.instrument->latencySamples() : 0;
            for (const auto& e : b.effects)
                n += e->latencySamples();
            return std::max (0, n);
        };
        busLatency = 0;
        for (const auto& b : buses)
            busLatency = std::max (busLatency, latencyOf (b));
        const auto latencyOfMix = [] (const MixBus& b) {
            int n = 0;
            for (const auto& e : b.effects)
                n += std::max (0, e->latencySamples());
            return n;
        };
        mixLatency = 0;
        for (const auto& b : mixBuses)
            mixLatency = std::max (mixLatency, latencyOfMix (b));
        for (const auto& b : mixBuses)
        {
            b.left.assign (static_cast<std::size_t> (maxBlockSize), 0.0f);
            b.right.assign (static_cast<std::size_t> (maxBlockSize), 0.0f);
            b.wait.setLength (mixLatency - latencyOfMix (b));
        }
        mixWait.setLength (mixLatency);
        masterLatency = 0;
        for (const auto& e : master)
            masterLatency += std::max (0, e->latencySamples());
        for (const auto& b : buses)
        {
            b.left.assign (static_cast<std::size_t> (maxBlockSize), 0.0f);
            b.right.assign (static_cast<std::size_t> (maxBlockSize), 0.0f);
            b.wait.setLength (busLatency - latencyOf (b));
        }
        directWait.setLength (busLatency);
    }

    const MixBus* mixBusFor (std::uint32_t id) const noexcept
    {
        for (const auto& b : mixBuses)
            if (b.id == id)
                return &b;
        return nullptr;
    }

    const Bus* busFor (TrackId id) const noexcept
    {
        for (const auto& b : buses)
            if (b.track == id)
                return &b;
        return nullptr;
    }
};

/** The drum samplers of the sampler tracks. Built on the UI thread and handed over whole. */
struct SamplerSet
{
    struct Entry
    {
        TrackId track = 0;
        std::shared_ptr<DrumSampler> sampler; ///< already prepared
    };
    std::vector<Entry> entries;

    DrumSampler* forTrack (TrackId id) const noexcept
    {
        for (const auto& e : entries)
            if (e.track == id)
                return e.sampler.get();
        return nullptr;
    }
};

/**
    The real-time engine: plays the song snapshot, the metronome and the
    built-in instrument, passes live MIDI through, and records.

    UI thread: the command methods queue work lock-free; it is applied at the
    start of the next audio block. Audio thread: process(). Status getters are
    safe from any thread.
*/
class Engine
{
public:
    static constexpr int kMaxTracks = 512;

    Engine();

    // ---- UI thread -------------------------------------------------------
    void play();
    void stop();
    /** Ends every sound on every port and channel the song uses: sustain off, all sound off, all notes off. */
    void panic();
    void togglePlay();
    void locate (Tick position);
    void setRecording (bool);

    /** Hands over a new song snapshot (tempo, meter, loop, metronome, tracks). */
    void setSnapshot (std::shared_ptr<const PlaybackSnapshot>);
    void loadSong (const Song& song) { setSnapshot (std::make_shared<const PlaybackSnapshot> (buildSnapshot (song))); }

    // Clip grid. A launched slot loops in place of the track's Timeline clips until it is stopped.
    // `quantise` is in ticks: the change waits for the next multiple of it (0 = at once).
    // Launching while stopped starts playback; with a quantise, it first moves back to the launch
    // grid (the bar line at or before the playhead) so every slot keeps in time.
    void launchSlot (TrackId, int scene, Tick quantise);
    void stopSlot (TrackId, Tick quantise);
    /** Launches the scene's slot on every track that has one, and stops the tracks that don't. */
    void launchScene (int scene, Tick quantise);
    void stopAllSlots (Tick quantise);
    SlotStatus slotStatus (TrackId) const noexcept;
    /** Slot launches and stops as they happened, oldest first (for recording a jam). */
    std::optional<SlotChange> popSlotChange() noexcept { return slotChanges.pop(); }

    /** Live MIDI from the inputs goes to this port and channel (1..16). Channel 0 turns thru off. */
    void setThru (std::uint16_t port, int channel);

    /** Sends the metronome's MIDI notes (channel 10 wood blocks) to a port too; kNoPort for audio click only. */
    void setClickPort (std::uint16_t port);

    /** Sends one message now, for example a mixer change or a note preview. */
    void sendMidi (std::uint16_t port, std::uint8_t b0, std::uint8_t b1, std::uint8_t b2 = 0);

    /** The built-in instrument (SoundFont player) that plays port 0. Prepare it before handing it over. */
    void setInstrument (std::shared_ptr<Processor>);
    std::shared_ptr<Processor> instrument() const { return instruments.current(); }

    /** The samplers that play the sampler tracks. Prepare each one before handing the set over. */
    void setSamplers (std::shared_ptr<const SamplerSet>);
    std::shared_ptr<const SamplerSet> samplers() const { return samplerSets.current(); }

    /** Plugin instruments and insert effects. Prepare the processors and the set before handing it over. */
    void setRacks (std::shared_ptr<const RackSet>);
    std::shared_ptr<const RackSet> racks() const { return rackSets.current(); }

    /** The port to use with sendMidi() and setThru() for a sampler track (kNoPort if it has none yet). */
    std::uint16_t samplerPort (TrackId) const;

    /** Frees snapshots and instruments the audio thread has finished with. Call from a UI timer. */
    void collectGarbage();

    /** Audio recording: keeps the audio input while the song plays. Read it with capturedAudio(). */
    void setAudioCapture (bool on) noexcept
    {
        if (on)
            captureStart.store (-1.0, std::memory_order_relaxed);
        captureOn.store (on, std::memory_order_relaxed);
    }
    AudioCapture& capturedAudio() noexcept { return capture; }

    /** Which audio input to record and monitor: the first channel among the open inputs (0-based) and 1 or 2 channels.
        A channel past the open inputs falls back to the first ones. */
    void setAudioInput (int firstChannel, int channels) noexcept
    {
        inputFirst.store (firstChannel < 0 ? 0 : firstChannel, std::memory_order_relaxed);
        inputCount.store (channels == 1 ? 1 : 2, std::memory_order_relaxed);
    }

    /** Input monitoring: adds the audio input to the output, so that you hear what you record. Use headphones: with speakers the microphone hears the output. */
    void setInputMonitor (bool on) noexcept { monitorOn.store (on, std::memory_order_relaxed); }
    bool inputMonitor() const noexcept { return monitorOn.load (std::memory_order_relaxed); }
    /** Where in the song the kept audio starts (less than 0: nothing kept yet). */
    double captureStartTicks() const noexcept { return captureStart.load (std::memory_order_relaxed); }

    /** Recorded MIDI, oldest first. */
    std::optional<RecordedEvent> popRecorded() noexcept { return recorded.pop(); }

    // ---- audio thread ----------------------------------------------------
    /** Call before the first process() and whenever the device changes. Not real-time safe. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Renders one block. Output audio is overwritten. Hardware MIDI (port 1 and up) goes to `midiOut`.
        Never allocates, locks or waits. */
    void process (AudioBlock& audio, const MidiEventBuffer& midiIn, MidiEventBuffer& midiOut) noexcept;

    /** The same, with the audio input of this block. While audio capture is on and the song plays, the input is kept. */
    void process (AudioBlock& audio, const MidiEventBuffer& midiIn, MidiEventBuffer& midiOut, const AudioBlock& input) noexcept;

    /** Call when the audio device stops. */
    void released() noexcept;

    // ---- any thread ------------------------------------------------------
    bool isPlaying() const noexcept { return statusPlaying.load (std::memory_order_relaxed); }
    bool isRecording() const noexcept { return statusRecording.load (std::memory_order_relaxed); }
    double positionTicks() const noexcept { return statusTicks.load (std::memory_order_relaxed); }
    double sampleRate() const noexcept { return statusSampleRate.load (std::memory_order_relaxed); }
    /** Peak level of the master output since the last call, per channel (0 = left). */
    float takeMasterPeak (int channel) noexcept;

private:
    struct Command
    {
        enum class Type { play, stop, panic, togglePlay, locate, record, thru, midi, clickPort, launchSlot, stopSlot, launchScene, stopAllSlots };
        Type type = Type::stop;
        double number = 0.0;
        std::uint16_t port = 0;
        int channel = 0;
        std::array<std::uint8_t, 3> bytes {};
        TrackId track = 0;
    };

    struct SlotState
    {
        std::atomic<TrackId> id { 0 };
        int playing = -1;
        double launchTick = 0.0;
        int pending = SlotStatus::kNone;
        double pendingTick = 0.0;
        std::atomic<int> shownPlaying { -1 }, shownPending { SlotStatus::kNone };
        std::atomic<double> shownLaunch { 0.0 };
    };

    struct ActiveNotes
    {
        TrackId id = 0;
        std::uint16_t port = 0;
        std::uint8_t channel = 0;
        std::uint64_t contentKey = 0;
        std::bitset<128> on;
        bool pedal = false; ///< the sustain pedal is down
    };

    static Command make (Command::Type);
    void send (const Command&);
    void apply (const Command&) noexcept;

    void startPlayback() noexcept;
    void stopPlayback (int offset) noexcept;
    void switchSnapshot (const PlaybackSnapshot*, int offset) noexcept;
    void releaseNotes (ActiveNotes&, int offset) noexcept;
    void releaseAllNotes (int offset) noexcept;
    /** Sends a controller to every port and channel the snapshot's tracks use, once each. */
    void sendToTrackChannels (int offset, std::uint8_t controller, std::uint8_t value) noexcept;
    void playThru (const MidiEventBuffer& midiIn) noexcept;
    void sequence (const ProcessContext&) noexcept;
    void playRange (const PlaybackTrack&, ActiveNotes&, const SlotState*, const ProcessContext&, double from, double to) noexcept;
    void emitSequenced (const PlaybackTrack&, ActiveNotes&, const ProcessContext&, const SequenceEvent&, double atTick) noexcept;
    bool clipGridActive() const noexcept;
    SlotState* slotState (TrackId, bool create) noexcept;
    void queueSlot (TrackId, int scene, double quantise) noexcept;
    static int offsetFor (const ProcessContext&, double tick) noexcept;
    void recordInput (const ProcessContext&, const MidiEventBuffer& midiIn) noexcept;
    void playAudio (const ProcessContext&, AudioBlock&) noexcept;
    void emit (const MidiEvent&) noexcept;

    SpscQueue<Command, 1024> commands;
    SpscQueue<RecordedEvent, 16384> recorded;
    Handoff<const PlaybackSnapshot> snapshots;
    Handoff<Processor> instruments;
    Handoff<const SamplerSet> samplerSets;
    Handoff<const RackSet> rackSets;
    SpscQueue<SlotChange, 1024> slotChanges;
    std::array<SlotState, 256> slotStates;

    // Audio thread state
    const PlaybackSnapshot* snapshot = nullptr;
    Processor* currentInstrument = nullptr;
    const SamplerSet* currentSamplers = nullptr;
    const RackSet* currentRacks = nullptr;
    MidiEventBuffer noMidi { 1 };
    Transport transport;
    Metronome metronome;
    bool clipMode = false; ///< a Clip grid slot is pending or playing: the Timeline is silent
    PlaybackSnapshot emptySnapshot;
    std::vector<ActiveNotes> active;  // one per snapshot track, same order
    MidiEventBuffer events { 8192 };  // everything this block produces
    MidiEventBuffer instrumentIn { 4096 }, samplerIn { 4096 }, scratch { 256 };
    std::array<float*, 64> spanChannels {};
    bool recording = false;
    bool sendSetupOnPlay = false;
    std::uint16_t thruPort = 0;
    int thruChannel = 0;
    /** Where each live key went (port << 8 | channel + 1; 0 = not held), by input channel and key,
        so that its note-off follows it after the thru target changes. Likewise the sustain pedal. */
    std::array<std::array<std::uint32_t, 128>, 16> thruHeld {};
    std::array<std::uint32_t, 16> thruPedal {};
    std::uint16_t clickPort = MidiEvent::kNoPort;
    double blockRate = 48000.0;

    std::atomic<bool> statusPlaying { false }, statusRecording { false };
    std::atomic<double> statusTicks { 0.0 };
    AudioCapture capture;
    std::atomic<bool> captureOn { false };
    std::atomic<bool> monitorOn { false };
    std::atomic<int> inputFirst { 0 }, inputCount { 2 };
    std::vector<float> monitorLeft, monitorRight; ///< a copy of the input of this block
    float monitorLevel = 0.0f;                    ///< audio thread only: moves to 1 or 0 so that the switch does not click
    std::atomic<double> captureStart { -1.0 };
    bool capturing = false; ///< audio thread only
    std::atomic<double> statusSampleRate { 0.0 };
    std::array<std::atomic<float>, 2> masterPeak {};
};

} // namespace arranger

#pragma once

#include "arranger/core/Song.h"
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
    void togglePlay();
    void locate (Tick position);
    void setRecording (bool);

    /** Hands over a new song snapshot (tempo, meter, loop, metronome, tracks). */
    void setSnapshot (std::shared_ptr<const PlaybackSnapshot>);
    void loadSong (const Song& song) { setSnapshot (std::make_shared<const PlaybackSnapshot> (buildSnapshot (song))); }

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

    /** The port to use with sendMidi() and setThru() for a sampler track (kNoPort if it has none yet). */
    std::uint16_t samplerPort (TrackId) const;

    /** Frees snapshots and instruments the audio thread has finished with. Call from a UI timer. */
    void collectGarbage();

    /** Recorded MIDI, oldest first. */
    std::optional<RecordedEvent> popRecorded() noexcept { return recorded.pop(); }

    // ---- audio thread ----------------------------------------------------
    /** Call before the first process() and whenever the device changes. Not real-time safe. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Renders one block. Output audio is overwritten. Hardware MIDI (port 1 and up) goes to `midiOut`.
        Never allocates, locks or waits. */
    void process (AudioBlock& audio, const MidiEventBuffer& midiIn, MidiEventBuffer& midiOut) noexcept;

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
        enum class Type { play, stop, togglePlay, locate, record, thru, midi, clickPort };
        Type type = Type::stop;
        double number = 0.0;
        std::uint16_t port = 0;
        int channel = 0;
        std::array<std::uint8_t, 3> bytes {};
    };

    struct ActiveNotes
    {
        TrackId id = 0;
        std::uint16_t port = 0;
        std::uint8_t channel = 0;
        std::uint64_t contentKey = 0;
        std::bitset<128> on;
    };

    static Command make (Command::Type);
    void send (const Command&);
    void apply (const Command&) noexcept;

    void startPlayback() noexcept;
    void stopPlayback (int offset) noexcept;
    void switchSnapshot (const PlaybackSnapshot*, int offset) noexcept;
    void releaseNotes (ActiveNotes&, int offset) noexcept;
    void releaseAllNotes (int offset) noexcept;
    void sequence (const ProcessContext&) noexcept;
    void recordInput (const ProcessContext&, const MidiEventBuffer& midiIn) noexcept;
    void emit (const MidiEvent&) noexcept;

    SpscQueue<Command, 1024> commands;
    SpscQueue<RecordedEvent, 16384> recorded;
    Handoff<const PlaybackSnapshot> snapshots;
    Handoff<Processor> instruments;
    Handoff<const SamplerSet> samplerSets;

    // Audio thread state
    const PlaybackSnapshot* snapshot = nullptr;
    Processor* currentInstrument = nullptr;
    const SamplerSet* currentSamplers = nullptr;
    Transport transport;
    Metronome metronome;
    PlaybackSnapshot emptySnapshot;
    std::vector<ActiveNotes> active;  // one per snapshot track, same order
    MidiEventBuffer events { 8192 };  // everything this block produces
    MidiEventBuffer instrumentIn { 4096 }, samplerIn { 4096 }, scratch { 256 };
    std::array<float*, 64> spanChannels {};
    bool recording = false;
    bool sendSetupOnPlay = false;
    std::uint16_t thruPort = 0;
    int thruChannel = 0;
    std::uint16_t clickPort = MidiEvent::kNoPort;
    double blockRate = 48000.0;

    std::atomic<bool> statusPlaying { false }, statusRecording { false };
    std::atomic<double> statusTicks { 0.0 };
    std::atomic<double> statusSampleRate { 0.0 };
    std::array<std::atomic<float>, 2> masterPeak {};
};

} // namespace arranger

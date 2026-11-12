#pragma once

#include "arranger/core/Song.h"
#include "arranger/engine/Metronome.h"
#include "arranger/engine/SpscQueue.h"
#include "arranger/engine/Transport.h"

#include <atomic>

namespace arranger
{

/**
    The real-time engine. The UI thread calls the command methods; they are
    queued lock-free and applied at the start of the next audio block. The audio
    thread calls process(). Status getters are safe from any thread.
*/
class Engine
{
public:
    Engine();

    // ---- UI thread -------------------------------------------------------
    void play();
    void stop();
    void togglePlay();
    void locate (Tick position);
    void setTempo (double bpm);
    void setTimeSignature (TimeSignature);
    void setMetronome (const MetronomeSettings&);

    /** Sends tempo, meter and metronome settings from a song. */
    void loadSong (const Song&);

    // ---- audio thread ----------------------------------------------------
    /** Call before the first process() and whenever the device changes. Not real-time safe. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Renders one block. Output audio is overwritten. Never allocates, locks or waits. */
    void process (AudioBlock& audio, const MidiEventBuffer& midiIn, MidiEventBuffer& midiOut) noexcept;

    // ---- any thread ------------------------------------------------------
    bool isPlaying() const noexcept { return statusPlaying.load (std::memory_order_relaxed); }
    double positionTicks() const noexcept { return statusTicks.load (std::memory_order_relaxed); }
    double sampleRate() const noexcept { return statusSampleRate.load (std::memory_order_relaxed); }

private:
    struct Command
    {
        enum class Type { play, stop, togglePlay, locate, tempo, timeSignature, metronome };
        Type type = Type::stop;
        double number = 0.0;
        TimeSignature sig;
        MetronomeSettings metronome;
    };

    static Command make (Command::Type);
    void send (const Command&);
    void apply (const Command&) noexcept;

    SpscQueue<Command, 256> commands;
    Transport transport;
    Metronome metronome;

    std::atomic<bool> statusPlaying { false };
    std::atomic<double> statusTicks { 0.0 };
    std::atomic<double> statusSampleRate { 0.0 };
};

} // namespace arranger

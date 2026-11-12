#include "arranger/engine/Engine.h"

namespace arranger
{

Engine::Engine()
{
    metronome.prepare ({});
}

Engine::Command Engine::make (Command::Type type)
{
    Command c;
    c.type = type;
    return c;
}

void Engine::send (const Command& c)
{
    // The queue holds 256 commands and the audio thread drains it every block,
    // so it is never full in practice. If it is (no audio device running), the
    // command is dropped; the UI re-sends full state when a device starts.
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

void Engine::setTempo (double bpm)
{
    auto c = make (Command::Type::tempo);
    c.number = bpm;
    send (c);
}

void Engine::setTimeSignature (TimeSignature sig)
{
    auto c = make (Command::Type::timeSignature);
    c.sig = sig;
    send (c);
}

void Engine::setMetronome (const MetronomeSettings& m)
{
    auto c = make (Command::Type::metronome);
    c.metronome = m;
    send (c);
}

void Engine::loadSong (const Song& song)
{
    setTempo (song.tempoBpm);
    setTimeSignature (song.timeSignature);
    setMetronome (song.metronome);
}

void Engine::prepare (double rate, int maxBlockSize)
{
    transport.prepare (rate);
    metronome.prepare ({ rate, maxBlockSize });
    statusSampleRate.store (rate);
}

void Engine::apply (const Command& c) noexcept
{
    switch (c.type)
    {
        case Command::Type::play:
            transport.play();
            break;
        case Command::Type::stop:
            transport.stop();
            metronome.reset();
            break;
        case Command::Type::togglePlay:
            if (transport.isPlaying())
            {
                transport.stop();
                metronome.reset();
            }
            else
            {
                transport.play();
            }
            break;
        case Command::Type::locate:
            transport.locate (c.number);
            metronome.reset();
            break;
        case Command::Type::tempo:
            transport.setTempo (c.number);
            break;
        case Command::Type::timeSignature:
            transport.setTimeSignature (c.sig);
            break;
        case Command::Type::metronome:
            metronome.setSettings (c.metronome);
            break;
    }
}

void Engine::process (AudioBlock& audio, const MidiEventBuffer& midiIn, MidiEventBuffer& midiOut) noexcept
{
    while (auto c = commands.pop())
        apply (*c);

    audio.clear();

    const auto ctx = transport.contextForBlock (audio.numSamples);
    metronome.process (ctx, audio, midiIn, midiOut);
    transport.advance (audio.numSamples);

    statusPlaying.store (transport.isPlaying(), std::memory_order_relaxed);
    statusTicks.store (transport.positionTicks(), std::memory_order_relaxed);
}

} // namespace arranger

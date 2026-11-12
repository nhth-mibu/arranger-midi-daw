#include "arranger/engine/Metronome.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace arranger
{

namespace
{
    constexpr double kClickSeconds = 0.035;
    constexpr double kNoteSeconds = 0.05;
    constexpr double kAccentHz = 1760.0;
    constexpr double kBeatHz = 1320.0;
}

void Metronome::prepare (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 48000.0;
    reset();
}

void Metronome::reset() noexcept
{
    voice = {};
    noteOffIn = -1;
}

void Metronome::trigger (bool accent) noexcept
{
    voice.length = static_cast<int> (kClickSeconds * sampleRate);
    voice.samplesLeft = voice.length;
    voice.phase = std::numbers::pi / 2.0; // start at the peak so the first sample is audible
    voice.phaseStep = 2.0 * std::numbers::pi * (accent ? kAccentHz : kBeatHz) / sampleRate;
    voice.gain = settings.level * (accent ? 1.0f : 0.6f);
}

void Metronome::render (AudioBlock& audio, int start, int end) noexcept
{
    if (voice.samplesLeft <= 0 || audio.numChannels == 0)
        return;

    const double decay = 6.0 / static_cast<double> (voice.length); // about -52 dB at the end
    const int attack = std::max (1, static_cast<int> (0.0005 * sampleRate));

    for (int i = start; i < end && voice.samplesLeft > 0; ++i)
    {
        const int age = voice.length - voice.samplesLeft;
        const double attackGain = age < attack ? static_cast<double> (age + 1) / attack : 1.0;
        const double env = attackGain * std::exp (-decay * age);
        const float s = static_cast<float> (std::sin (voice.phase) * env) * voice.gain;

        for (int c = 0; c < audio.numChannels; ++c)
            audio.channels[c][i] += s;

        voice.phase += voice.phaseStep;
        --voice.samplesLeft;
    }
}

void Metronome::process (const ProcessContext& ctx, AudioBlock& audio,
                         const MidiEventBuffer&, MidiEventBuffer& midiOut) noexcept
{
    const int n = ctx.numSamples;
    int cursor = 0;

    auto flushNoteOff = [&] (int upTo) {
        if (noteOffIn >= 0 && noteOffIn < upTo)
        {
            midiOut.add (MidiEvent::noteOff (noteOffIn, kMidiChannel, pendingNote));
            noteOffIn = -1;
        }
    };

    if (ctx.playing && settings.enabled && ctx.ticksPerSample > 0.0)
    {
        const Tick beatTicks = ctx.timeSignature.ticksPerBeat();
        const std::int64_t blockStart = ctx.sampleTime;
        const std::int64_t blockEnd = blockStart + n;

        // Start one beat back so a click exactly on the block's first sample is never missed.
        auto beat = static_cast<std::int64_t> (std::floor (ctx.tickAtStart / static_cast<double> (beatTicks))) - 1;

        for (;; ++beat)
        {
            const double tick = static_cast<double> (beat * beatTicks);
            const std::int64_t at = ctx.sampleForTick (tick);
            if (at >= blockEnd)
                break;
            if (at < blockStart)
                continue;

            const int offset = static_cast<int> (at - blockStart);
            render (audio, cursor, offset);
            if (noteOffIn >= 0) // end the previous note before the next one starts
            {
                midiOut.add (MidiEvent::noteOff (std::min (noteOffIn, offset), kMidiChannel, pendingNote));
                noteOffIn = -1;
            }
            cursor = offset;

            const auto beatsPerBar = static_cast<std::int64_t> (ctx.timeSignature.numerator);
            const bool downbeat = ((beat % beatsPerBar) + beatsPerBar) % beatsPerBar == 0;
            const bool accent = settings.accentDownbeat && downbeat;

            trigger (accent);
            pendingNote = accent ? kAccentNote : kBeatNote;
            midiOut.add (MidiEvent::noteOn (offset, kMidiChannel, pendingNote, accent ? 110 : 90));
            noteOffIn = offset + static_cast<int> (kNoteSeconds * sampleRate);
        }
    }

    render (audio, cursor, n);
    flushNoteOff (n);
    if (noteOffIn >= 0)
        noteOffIn -= n; // carries into the next block
}

} // namespace arranger

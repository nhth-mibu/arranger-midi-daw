#pragma once

#include "arranger/core/Song.h"
#include "arranger/engine/Processor.h"

namespace arranger
{

/**
    Sample-accurate click on every beat of the meter (quarters in 4/4, eighths
    in 6/8), with a higher accent on the downbeat.

    It makes its own short synthesized click, and also writes a matching GM
    drum note (wood blocks on channel 10) to the MIDI output, so the click can
    later be played by an external keyboard as well.
*/
class Metronome final : public Processor
{
public:
    static constexpr int kMidiChannel = 10;
    static constexpr int kAccentNote = 76; ///< GM Hi Wood Block
    static constexpr int kBeatNote = 77;   ///< GM Low Wood Block

    void setSettings (const MetronomeSettings& s) noexcept { settings = s; }
    const MetronomeSettings& getSettings() const noexcept { return settings; }

    void prepare (const ProcessSpec&) override;
    void process (const ProcessContext&, AudioBlock&, const MidiEventBuffer&, MidiEventBuffer&) noexcept override;
    void reset() noexcept override;

private:
    struct Voice
    {
        int samplesLeft = 0;
        int length = 0;
        double phase = 0.0;
        double phaseStep = 0.0;
        float gain = 0.0f;
    };

    void trigger (bool accent) noexcept;
    void render (AudioBlock&, int start, int end) noexcept;

    MetronomeSettings settings;
    double sampleRate = 48000.0;
    Voice voice;
    int noteOffIn = -1; ///< samples until the pending MIDI note-off, -1 when none
    int pendingNote = 0;
};

} // namespace arranger

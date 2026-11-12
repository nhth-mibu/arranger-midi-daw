#pragma once

#include "arranger/engine/Processor.h"

#include <array>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace arranger
{

/**
    The built-in GM/GS player: FluidSynth playing an SF2 file on 16 MIDI
    channels. Each channel renders to its own stereo pair, so SoundFont tracks
    get real level meters. Reverb and chorus follow CC91 and CC93 per channel.

    Create and load on the UI thread (loading takes a moment), then hand it to
    the engine with Engine::setInstrument().
*/
class SoundFontPlayer final : public Processor
{
public:
    struct Preset
    {
        int bank = 0;
        int program = 0;
        std::string name;
    };

    struct LoadError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    /** Loads an SF2 (or SF3 where supported). Throws LoadError. */
    SoundFontPlayer (const std::string& path, double sampleRate, int maxBlockSize);
    ~SoundFontPlayer() override;

    const std::string& path() const noexcept { return filePath; }
    const std::string& name() const noexcept { return fontName; }
    const std::vector<Preset>& presets() const noexcept { return presetList; }

    /** Peak level of a MIDI channel (0..15) since the last call. */
    float takeChannelPeak (int channel) noexcept;

    void prepare (const ProcessSpec&) override;
    void process (const ProcessContext&, AudioBlock&, const MidiEventBuffer&, MidiEventBuffer&) noexcept override;
    void reset() noexcept override;

private:
    void handle (const MidiEvent&) noexcept;
    void render (AudioBlock&, int start, int count) noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl;
    std::string filePath, fontName;
    std::vector<Preset> presetList;
    int maxBlock = 512;
    std::vector<std::vector<float>> channelBuffers; // 16 stereo pairs, then 4 fx buffers
    std::vector<float*> outPtrs, fxPtrs;
    std::array<std::atomic<float>, 16> peaks {};
};

} // namespace arranger

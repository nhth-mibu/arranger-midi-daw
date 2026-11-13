#pragma once

#include "arranger/core/Sample.h"
#include "arranger/core/Transients.h"
#include "arranger/engine/Processor.h"

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace arranger
{

/** One pad of a kit: a sample (or a slice of one) played by one MIDI note. */
struct DrumPad
{
    std::string name;
    int note = 36;                        ///< MIDI note that plays the pad
    std::shared_ptr<const Sample> sample; ///< shared, so slices of one loop don't copy it
    Slice range;                          ///< part of the sample to play; end 0 means all of it
    float gain = 1.0f;
    float pan = 0.0f;                     ///< -1 left .. 1 right
    double tuneSemitones = 0.0;
    int chokeGroup = 0;                   ///< pads in the same group (not 0) cut each other: open and closed hi-hat
    bool oneShot = true;                  ///< true: plays to its end; false: stops at note-off
};

/** A set of pads. Built on the UI thread, then never changed: to edit a kit, hand the engine a new one. */
struct DrumKit
{
    std::string name;
    std::vector<DrumPad> pads;

    const DrumPad* padForNote (int note) const noexcept;
};

/** A kit with one pad per slice, on rising notes from `firstNote`. */
DrumKit kitFromSlices (std::shared_ptr<const Sample>, const std::vector<Slice>&, int firstNote = 36);

/**
    The built-in drum sampler and slice player. Plays any MIDI channel; note
    velocity sets the level. Real-time safe: all voices are allocated up front.
*/
class DrumSampler final : public Processor
{
public:
    static constexpr int kVoices = 32;

    explicit DrumSampler (DrumKit);

    const DrumKit& kit() const noexcept { return drumKit; }

    void prepare (const ProcessSpec&) override;
    void process (const ProcessContext&, AudioBlock&, const MidiEventBuffer& midiIn, MidiEventBuffer& midiOut) noexcept override;
    void reset() noexcept override;

    /** Peak output level since the last call (for a meter). Any thread. */
    float takePeak() noexcept { return peak.exchange (0.0f, std::memory_order_relaxed); }

    /** Master level of the sampler, 0..1 and above. Any thread. */
    void setGain (float g) noexcept { gain.store (g, std::memory_order_relaxed); }

private:
    struct Voice
    {
        const DrumPad* pad = nullptr;
        double position = 0.0; ///< in sample frames
        double step = 1.0;
        double end = 0.0;
        float left = 0.0f, right = 0.0f;
        float fade = 1.0f;      ///< 1 while playing; falls to 0 when choked or released
        float fadeStep = 0.0f;
        std::uint64_t age = 0;
        bool active = false;
    };

    void noteOn (int note, int velocity) noexcept;
    void noteOff (int note) noexcept;
    void render (AudioBlock&, int start, int count) noexcept;

    DrumKit drumKit;
    std::array<const DrumPad*, 128> padByNote {};
    std::array<Voice, kVoices> voices {};
    double rate = 48000.0;
    float releaseStep = 0.001f;
    std::uint64_t clock = 0;
    std::atomic<float> peak { 0.0f };
    std::atomic<float> gain { 1.0f };
};

} // namespace arranger

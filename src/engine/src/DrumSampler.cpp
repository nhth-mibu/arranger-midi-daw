#include "arranger/engine/DrumSampler.h"

#include <algorithm>
#include <cmath>

namespace arranger
{

const DrumPad* DrumKit::padForNote (int note) const noexcept
{
    for (const auto& p : pads)
        if (p.note == note)
            return &p;
    return nullptr;
}

DrumKit kitFromSlices (std::shared_ptr<const Sample> sample, const std::vector<Slice>& slices, int firstNote)
{
    DrumKit kit;
    if (sample == nullptr)
        return kit;
    kit.name = sample->name;
    int note = firstNote;
    for (const auto& s : slices)
    {
        if (note > 127)
            break;
        DrumPad pad;
        pad.name = "Slice " + std::to_string (note - firstNote + 1);
        pad.note = note++;
        pad.sample = sample;
        pad.range = s;
        kit.pads.push_back (std::move (pad));
    }
    return kit;
}

DrumSampler::DrumSampler (DrumKit k) : drumKit (std::move (k))
{
    for (const auto& p : drumKit.pads)
        if (p.note >= 0 && p.note < 128 && p.sample != nullptr && p.sample->frames() > 0)
            padByNote[static_cast<std::size_t> (p.note)] = &p;
}

void DrumSampler::prepare (const ProcessSpec& spec)
{
    rate = spec.sampleRate > 0 ? spec.sampleRate : 48000.0;
    releaseStep = static_cast<float> (1.0 / (0.005 * rate)); // 5 ms fade: no click when a voice is cut
}

void DrumSampler::reset() noexcept
{
    for (auto& v : voices)
        if (v.active)
            v.fadeStep = releaseStep;
}

void DrumSampler::noteOn (int note, int velocity) noexcept
{
    const auto* pad = padByNote[static_cast<std::size_t> (note & 0x7f)];
    if (pad == nullptr)
        return;

    // The same pad again, or a pad in the same choke group, cuts what is playing.
    for (auto& v : voices)
        if (v.active && (v.pad == pad || (pad->chokeGroup != 0 && v.pad->chokeGroup == pad->chokeGroup)))
            v.fadeStep = releaseStep;

    // A free voice, or else the oldest one.
    Voice* voice = nullptr;
    for (auto& v : voices)
        if (! v.active)
        {
            voice = &v;
            break;
        }
    if (voice == nullptr)
        voice = &*std::min_element (voices.begin(), voices.end(), [] (const Voice& a, const Voice& b) { return a.age < b.age; });

    const auto frames = static_cast<double> (pad->sample->frames());
    const double start = std::min (static_cast<double> (pad->range.start), frames);
    const double end = pad->range.end > pad->range.start ? std::min (static_cast<double> (pad->range.end), frames) : frames;
    if (end <= start)
        return;

    const float level = pad->gain * static_cast<float> (velocity * velocity) / (127.0f * 127.0f);
    const float angle = (std::clamp (pad->pan, -1.0f, 1.0f) + 1.0f) * 0.785398163f; // equal power
    voice->pad = pad;
    voice->position = start;
    voice->end = end;
    voice->step = pad->sample->sampleRate / rate * std::pow (2.0, pad->tuneSemitones / 12.0);
    voice->left = level * std::cos (angle) * 1.41421356f;
    voice->right = level * std::sin (angle) * 1.41421356f;
    voice->fade = 1.0f;
    voice->fadeStep = 0.0f;
    voice->age = ++clock;
    voice->active = true;
}

void DrumSampler::noteOff (int note) noexcept
{
    for (auto& v : voices)
        if (v.active && v.pad->note == note && ! v.pad->oneShot)
            v.fadeStep = releaseStep;
}

void DrumSampler::render (AudioBlock& audio, int start, int count) noexcept
{
    if (audio.numChannels < 1 || count <= 0)
        return;
    float* outL = audio.channels[0] + start;
    float* outR = audio.numChannels > 1 ? audio.channels[1] + start : nullptr;
    const float master = gain.load (std::memory_order_relaxed);
    float blockPeak = 0.0f;

    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        const auto& chans = v.pad->sample->channels;
        const float* srcL = chans[0].data();
        const float* srcR = chans.size() > 1 ? chans[1].data() : srcL;
        const auto last = static_cast<std::size_t> (v.end) - 1;

        for (int i = 0; i < count; ++i)
        {
            if (v.position >= v.end || v.fade <= 0.0f)
            {
                v.active = false;
                break;
            }
            const auto index = static_cast<std::size_t> (v.position);
            const auto next = std::min (index + 1, last);
            const auto frac = static_cast<float> (v.position - static_cast<double> (index));
            const float l = (srcL[index] + (srcL[next] - srcL[index]) * frac) * v.left * v.fade * master;
            const float r = (srcR[index] + (srcR[next] - srcR[index]) * frac) * v.right * v.fade * master;
            if (outR != nullptr)
            {
                outL[i] += l;
                outR[i] += r;
            }
            else
                outL[i] += 0.5f * (l + r);
            blockPeak = std::max (blockPeak, std::max (std::abs (l), std::abs (r)));
            v.position += v.step;
            v.fade -= v.fadeStep;
        }
        if (v.position >= v.end || v.fade <= 0.0f)
            v.active = false;
    }

    if (blockPeak > peak.load (std::memory_order_relaxed))
        peak.store (blockPeak, std::memory_order_relaxed);
}

void DrumSampler::process (const ProcessContext&, AudioBlock& audio, const MidiEventBuffer& midiIn, MidiEventBuffer&) noexcept
{
    int cursor = 0;
    for (const auto& e : midiIn)
    {
        const int at = std::clamp (e.sampleOffset, 0, audio.numSamples);
        if (at > cursor)
        {
            render (audio, cursor, at - cursor);
            cursor = at;
        }
        if (e.isNoteOn())
            noteOn (e.bytes[1], e.bytes[2]);
        else if (e.isNoteOff())
            noteOff (e.bytes[1]);
        else if ((e.bytes[0] & 0xf0) == 0xb0 && (e.bytes[1] == 120 || e.bytes[1] == 123))
            reset();
    }
    if (cursor < audio.numSamples)
        render (audio, cursor, audio.numSamples - cursor);
}

} // namespace arranger

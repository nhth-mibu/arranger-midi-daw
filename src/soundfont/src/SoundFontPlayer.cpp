#include "arranger/soundfont/SoundFontPlayer.h"

#include <fluidsynth.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace arranger
{

namespace
{
    constexpr int kChannels = 16;

    void quietLogs()
    {
        // Keep FluidSynth's info and warning chatter out of the console; errors still show.
        static const bool done = [] {
            fluid_set_log_function (FLUID_DBG, nullptr, nullptr);
            fluid_set_log_function (FLUID_INFO, nullptr, nullptr);
            fluid_set_log_function (FLUID_WARN, nullptr, nullptr);
            return true;
        }();
        (void) done;
    }
}

struct SoundFontPlayer::Impl
{
    fluid_settings_t* settings = nullptr;
    fluid_synth_t* synth = nullptr;

    ~Impl()
    {
        if (synth != nullptr)
            delete_fluid_synth (synth);
        if (settings != nullptr)
            delete_fluid_settings (settings);
    }
};

SoundFontPlayer::SoundFontPlayer (const std::string& path, double sampleRate, int maxBlockSize)
    : impl (std::make_unique<Impl>()), filePath (path), maxBlock (std::max (64, maxBlockSize))
{
    quietLogs();
    if (! std::filesystem::exists (path))
        throw LoadError ("SoundFont not found: " + path);

    impl->settings = new_fluid_settings();
    auto* s = impl->settings;
    fluid_settings_setnum (s, "synth.sample-rate", sampleRate > 0 ? sampleRate : 48000.0);
    fluid_settings_setint (s, "synth.audio-channels", kChannels);
    fluid_settings_setint (s, "synth.audio-groups", kChannels);
    fluid_settings_setint (s, "synth.effects-groups", 1);
    fluid_settings_setint (s, "synth.polyphony", 256);
    fluid_settings_setint (s, "synth.threadsafe-api", 0); // only the audio thread touches it
    fluid_settings_setint (s, "synth.cpu-cores", 1);
    fluid_settings_setnum (s, "synth.gain", 0.5);
    fluid_settings_setstr (s, "synth.midi-bank-select", "gs");
    fluid_settings_setint (s, "synth.reverb.active", 1);
    fluid_settings_setint (s, "synth.chorus.active", 1);

    impl->synth = new_fluid_synth (s);
    if (impl->synth == nullptr)
        throw LoadError ("Can't start the SoundFont player");

    const int id = fluid_synth_sfload (impl->synth, path.c_str(), 1);
    if (id == FLUID_FAILED)
        throw LoadError ("Can't read SoundFont: " + path);

    fontName = std::filesystem::path (path).stem().string();
    if (auto* font = fluid_synth_get_sfont_by_id (impl->synth, id))
    {
        fluid_sfont_iteration_start (font);
        while (auto* preset = fluid_sfont_iteration_next (font))
            presetList.push_back ({ fluid_preset_get_banknum (preset), fluid_preset_get_num (preset), fluid_preset_get_name (preset) });
        std::sort (presetList.begin(), presetList.end(),
                   [] (const Preset& a, const Preset& b) { return a.bank != b.bank ? a.bank < b.bank : a.program < b.program; });
    }

    channelBuffers.assign (kChannels * 2 + 4, std::vector<float> (static_cast<std::size_t> (maxBlock), 0.0f));
    for (int i = 0; i < kChannels * 2; ++i)
        outPtrs.push_back (channelBuffers[static_cast<std::size_t> (i)].data());
    for (int i = 0; i < 4; ++i)
        fxPtrs.push_back (channelBuffers[static_cast<std::size_t> (kChannels * 2 + i)].data());
}

SoundFontPlayer::~SoundFontPlayer() = default;

void SoundFontPlayer::prepare (const ProcessSpec& spec)
{
    if (spec.maxBlockSize > maxBlock)
    {
        maxBlock = spec.maxBlockSize;
        for (auto& b : channelBuffers)
            b.assign (static_cast<std::size_t> (maxBlock), 0.0f);
        for (int i = 0; i < kChannels * 2; ++i)
            outPtrs[static_cast<std::size_t> (i)] = channelBuffers[static_cast<std::size_t> (i)].data();
        for (int i = 0; i < 4; ++i)
            fxPtrs[static_cast<std::size_t> (i)] = channelBuffers[static_cast<std::size_t> (kChannels * 2 + i)].data();
    }

    double current = 0.0;
    fluid_settings_getnum (impl->settings, "synth.sample-rate", &current);
    if (spec.sampleRate > 0 && spec.sampleRate != current)
    {
#if defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
 #pragma warning(push)
 #pragma warning(disable : 4996)
#endif
        fluid_synth_set_sample_rate (impl->synth, static_cast<float> (spec.sampleRate));
#if defined(__GNUC__)
 #pragma GCC diagnostic pop
#elif defined(_MSC_VER)
 #pragma warning(pop)
#endif
        fluid_settings_setnum (impl->settings, "synth.sample-rate", spec.sampleRate);
    }
}

void SoundFontPlayer::reset() noexcept
{
    for (int ch = 0; ch < kChannels; ++ch)
        fluid_synth_all_notes_off (impl->synth, ch);
}

float SoundFontPlayer::takeChannelPeak (int channel) noexcept
{
    return peaks[static_cast<std::size_t> (std::clamp (channel, 0, kChannels - 1))].exchange (0.0f, std::memory_order_relaxed);
}

void SoundFontPlayer::handle (const MidiEvent& e) noexcept
{
    auto* synth = impl->synth;
    const int ch = e.bytes[0] & 0x0f;
    const int d1 = e.bytes[1] & 0x7f, d2 = e.bytes[2] & 0x7f;
    switch (e.bytes[0] & 0xf0)
    {
        case 0x90:
            if (d2 > 0)
                fluid_synth_noteon (synth, ch, d1, d2);
            else
                fluid_synth_noteoff (synth, ch, d1);
            break;
        case 0x80: fluid_synth_noteoff (synth, ch, d1); break;
        case 0xb0: fluid_synth_cc (synth, ch, d1, d2); break;
        case 0xc0: fluid_synth_program_change (synth, ch, d1); break;
        case 0xd0: fluid_synth_channel_pressure (synth, ch, d1); break;
        case 0xe0: fluid_synth_pitch_bend (synth, ch, (d2 << 7) | d1); break;
        default: break;
    }
}

void SoundFontPlayer::render (AudioBlock& audio, int start, int count) noexcept
{
    while (count > 0)
    {
        const int n = std::min (count, maxBlock);
        for (auto& b : channelBuffers)
            std::fill (b.begin(), b.begin() + n, 0.0f);

        fluid_synth_process (impl->synth, n, static_cast<int> (fxPtrs.size()), fxPtrs.data(),
                             static_cast<int> (outPtrs.size()), outPtrs.data());

        float* left = audio.numChannels > 0 ? audio.channels[0] + start : nullptr;
        float* right = audio.numChannels > 1 ? audio.channels[1] + start : left;

        for (int ch = 0; ch < kChannels; ++ch)
        {
            const float* l = outPtrs[static_cast<std::size_t> (ch * 2)];
            const float* r = outPtrs[static_cast<std::size_t> (ch * 2 + 1)];
            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                peak = std::max (peak, std::max (std::abs (l[i]), std::abs (r[i])));
                if (left != nullptr)
                {
                    left[i] += l[i];
                    if (right != left)
                        right[i] += r[i];
                }
            }
            auto& meter = peaks[static_cast<std::size_t> (ch)];
            if (peak > meter.load (std::memory_order_relaxed))
                meter.store (peak, std::memory_order_relaxed);
        }

        // Reverb and chorus returns.
        if (left != nullptr)
            for (int i = 0; i < n; ++i)
            {
                left[i] += fxPtrs[0][i] + fxPtrs[2][i];
                if (right != left)
                    right[i] += fxPtrs[1][i] + fxPtrs[3][i];
            }

        start += n;
        count -= n;
    }
}

void SoundFontPlayer::process (const ProcessContext&, AudioBlock& audio, const MidiEventBuffer& midiIn, MidiEventBuffer&) noexcept
{
    // Render up to each event, apply it, carry on. Events closer than a few
    // samples are applied together, to keep the render calls efficient.
    int cursor = 0;
    for (const auto& e : midiIn)
    {
        const int at = std::clamp (e.sampleOffset, 0, audio.numSamples);
        if (at - cursor >= 8)
        {
            render (audio, cursor, at - cursor);
            cursor = at;
        }
        handle (e);
    }
    if (cursor < audio.numSamples)
        render (audio, cursor, audio.numSamples - cursor);
}

} // namespace arranger

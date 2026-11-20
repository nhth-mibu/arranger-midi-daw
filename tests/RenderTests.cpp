// The whole mix, rendered offline: SoundFont, drum sampler and audio clips
// together, with tempo changes, a loop and automation.
//
// 1. The audio must not depend on the audio buffer size (64, 512, random).
// 2. The audio must match a stored description of the last good render
//    (tests/golden/*.json: level per quarter second, and where sounds start),
//    so a code change can't change the sound by accident. To accept a change
//    on purpose, run the test with ARRANGER_UPDATE_GOLDEN=1 and commit the file.

#include "TestEnv.h"
#include "TestRender.h"
#include "TestSoundFont.h"

#include "arranger/core/SongEdit.h"
#include "arranger/soundfont/SoundFontPlayer.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

using namespace arranger;
using test::Renderer;
using nlohmann::json;

namespace
{
    constexpr double kRate = 48000.0;

    std::shared_ptr<const Sample> sine (double hz, double seconds)
    {
        auto s = std::make_shared<Sample>();
        s->sampleRate = 44100.0; // not the engine rate: the clip is resampled
        s->channels.assign (2, std::vector<float> (static_cast<std::size_t> (seconds * s->sampleRate)));
        for (std::size_t i = 0; i < s->channels[0].size(); ++i)
        {
            const auto v = static_cast<float> (0.3 * std::sin (2.0 * 3.14159265358979 * hz * static_cast<double> (i) / s->sampleRate));
            s->channels[0][i] = v;
            s->channels[1][i] = -v;
        }
        return s;
    }

    std::shared_ptr<DrumSampler> kit()
    {
        DrumKit k;
        for (const int note : { 36, 38, 42 })
        {
            auto s = std::make_shared<Sample>();
            s->sampleRate = kRate;
            s->channels.assign (1, std::vector<float> (2400));
            for (std::size_t i = 0; i < s->channels[0].size(); ++i) // a decaying tone per pad
                s->channels[0][i] = static_cast<float> (0.5 * std::exp (-static_cast<double> (i) / 400.0)
                                                        * std::sin (static_cast<double> (i) * note * 0.01));
            DrumPad pad;
            pad.note = note;
            pad.sample = s;
            pad.chokeGroup = note == 42 ? 1 : 0;
            k.pads.push_back (pad);
        }
        auto sampler = std::make_shared<DrumSampler> (std::move (k));
        sampler->prepare ({ kRate, 1024 });
        return sampler;
    }

    /** The reference song: every kind of sound source, a tempo change, a loop and automation. */
    int onlyTrack = -1; // play one track alone (the others muted); -1 = all

    Song referenceSong()
    {
        Song s;
        s.metronome.enabled = false;
        s.tempo.setPoints ({ { 0, 120.0 }, { 3840, 97.0 } });
        s.loop = { true, 0, 3840 * 2 };

        auto& keys = edit::addTrack (s, "Keys");
        keys.output = { TrackOutput::Kind::soundFont, "", 1 };
        keys.sound.program = 0;
        keys.mixer.reverbSend = 0;
        keys.mixer.chorusSend = 0;
        const auto keysId = keys.id;
        Clip c;
        c.length = 3840 * 2;
        c.notes = { { 0, 900, 60, 110 }, { 960, 400, 64, 90 }, { 1920, 1, 67, 127 }, { 2880, 1500, 72, 70 }, { 5000, 2000, 48, 100 } };
        edit::addClip (s, keysId, c);
        edit::setAutomationPoint (s, keysId, 0, { 0, 100.0f });
        edit::setAutomationPoint (s, keysId, 0, { 3840 * 2, 30.0f });
        edit::setAutomationPoint (s, keysId, 1, { 0, 0.0f });
        edit::setAutomationPoint (s, keysId, 1, { 3840, 127.0f });

        auto& drums = edit::addTrack (s, "Drums");
        drums.output = { TrackOutput::Kind::sampler, "", 10 };
        const auto drumsId = drums.id;
        Clip d;
        d.length = 3840 * 2;
        d.loopLength = 960;
        d.notes = { { 0, 120, 36, 127 }, { 480, 120, 42, 80 }, { 720, 60, 42, 60 } };
        edit::addClip (s, drumsId, d);

        auto& audio = edit::addAudioTrack (s, "Pad");
        audio.output.kind = TrackOutput::Kind::audio;
        audio.mixer.pan = 30;
        AudioClip a;
        a.file = "sine.wav";
        a.start = 480;
        a.length = 3840;
        a.offset = 0.1;
        a.fadeIn = 240;
        a.fadeOut = 480;
        a.gainDb = -3.0f;
        edit::addAudioClip (s, audio.id, a);
        if (onlyTrack >= 0)
            for (int i = 0; i < 3; ++i)
                s.tracks[static_cast<std::size_t> (i)].mixer.mute = i != onlyTrack;
        return s;
    }

    struct Rendered
    {
        std::vector<float> left, right;
    };

    Rendered render (const std::function<int()>& blockSize, int maxBlock, std::int64_t samples)
    {
        static const auto font = test::writeTestSoundFont ("arranger-render-test.sf2");
        auto player = std::make_shared<SoundFontPlayer> (font.string(), kRate, maxBlock);
        const auto song = referenceSong();
        const auto pad = sine (330.0, 3.0);
        auto samplers = std::make_shared<SamplerSet>();
        samplers->entries.push_back ({ song.tracks[1].id, kit() });

        Engine e;
        e.prepare (kRate, maxBlock);
        e.setInstrument (player);
        e.setSamplers (samplers);
        e.setSnapshot (std::make_shared<const PlaybackSnapshot> (buildSnapshot (song, {}, [&] (const std::string&) { return pad; })));
        e.play();

        Rendered out;
        std::vector<float> l (static_cast<std::size_t> (maxBlock)), r (static_cast<std::size_t> (maxBlock));
        MidiEventBuffer in (16), midi (1024);
        for (std::int64_t done = 0; done < samples;)
        {
            const int n = static_cast<int> (std::min<std::int64_t> (blockSize(), samples - done));
            float* chans[] = { l.data(), r.data() };
            AudioBlock block { chans, 2, n };
            midi.clear();
            e.process (block, in, midi);
            out.left.insert (out.left.end(), l.begin(), l.begin() + n);
            out.right.insert (out.right.end(), r.begin(), r.begin() + n);
            done += n;
        }
        return out;
    }

    double maxDifference (const std::vector<float>& a, const std::vector<float>& b)
    {
        double d = 0.0;
        for (std::size_t i = 0; i < std::min (a.size(), b.size()); ++i)
            d = std::max (d, static_cast<double> (std::abs (a[i] - b[i])));
        return d;
    }

    /** Level per quarter second and where sounds start: small, readable, and stable across compilers. */
    json describe (const Rendered& r)
    {
        json windows = json::array();
        const std::size_t step = static_cast<std::size_t> (kRate / 4);
        for (std::size_t from = 0; from + step <= r.left.size(); from += step)
        {
            double sum = 0.0, peak = 0.0;
            for (std::size_t i = from; i < from + step; ++i)
            {
                const double m = 0.5 * (r.left[i] + r.right[i]);
                sum += m * m;
                peak = std::max (peak, std::abs (m));
            }
            windows.push_back ({ { "rms", std::round (std::sqrt (sum / static_cast<double> (step)) * 1e4) / 1e4 },
                                 { "peak", std::round (peak * 1e4) / 1e4 } });
        }
        // An onset: a loud sample after at least 10 ms of silence.
        std::vector<std::size_t> onsets;
        std::size_t lastLoud = 0;
        bool any = false;
        for (std::size_t i = 0; i < r.left.size() && onsets.size() < 8; ++i)
            if (std::abs (r.left[i]) + std::abs (r.right[i]) > 1e-3f)
            {
                if (! any || i - lastLoud > 480)
                    onsets.push_back (i);
                lastLoud = i;
                any = true;
            }
        return { { "windows", windows }, { "firstOnsets", onsets } };
    }
}

TEST_CASE ("The sampler and audio clips do not depend on the audio buffer size", "[render]")
{
    // Sample-exact: the same output, whatever the block size.
    const std::int64_t samples = static_cast<std::int64_t> (kRate * 6); // two passes of the loop
    for (const int track : { 1, 2 })
    {
        INFO ("track " << track);
        onlyTrack = track;
        const auto a = render ([] { return 64; }, 64, samples);
        const auto b = render ([] { return 512; }, 512, samples);
        std::mt19937 rng (5);
        const auto c = render ([&] { return 1 + static_cast<int> (rng() % 1024); }, 1024, samples);
        onlyTrack = -1;

        double peak = 0.0;
        for (const auto v : a.left)
            peak = std::max (peak, static_cast<double> (std::abs (v)));
        REQUIRE (peak > 0.01); // there is sound
        CHECK (maxDifference (a.left, b.left) < 1e-6);
        CHECK (maxDifference (a.right, b.right) < 1e-6);
        CHECK (maxDifference (a.left, c.left) < 1e-6);
        CHECK (maxDifference (a.right, c.right) < 1e-6);
    }
}

TEST_CASE ("SoundFont notes start within 64 samples, whatever the audio buffer size", "[render]")
{
    // FluidSynth works in steps of 64 samples (1.3 ms at 48 kHz) and applies a note
    // at the next step, so the SoundFont can't be sample-exact. It must stay within one step.
    const std::int64_t samples = static_cast<std::int64_t> (kRate * 6);
    onlyTrack = 0;
    const auto a = describe (render ([] { return 64; }, 64, samples));
    const auto b = describe (render ([] { return 512; }, 512, samples));
    std::mt19937 rng (5);
    const auto c = describe (render ([&] { return 1 + static_cast<int> (rng() % 1024); }, 1024, samples));
    onlyTrack = -1;

    for (const auto* other : { &b, &c })
    {
        REQUIRE ((*other)["firstOnsets"].size() == a["firstOnsets"].size());
        for (std::size_t i = 0; i < a["firstOnsets"].size(); ++i)
            CHECK (std::abs ((*other)["firstOnsets"][i].get<long long>() - a["firstOnsets"][i].get<long long>()) <= 64);
        for (std::size_t i = 0; i < a["windows"].size(); ++i)
        {
            const double x = a["windows"][i]["rms"], y = (*other)["windows"][i]["rms"];
            CHECK (std::abs (x - y) <= 0.002 + 0.05 * x);
        }
    }
}

TEST_CASE ("The mix matches the last good render", "[render][golden]")
{
    const auto now = describe (render ([] { return 512; }, 512, static_cast<std::int64_t> (kRate * 6)));
    const auto file = std::filesystem::path (ARRANGER_TESTS_DIR) / "golden" / "render-reference-song.json";

    if (testenv::envVar ("ARRANGER_UPDATE_GOLDEN").has_value() || ! std::filesystem::exists (file))
    {
        std::filesystem::create_directories (file.parent_path());
        std::ofstream (file) << now.dump (1) << "\n";
        WARN ("Wrote " << file.string());
        return;
    }

    std::ifstream in (file);
    const auto good = json::parse (in);
    REQUIRE (good["windows"].size() == now["windows"].size());
    for (std::size_t i = 0; i < now["windows"].size(); ++i)
    {
        INFO ("window " << i << " (" << static_cast<double> (i) / 4.0 << " s)");
        const double rms = now["windows"][i]["rms"], goodRms = good["windows"][i]["rms"];
        const double peak = now["windows"][i]["peak"], goodPeak = good["windows"][i]["peak"];
        CHECK (std::abs (rms - goodRms) <= 0.002 + 0.02 * goodRms);
        CHECK (std::abs (peak - goodPeak) <= 0.003 + 0.03 * goodPeak);
    }
    // Onsets within 2 samples: rounding may differ between compilers, timing may not.
    REQUIRE (good["firstOnsets"].size() == now["firstOnsets"].size());
    for (std::size_t i = 0; i < now["firstOnsets"].size(); ++i)
        CHECK (std::abs (now["firstOnsets"][i].get<long long>() - good["firstOnsets"][i].get<long long>()) <= 2);
}


#include "TestRender.h"

#include "arranger/core/ProjectFile.h"
#include "arranger/core/SongEdit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace arranger;
using namespace arranger::test;
using Catch::Approx;

namespace
{
    /** An effect: multiplies the audio. */
    struct Gain final : Processor
    {
        explicit Gain (float g) : gain (g) {}
        void prepare (const ProcessSpec&) override {}
        void process (const ProcessContext&, AudioBlock& audio, const MidiEventBuffer&, MidiEventBuffer&) noexcept override
        {
            for (int c = 0; c < audio.numChannels; ++c)
                for (int i = 0; i < audio.numSamples; ++i)
                    audio.channels[c][i] *= gain;
        }
        float gain;
    };

    /** An instrument: a steady level of 1 while a note is down. */
    struct Tone final : Processor
    {
        void prepare (const ProcessSpec&) override {}
        void process (const ProcessContext&, AudioBlock& audio, const MidiEventBuffer& midiIn, MidiEventBuffer&) noexcept override
        {
            auto next = midiIn.begin();
            for (int i = 0; i < audio.numSamples; ++i)
            {
                for (; next != midiIn.end() && next->sampleOffset <= i; ++next)
                {
                    if (next->isNoteOn()) ++down;
                    else if ((next->bytes[0] & 0xf0) == 0x80 || ((next->bytes[0] & 0xf0) == 0x90 && next->bytes[2] == 0)) down = std::max (0, down - 1);
                }
                if (down > 0)
                    for (int c = 0; c < audio.numChannels; ++c)
                        audio.channels[c][i] += 1.0f;
            }
        }
        void reset() noexcept override { down = 0; }
        int down = 0;
    };

    std::shared_ptr<const Sample> steady()
    {
        auto s = std::make_shared<Sample>();
        s->channels.assign (1, std::vector<float> (96000, 0.5f));
        return s;
    }

    std::vector<float> play (Engine& e, std::int64_t samples)
    {
        e.play();
        Renderer r (e, 512);
        r.keepAudio = true;
        r.runRandom (samples, 7);
        return r.audio;
    }
}

TEST_CASE ("Insert effects change one track; master effects change the mix", "[racks]")
{
    // Two audio tracks that each play a steady 0.5.
    Song song;
    song.metronome.enabled = false;
    TrackId ids[2] = {};
    for (auto& id : ids)
    {
        AudioClip clip;
        clip.file = "steady.wav";
        clip.length = 960 * 8;
        id = edit::addAudioTrack (song, "Audio").id;
        edit::addAudioClip (song, id, clip);
    }
    const auto sample = steady();
    const auto snap = std::make_shared<const PlaybackSnapshot> (buildSnapshot (song, {}, [&] (const std::string&) { return sample; }));

    {
        Engine e;
        e.prepare (48000.0, 512);
        e.setSnapshot (snap);
        CHECK (play (e, 24000)[12000] == Approx (1.0).margin (0.001)); // no racks: 0.5 + 0.5
    }
    {
        Engine e;
        e.prepare (48000.0, 512);
        e.setSnapshot (snap);
        auto racks = std::make_shared<RackSet>();
        racks->buses.push_back ({ ids[0], nullptr, { std::make_shared<Gain> (0.5f), std::make_shared<Gain> (0.5f) }, {}, {} });
        racks->prepare (512);
        e.setRacks (racks);
        // The first track goes through two halvings; the second is as it was.
        CHECK (play (e, 24000)[12000] == Approx (0.5 * 0.25 + 0.5).margin (0.001));
    }
    {
        Engine e;
        e.prepare (48000.0, 512);
        e.setSnapshot (snap);
        auto racks = std::make_shared<RackSet>();
        racks->master.push_back (std::make_shared<Gain> (0.1f));
        racks->prepare (512);
        e.setRacks (racks);
        CHECK (play (e, 24000)[12000] == Approx (0.1).margin (0.001));
    }
    {
        // Buses made for short blocks: a longer block plays without them and does not write past them.
        Engine e;
        e.prepare (48000.0, 512);
        e.setSnapshot (snap);
        auto racks = std::make_shared<RackSet>();
        racks->buses.push_back ({ ids[0], nullptr, { std::make_shared<Gain> (0.0f) }, {}, {} });
        racks->prepare (16);
        e.setRacks (racks);
        Renderer r (e, 512);
        r.keepAudio = true;
        e.play();
        r.run (24000, [] { return 512; });
        CHECK (r.audio[12000] == Approx (1.0).margin (0.001));
    }
}

TEST_CASE ("A plugin track plays its notes through its instrument, effects, volume and mute", "[racks]")
{
    Song song; // 120 BPM: one quarter note = 24000 samples
    song.metronome.enabled = false;
    auto& track = edit::addTrack (song, "Synth");
    track.output.kind = TrackOutput::Kind::plugin;
    track.instrument = { "Internal", "tone", "Tone", "", false };
    track.effects.push_back ({ "Internal", "gain", "Gain", "AAEC", true });
    const auto id = track.id;
    Clip clip;
    clip.length = 960 * 4;
    clip.notes = { { 960, 960, 60, 100 } }; // beat 2, one beat long
    edit::addClip (song, id, clip);

    const auto render = [&] (const Song& s) {
        Engine e;
        e.prepare (48000.0, 512);
        e.setSnapshot (std::make_shared<const PlaybackSnapshot> (buildSnapshot (s)));
        auto racks = std::make_shared<RackSet>();
        racks->buses.push_back ({ id, std::make_shared<Tone>(), { std::make_shared<Gain> (0.5f) }, {}, {} });
        racks->prepare (512);
        e.setRacks (racks);
        return play (e, 96000);
    };

    const auto out = render (song);
    CHECK (out[12000] == Approx (0.0).margin (0.001));  // before the note
    CHECK (out[36000] == Approx (0.5).margin (0.001));  // the note, through the effect, at volume 100
    CHECK (out[60000] == Approx (0.0).margin (0.001));  // after the note
    // The note starts on its sample.
    CHECK (out[23998] == Approx (0.0).margin (0.001));
    CHECK (out[24002] == Approx (0.5).margin (0.001));

    auto quiet = song;
    quiet.tracks[0].mixer.volume = 50; // (50 / 100)^2 = a quarter
    CHECK (render (quiet)[36000] == Approx (0.125).margin (0.001));

    auto muted = song;
    muted.tracks[0].mixer.mute = true;
    CHECK (render (muted)[36000] == Approx (0.0).margin (0.001));

    // The plugins are saved with the project.
    song.master.effects.push_back ({ "VST3", "abc", "Limiter", "", false });
    const auto loaded = project_file::fromJson (project_file::toJson (song));
    CHECK (loaded.tracks[0].output.kind == TrackOutput::Kind::plugin);
    CHECK (loaded.tracks[0].instrument == song.tracks[0].instrument);
    CHECK (loaded.tracks[0].effects == song.tracks[0].effects);
    CHECK (loaded.master.effects == song.master.effects);
    CHECK (loaded == song);
}

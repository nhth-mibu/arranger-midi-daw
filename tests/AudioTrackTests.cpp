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
    /** One second of a ramp from 0 to 1: the value says which frame of the file plays. */
    std::shared_ptr<const Sample> ramp (double sampleRate = 48000.0)
    {
        auto s = std::make_shared<Sample>();
        s->sampleRate = sampleRate;
        s->channels.assign (1, std::vector<float> (static_cast<std::size_t> (sampleRate)));
        for (std::size_t i = 0; i < s->channels[0].size(); ++i)
            s->channels[0][i] = static_cast<float> (static_cast<double> (i) / sampleRate);
        return s;
    }

    Song songWithClip (AudioClip clip)
    {
        Song song; // 120 BPM: one quarter note = 24000 samples at 48 kHz
        song.metronome.enabled = false;
        auto& track = edit::addTrack (song, "Audio");
        track.output.kind = TrackOutput::Kind::audio;
        clip.id = edit::takeId (song);
        track.audio.push_back (std::make_shared<const AudioClip> (std::move (clip)));
        return song;
    }

    std::vector<float> render (const Song& song, const std::shared_ptr<const Sample>& sample, std::int64_t samples, unsigned seed = 3)
    {
        Engine e;
        e.prepare (48000.0, 512);
        e.setSnapshot (std::make_shared<const PlaybackSnapshot> (buildSnapshot (song, {}, [&] (const std::string&) { return sample; })));
        e.play();
        Renderer r (e, 512);
        r.keepAudio = true;
        r.runRandom (samples, seed);
        return r.audio;
    }
}

TEST_CASE ("An audio clip plays its file at its place in the song", "[audio]")
{
    AudioClip clip;
    clip.file = "ramp.wav";
    clip.start = 960;   // 0.5 s
    clip.length = 960;  // 0.5 s
    clip.offset = 0.25; // starts a quarter of a second into the file
    const auto audio = render (songWithClip (clip), ramp(), 72000);

    CHECK (audio[23000] == 0.0f);                                   // before the clip
    CHECK (audio[24000 + 4800] == Approx (0.25f + 0.1f).margin (1.0e-4)); // 0.1 s in: file time 0.35 s
    CHECK (audio[24000 + 20000] == Approx (0.25f + 20000.0f / 48000.0f).margin (1.0e-4));
    CHECK (audio[48000 + 10] == 0.0f);                              // after the clip
    CHECK (std::abs (audio[24000 + 2]) < 0.01f);                    // the edges fade: no click
    CHECK (std::abs (audio[48000 - 2]) < 0.02f);
}

TEST_CASE ("An audio file with another sample rate plays at the right speed", "[audio]")
{
    AudioClip clip;
    clip.file = "ramp.wav";
    clip.start = 0;
    clip.length = 1920;
    const auto audio = render (songWithClip (clip), ramp (22050.0), 48000);
    CHECK (audio[12000] == Approx (0.25f).margin (1.0e-3));
    CHECK (audio[36000] == Approx (0.75f).margin (1.0e-3));
}

TEST_CASE ("Audio clips follow gain, fades, volume, mute and solo", "[audio]")
{
    AudioClip clip;
    clip.file = "ramp.wav";
    clip.start = 0;
    clip.length = 1920;
    clip.gainDb = -6.0206f; // half
    clip.fadeIn = 960;      // half a second
    auto song = songWithClip (clip);
    auto audio = render (song, ramp(), 48000);
    CHECK (audio[12000] == Approx (0.25f * 0.5f * 0.5f).margin (1.0e-3)); // half-way up the fade, at half gain
    CHECK (audio[36000] == Approx (0.75f * 0.5f).margin (1.0e-3));

    song.tracks[0].mixer.volume = 50; // (50 / 100) squared = a quarter
    audio = render (song, ramp(), 48000);
    CHECK (audio[36000] == Approx (0.75f * 0.5f * 0.25f).margin (1.0e-3));

    song.tracks[0].mixer.mute = true;
    audio = render (song, ramp(), 48000);
    CHECK (audio[36000] == 0.0f);

    song.tracks[0].mixer.mute = false;
    auto& other = edit::addTrack (song, "Keys");
    other.mixer.solo = true; // another track is soloed
    audio = render (song, ramp(), 48000);
    CHECK (audio[36000] == 0.0f);
}

TEST_CASE ("Audio clips stay in time through a loop and a missing file", "[audio]")
{
    AudioClip clip;
    clip.file = "ramp.wav";
    clip.start = 0;
    clip.length = 960;
    auto song = songWithClip (clip);
    song.loop = { true, 0, 960 };
    const auto audio = render (song, ramp(), 60000);
    CHECK (audio[12000] == Approx (0.25f).margin (1.0e-4));
    CHECK (audio[24000 + 12000] == Approx (0.25f).margin (1.0e-4)); // the second time round

    // No audio for the file: the clip is silent and nothing breaks.
    const auto silent = render (song, nullptr, 24000);
    CHECK (silent[12000] == 0.0f);
}

TEST_CASE ("Audio clips are saved in the project", "[audio]")
{
    AudioClip clip;
    clip.name = "Take 1";
    clip.file = "Audio/take 1.wav";
    clip.start = 480;
    clip.length = 9600;
    clip.offset = 1.5;
    clip.gainDb = -3.0f;
    clip.fadeIn = 120;
    clip.fadeOut = 240;
    const auto song = songWithClip (clip);
    CHECK (song.contentEnd() == 480 + 9600);
    const auto back = project_file::fromJson (project_file::toJson (song));
    REQUIRE (back.tracks.size() == 1);
    CHECK (back.tracks[0].output.kind == TrackOutput::Kind::audio);
    REQUIRE (back.tracks[0].audio.size() == 1);
    CHECK (*back.tracks[0].audio[0] == *song.tracks[0].audio[0]);
    CHECK (back == song);
}

TEST_CASE ("Audio clips can be moved, split and removed like other clips", "[audio]")
{
    Song song;
    auto& midi = edit::addTrack (song, "Keys");
    const auto midiId = midi.id;
    const auto a = edit::addAudioTrack (song, "Audio 1").id;
    const auto b = edit::addAudioTrack (song, "Audio 2").id;
    CHECK (song.tracks[1].output.kind == TrackOutput::Kind::audio);

    AudioClip clip;
    clip.file = "take.wav";
    clip.start = 960;
    clip.length = 3840;
    clip.offset = 1.0;
    clip.fadeIn = 240;
    clip.fadeOut = 240;
    const auto id = edit::addAudioClip (song, a, clip);
    REQUIRE (id != 0);
    CHECK (edit::trackOfClip (song, id) == a);

    edit::moveClip (song, id, 1920, b);
    CHECK (edit::trackOfClip (song, id) == b);
    edit::moveClip (song, id, 0, midiId); // not an audio track: only the time changes
    CHECK (edit::trackOfClip (song, id) == b);
    CHECK (edit::findAudioClip (song, id)->start == 0);

    // Split at beat 2 (one second at 120 BPM): the right part starts one second later in the file.
    const auto right = edit::splitClip (song, id, 1920);
    REQUIRE (right != 0);
    const auto l = edit::findAudioClip (song, id), r = edit::findAudioClip (song, right);
    CHECK (l->length == 1920);
    CHECK (l->fadeOut == 0);
    CHECK (r->start == 1920);
    CHECK (r->length == 1920);
    CHECK (r->offset == Approx (2.0));
    CHECK (r->fadeIn == 0);
    CHECK (r->fadeOut == 240);
    CHECK (edit::splitClip (song, id, 5000) == 0);

    edit::modifyAudioClip (song, right, [] (AudioClip& c) { c.gainDb = 99.0f; c.fadeIn = 99999; });
    CHECK (edit::findAudioClip (song, right)->gainDb == 24.0f);
    CHECK (edit::findAudioClip (song, right)->fadeIn == 1920);
    CHECK (edit::findAudioClip (song, right)->fadeOut == 0);

    edit::removeClip (song, id);
    CHECK (edit::findAudioClip (song, id) == nullptr);
    CHECK (song.tracks[2].audio.size() == 1);
}

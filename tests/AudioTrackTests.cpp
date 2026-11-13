#include "TestRender.h"

#include "arranger/core/ProjectFile.h"
#include "arranger/core/SongEdit.h"
#include "arranger/core/TimeStretch.h"

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

TEST_CASE ("Audio capture keeps the input while the song plays", "[audio]")
{
    Engine e;
    e.prepare (48000.0, 256);
    Song song;
    song.metronome.enabled = false;
    e.setSnapshot (std::make_shared<const PlaybackSnapshot> (buildSnapshot (song, {}, {})));

    std::vector<float> in (256), outL (256), outR (256);
    float* outs[] = { outL.data(), outR.data() };
    float* ins[] = { in.data() };
    MidiEventBuffer midiIn (16), midiOut (64);
    float next = 0.0f;
    const auto block = [&] {
        for (auto& v : in)
            v = next += 1.0f; // each frame has its own number
        AudioBlock out { outs, 2, 256 };
        const AudioBlock input { ins, 1, 256 };
        midiOut.clear();
        e.process (out, midiIn, midiOut, input);
    };

    std::vector<float> l, r;
    block(); // stopped, capture off
    e.setAudioCapture (true);
    block(); // capture on, but the song does not play
    CHECK (e.capturedAudio().pop (l, r) == 0);
    CHECK (e.captureStartTicks() < 0.0);

    e.locate (960);
    e.play();
    block(); // the play command arrives in this block
    for (int i = 0; i < 4; ++i)
        block();
    e.setAudioCapture (false);
    block();

    const auto frames = e.capturedAudio().pop (l, r);
    REQUIRE (frames == 4 * 256);
    CHECK (l == r); // a mono input goes to both sides
    for (std::size_t i = 1; i < l.size(); ++i)
        REQUIRE (l[i] == l[i - 1] + 1.0f); // nothing lost, nothing twice
    // 120 BPM at 48 kHz: one block of 256 frames is 256 / 24000 * 960 ticks.
    CHECK (e.captureStartTicks() == Approx (960.0 + 256.0 / 24000.0 * 960.0).margin (0.01));

    // A reader that is too slow loses the newest frames and can see how many.
    AudioCapture small (8);
    small.push (in.data(), nullptr, 20);
    l.clear();
    r.clear();
    CHECK (small.pop (l, r) == 7);
    CHECK (small.takeLost() == 13);
}

TEST_CASE ("Time-stretch changes the length and keeps the pitch", "[audio][stretch]")
{
    // Two seconds of a 220 Hz sine.
    Sample in;
    in.sampleRate = 48000.0;
    in.channels.assign (2, std::vector<float> (96000));
    for (std::size_t i = 0; i < 96000; ++i)
        in.channels[0][i] = in.channels[1][i] = 0.5f * static_cast<float> (std::sin (6.283185307179586 * 220.0 * static_cast<double> (i) / 48000.0));

    for (const double speed : { 0.8, 1.25, 1.5 })
    {
        const auto out = stretch::changeSpeed (in, speed);
        REQUIRE (out.channelCount() == 2);
        CHECK (out.frames() == static_cast<std::size_t> (std::llround (96000.0 / speed)));

        // Pitch: count the upward zero crossings in the middle second.
        const auto& x = out.channels[0];
        const std::size_t from = x.size() / 2 - 24000, to = from + 48000;
        int crossings = 0;
        float peak = 0.0f;
        for (std::size_t i = from; i < to; ++i)
        {
            crossings += x[i - 1] < 0.0f && x[i] >= 0.0f ? 1 : 0;
            peak = std::max (peak, std::abs (x[i]));
        }
        CHECK (crossings == Approx (220).margin (3));
        CHECK (peak == Approx (0.5).margin (0.1)); // the joins don't cancel or double the sound
    }
    CHECK (stretch::changeSpeed (in, 1.0).channels == in.channels);

    // A two-bar loop of 4 seconds is at 120 BPM, not 60 or 240, when the song is near 110.
    CHECK (stretch::tempoFromLength (4.0, 110.0) == Approx (120.0));
    CHECK (stretch::tempoFromLength (4.0, 70.0) == Approx (60.0));
}

TEST_CASE ("An audio clip with a file tempo follows the song tempo", "[audio][stretch]")
{
    AudioClip clip;
    clip.file = "loop.wav";
    clip.start = 960;
    clip.length = 1920;
    clip.offset = 0.5;
    clip.bpm = 100.0; // the song is at 120: the file plays 1.2 times as fast
    auto song = songWithClip (clip);
    CHECK (edit::audioSpeed (song, *song.tracks[0].audio[0]) == Approx (1.2));

    double asked = 0.0;
    const auto plain = ramp();
    const auto fast = std::make_shared<const Sample> (stretch::changeSpeed (*plain, 1.2));
    const auto snap = buildSnapshot (song, {}, [&] (const std::string&) { return plain; },
                                     [&] (const std::string&, double speed) { asked = speed; return fast; });
    REQUIRE (snap.audio.size() == 1);
    CHECK (asked == Approx (1.2));
    CHECK (snap.audio[0].sample == fast);
    CHECK (snap.audio[0].offset == Approx (0.5 / 1.2)); // the same place in the shorter copy

    // Without a stretcher the clip plays the file as it is.
    CHECK (buildSnapshot (song, {}, [&] (const std::string&) { return plain; }).audio[0].sample == plain);

    // A split keeps the place in the file: half a second of song is 0.6 seconds of file.
    edit::splitClip (song, song.tracks[0].audio[0]->id, 1920);
    REQUIRE (song.tracks[0].audio.size() == 2);
    CHECK (song.tracks[0].audio[1]->offset == Approx (0.5 + 0.5 * 1.2));

    const auto loaded = project_file::fromJson (project_file::toJson (song));
    CHECK (loaded.tracks[0].audio[0]->bpm == Approx (100.0));
}

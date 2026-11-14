#include "TestRender.h"

#include "arranger/core/AudioToMidi.h"
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

TEST_CASE ("Audio to MIDI finds the notes of a melody", "[audio][tomidi]")
{
    // Four notes of 0.4 s with 0.1 s of silence after each: A3, C4, E4, A2. Each has two overtones.
    const int pitches[] = { 57, 60, 64, 45 };
    Sample s;
    s.sampleRate = 44100.0;
    s.channels.assign (1, std::vector<float> (static_cast<std::size_t> (44100 * 2.2), 0.0f));
    for (int n = 0; n < 4; ++n)
    {
        const double hz = 440.0 * std::pow (2.0, (pitches[n] - 69) / 12.0);
        const auto from = static_cast<std::size_t> (44100 * (0.1 + 0.5 * n)), count = static_cast<std::size_t> (44100 * 0.4);
        for (std::size_t i = 0; i < count; ++i)
        {
            const double t = static_cast<double> (i) / 44100.0;
            const double fade = std::min ({ 1.0, t / 0.01, (0.4 - t) / 0.02 });
            const double level = n == 3 ? 0.15 : 0.5; // the last note is quieter
            s.channels[0][from + i] = static_cast<float> (level * fade * (std::sin (6.283185307179586 * hz * t) + 0.5 * std::sin (6.283185307179586 * 2.0 * hz * t) + 0.25 * std::sin (6.283185307179586 * 3.0 * hz * t)) / 1.75);
        }
    }

    const auto found = audio_to_midi::melody (s);
    REQUIRE (found.size() == 4);
    for (int n = 0; n < 4; ++n)
    {
        CHECK (found[static_cast<std::size_t> (n)].pitch == pitches[n]);
        CHECK (found[static_cast<std::size_t> (n)].start == Approx (0.1 + 0.5 * n).margin (0.04));
        CHECK (found[static_cast<std::size_t> (n)].length == Approx (0.4).margin (0.07));
    }
    CHECK (found[3].velocity < found[0].velocity);
    CHECK (audio_to_midi::melody (Sample {}).empty());

    // In a clip at bar 2 that starts 0.5 s into the file, the second note is first, 0.1 s in (120 BPM: 192 ticks).
    Song song;
    AudioClip clip;
    clip.name = "Take";
    clip.start = 3840;
    clip.length = 1920; // one second: holds the second and third notes
    clip.offset = 0.5;
    const auto midi = audio_to_midi::toClip (found, song, clip);
    CHECK (midi.start == 3840);
    CHECK (midi.length == 1920);
    REQUIRE (midi.notes.size() == 2);
    CHECK (midi.notes[0].pitch == 60);
    CHECK (static_cast<double> (midi.notes[0].start) == Approx (192.0).margin (80.0));
    CHECK (midi.notes[1].pitch == 64);
}

TEST_CASE ("Audio to MIDI sorts drum hits into kick, snare and hi-hat", "[audio][tomidi]")
{
    Sample s;
    s.sampleRate = 48000.0;
    s.channels.assign (1, std::vector<float> (48000 * 2, 0.0f));
    unsigned seed = 12345;
    const auto noise = [&] { seed = seed * 1664525u + 1013904223u; return static_cast<double> (seed >> 8) / 8388608.0 - 1.0; };
    const auto hit = [&] (double at, int kind) {
        const auto from = static_cast<std::size_t> (at * 48000.0);
        double low = 0.0;
        for (std::size_t i = 0; i < 4800; ++i)
        {
            const double t = static_cast<double> (i) / 48000.0;
            const double white = noise();
            low += 0.25 * (white - low); // noise with less top, for the snare
            double v = 0.0;
            if (kind == 0) v = std::sin (6.283185307179586 * 60.0 * t) * std::exp (-t * 30.0);                  // kick: a low thump
            if (kind == 1) v = (0.7 * low + 0.3 * std::sin (6.283185307179586 * 190.0 * t)) * std::exp (-t * 40.0); // snare
            if (kind == 2) v = 0.5 * (white - low) * std::exp (-t * 90.0);                                      // hi-hat: only the top
            s.channels[0][from + i] += static_cast<float> (0.8 * v);
        }
    };
    const int kinds[] = { 0, 2, 1, 2, 0, 0, 1, 2 };
    for (int i = 0; i < 8; ++i)
        hit (0.05 + 0.24 * i, kinds[i]);

    const auto found = audio_to_midi::drums (s);
    REQUIRE (found.size() == 8);
    const int notes[] = { 36, 42, 38 };
    const int expected[] = { 0, 2, 1, 2, 0, 0, 1, 2 };
    for (std::size_t i = 0; i < 8; ++i)
    {
        CHECK (found[i].pitch == (expected[i] == 0 ? notes[0] : expected[i] == 1 ? notes[2] : notes[1]));
        CHECK (found[i].start == Approx (0.05 + 0.24 * static_cast<double> (i)).margin (0.02));
    }
}

TEST_CASE ("An audio clip in a Clip grid slot loops in time with the song", "[audio][grid]")
{
    Song song; // 120 BPM: one quarter note = 24000 samples at 48 kHz
    song.metronome.enabled = false;
    auto& track = edit::addAudioTrack (song, "Loops");
    const auto id = track.id;
    AudioClip timeline;
    timeline.file = "ramp.wav";
    timeline.length = 960 * 8;
    edit::addAudioClip (song, id, timeline);

    AudioClip loop;
    loop.file = "ramp.wav";
    loop.length = 960; // one quarter note: half a second of the ramp
    loop.start = 5000; // a slot clip has no place on the Timeline
    const auto clipId = edit::setAudioSlot (song, id, 1, loop);
    REQUIRE (clipId != 0);
    REQUIRE (song.tracks[0].audioSlots.size() == 2);
    CHECK (song.tracks[0].audioSlots[0] == nullptr);
    CHECK (song.tracks[0].audioSlots[1]->start == 0);
    CHECK (edit::sceneOfClip (song, clipId) == 1);
    CHECK (edit::trackOfClip (song, clipId) == id);
    CHECK (song.sceneCount() >= 3);

    // Only audio tracks take audio in their slots.
    auto& midi = edit::addTrack (song, "Piano");
    CHECK (edit::setAudioSlot (song, midi.id, 0, loop) == 0);

    // The project file keeps the slot.
    const auto loaded = project_file::fromJson (project_file::toJson (song));
    CHECK (loaded == song);

    Engine e;
    e.prepare (48000.0, 512);
    const auto sample = ramp();
    e.setSnapshot (std::make_shared<const PlaybackSnapshot> (buildSnapshot (song, {}, [&] (const std::string&) { return sample; })));
    e.launchSlot (id, 1, 0); // starts playback at once
    Renderer r (e, 512);
    r.keepAudio = true;
    r.runRandom (72000, 5);
    const auto& audio = r.audio;

    // The slot plays in place of the Timeline clip, and starts again each quarter note.
    CHECK (audio[4800] == Approx (0.1f).margin (1.0e-3));
    CHECK (audio[20000] == Approx (20000.0f / 48000.0f).margin (1.0e-3));
    CHECK (audio[24000 + 4800] == Approx (0.1f).margin (1.0e-3));
    CHECK (audio[48000 + 12000] == Approx (0.25f).margin (1.0e-3));
    CHECK (std::abs (audio[24000 - 2]) < 0.02f); // the loop point fades: no click
    CHECK (std::abs (audio[24000 + 2]) < 0.02f);
    CHECK (e.slotStatus (id).playing == 1);

    // After a stop, the Timeline clip plays again.
    e.stopSlot (id, 0);
    Renderer after (e, 512);
    after.keepAudio = true;
    after.runRandom (9600, 6);
    CHECK (e.slotStatus (id).playing == -1);
    CHECK (after.audio[9000] == 0.0f); // the Timeline clip is past the end of its one-second file here; the loop is silent

    // Removing the clip empties the slot.
    edit::removeClip (song, clipId);
    CHECK (song.tracks[0].audioSlots[1] == nullptr);
}

TEST_CASE ("Input monitoring puts the audio input in the output", "[audio]")
{
    Engine e;
    e.prepare (48000.0, 256);
    Song song;
    song.metronome.enabled = false;
    e.setSnapshot (std::make_shared<const PlaybackSnapshot> (buildSnapshot (song, {}, {})));

    // The device can use the same buffers for the input and the output.
    std::vector<float> left (256), right (256);
    float* outs[] = { left.data(), right.data() };
    float* ins[] = { left.data() };
    MidiEventBuffer midiIn (16), midiOut (64);
    const auto block = [&] {
        std::fill (left.begin(), left.end(), 0.5f);
        AudioBlock out { outs, 2, 256 };
        const AudioBlock input { ins, 1, 256 };
        midiOut.clear();
        e.process (out, midiIn, midiOut, input);
    };

    block(); // off: the input does not go to the output
    CHECK (left[100] == 0.0f);
    CHECK (right[100] == 0.0f);

    e.setInputMonitor (true);
    block(); // the level comes up over this block: no click
    CHECK (left[0] < 0.01f);
    CHECK (left[255] == Approx (0.5f));
    block();
    CHECK (left[0] == Approx (0.5f));
    CHECK (right[200] == Approx (0.5f)); // one input channel goes to both sides

    e.setInputMonitor (false);
    block(); // and goes down again
    CHECK (left[0] > 0.4f);
    CHECK (left[255] == Approx (0.0f).margin (1.0e-6));
    block();
    CHECK (left[100] == 0.0f);
}

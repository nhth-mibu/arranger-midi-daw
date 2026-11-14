#include "TestRender.h"

#include "arranger/core/ProjectFile.h"
#include "arranger/core/SongEdit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace arranger;
using namespace arranger::test;
using Catch::Approx;

TEST_CASE ("An automation line moves in straight lines between its points", "[automation]")
{
    Song song;
    const auto id = edit::addTrack (song, "Piano").id;
    CHECK (automation::valueAt ({}, 500.0, 90.0f) == 90.0f);

    CHECK (edit::setAutomationPoint (song, id, 0, { 1920, 100.0f }) == 0);
    CHECK (edit::setAutomationPoint (song, id, 0, { 960, 20.0f }) == 0);
    CHECK (edit::setAutomationPoint (song, id, 0, { 3840, 200.0f }) == 2); // clamped to 127
    const auto& line = song.tracks[0].volumeAutomation;
    REQUIRE (line.size() == 3);
    CHECK (line[2].value == 127.0f);
    CHECK (automation::valueAt (line, 0.0, 0.0f) == 20.0f);      // level before the first point
    CHECK (automation::valueAt (line, 1440.0, 0.0f) == Approx (60.0f));
    CHECK (automation::valueAt (line, 9999.0, 0.0f) == 127.0f);  // level after the last point

    CHECK (edit::setAutomationPoint (song, id, 0, { 960, 40.0f }) == 0); // the same time: replaced
    CHECK (song.tracks[0].volumeAutomation.size() == 3);
    edit::setAutomationPoint (song, id, 1, { 0, 0.0f });

    const auto loaded = project_file::fromJson (project_file::toJson (song));
    CHECK (loaded.tracks[0].volumeAutomation == song.tracks[0].volumeAutomation);
    CHECK (loaded.tracks[0].panAutomation == song.tracks[0].panAutomation);

    edit::removeAutomationPoint (song, id, 0, 1);
    CHECK (song.tracks[0].volumeAutomation.size() == 2);
    edit::clearAutomation (song, id, 0);
    CHECK (song.tracks[0].volumeAutomation.empty());
}

TEST_CASE ("Automation of a MIDI track goes out as controller messages", "[automation]")
{
    Song song;
    song.metronome.enabled = false;
    auto& track = edit::addTrack (song, "Synth");
    track.output.kind = TrackOutput::Kind::midiPort;
    track.output.port = "Synth";
    track.output.channel = 3;
    const auto id = track.id;
    edit::setAutomationPoint (song, id, 0, { 0, 0.0f });
    edit::setAutomationPoint (song, id, 0, { 1920, 127.0f }); // a fade in over two beats

    const auto snap = buildSnapshot (song);
    REQUIRE (snap.tracks.size() == 1);
    int last = -1, count = 0;
    for (const auto& e : snap.tracks[0].events)
        if (e.bytes[0] == 0xb2 && e.bytes[1] == 7)
        {
            CHECK (e.bytes[2] >= last); // only up
            if (e.at == 960)
                CHECK (e.bytes[2] == Approx (64).margin (1));
            last = e.bytes[2];
            ++count;
        }
    CHECK (last == 127);
    CHECK (count >= 17); // one every 120 ticks while it moves

    // A device that uses another controller for volume gets that one.
    const auto other = buildSnapshot (song, [] (const std::string&) { MixerControls m; m.volume = 11; return m; });
    CHECK (std::any_of (other.tracks[0].events.begin(), other.tracks[0].events.end(), [] (const SequenceEvent& e) { return e.bytes[0] == 0xb2 && e.bytes[1] == 11; }));
    CHECK (std::none_of (other.tracks[0].events.begin(), other.tracks[0].events.end(), [] (const SequenceEvent& e) { return e.bytes[0] == 0xb2 && e.bytes[1] == 7; }));
}

TEST_CASE ("Automation of an audio track changes its level as the song plays", "[automation]")
{
    Song song; // 120 BPM: one beat = 24000 samples
    song.metronome.enabled = false;
    const auto id = edit::addAudioTrack (song, "Audio").id;
    AudioClip clip;
    clip.file = "steady.wav";
    clip.length = 960 * 4;
    edit::addAudioClip (song, id, clip);
    edit::setAutomationPoint (song, id, 0, { 960, 100.0f });
    edit::setAutomationPoint (song, id, 0, { 1920, 0.0f }); // full until beat 2, silent from beat 3
    auto sample = std::make_shared<Sample>();
    sample->channels.assign (1, std::vector<float> (96000, 0.5f));
    const std::shared_ptr<const Sample> steady = sample;

    Engine e;
    e.prepare (48000.0, 512);
    e.setSnapshot (std::make_shared<const PlaybackSnapshot> (buildSnapshot (song, {}, [&] (const std::string&) { return steady; })));
    e.play();
    Renderer r (e, 512);
    r.keepAudio = true;
    r.runRandom (90000, 11);
    CHECK (r.audio[12000] == Approx (0.5).margin (0.002));           // before the line starts to fall
    CHECK (r.audio[36000] == Approx (0.5 * 0.25).margin (0.004));    // half way: volume 50 = a quarter
    CHECK (r.audio[60000] == Approx (0.0).margin (0.002));           // after the fall
    for (std::size_t i = 24100; i < 47900; ++i)
        REQUIRE (r.audio[i] <= r.audio[i - 1] + 1.0e-5f);            // smooth: no steps up on the way down
}

TEST_CASE ("Reverb and chorus sends have automation lines of their own", "[automation]")
{
    Song song;
    song.metronome.enabled = false;
    auto& track = edit::addTrack (song, "Synth");
    track.output.kind = TrackOutput::Kind::midiPort;
    track.output.port = "Synth";
    track.output.channel = 3;
    const auto id = track.id;
    edit::setAutomationPoint (song, id, 2, { 0, 0.0f });
    edit::setAutomationPoint (song, id, 2, { 1920, 127.0f }); // the reverb grows over two beats
    edit::setAutomationPoint (song, id, 3, { 0, 90.0f });
    edit::setAutomationPoint (song, id, 3, { 960, 10.0f });   // the chorus falls over one beat
    CHECK (song.tracks[0].reverbAutomation.size() == 2);
    CHECK (song.tracks[0].chorusAutomation.size() == 2);
    CHECK (song.tracks[0].volumeAutomation.empty());
    CHECK (song.tracks[0].panAutomation.empty());

    const auto snap = buildSnapshot (song);
    REQUIRE (snap.tracks.size() == 1);
    int reverb = -1, chorus = 128, reverbCount = 0, chorusCount = 0;
    for (const auto& e : snap.tracks[0].events)
    {
        if (e.bytes[0] != 0xb2)
            continue;
        if (e.bytes[1] == 91)
        {
            CHECK (e.bytes[2] >= reverb); // only up
            reverb = e.bytes[2];
            ++reverbCount;
        }
        if (e.bytes[1] == 93)
        {
            CHECK (e.bytes[2] <= chorus); // only down
            chorus = e.bytes[2];
            ++chorusCount;
        }
    }
    CHECK (reverb == 127);
    CHECK (chorus == 10);
    CHECK (reverbCount >= 17);
    CHECK (chorusCount >= 9);

    const auto loaded = project_file::fromJson (project_file::toJson (song));
    CHECK (loaded.tracks[0].reverbAutomation == song.tracks[0].reverbAutomation);
    CHECK (loaded.tracks[0].chorusAutomation == song.tracks[0].chorusAutomation);
    CHECK (loaded == song);

    edit::removeAutomationPoint (song, id, 3, 0);
    CHECK (song.tracks[0].chorusAutomation.size() == 1);
    edit::clearAutomation (song, id, 2);
    CHECK (song.tracks[0].reverbAutomation.empty());
    CHECK (song.tracks[0].chorusAutomation.size() == 1);
}

TEST_CASE ("A plugin control gets an automation line that stays with the plugin", "[automation]")
{
    Song song;
    auto& track = edit::addTrack (song, "Synth");
    track.output.kind = TrackOutput::Kind::plugin;
    track.instrument.id = "synth";
    track.instrument.name = "Synth";
    track.effects.resize (2);
    track.effects[0].id = "delay";
    track.effects[1].id = "filter";
    const auto id = track.id;

    CHECK (song.tracks[0].laneCount() == 4);
    CHECK (edit::addControlLine (song, id, 1, 7, "Cutoff") == 4);
    CHECK (edit::addControlLine (song, id, -1, 2, "Glide") == 4);  // the instrument's lines come first
    CHECK (edit::addControlLine (song, id, 1, 7, "Cutoff") == 5);  // there already: the same line
    CHECK (edit::addControlLine (song, id, 1, 9, "Resonance") == 6);
    CHECK (edit::addControlLine (song, id, 2, 0, "None") == -1);   // no such effect
    CHECK (song.tracks[0].laneCount() == 7);

    int slot = 99;
    REQUIRE (song.tracks[0].controlLine (5, &slot) != nullptr);
    CHECK (slot == 1);
    CHECK (song.tracks[0].controlLine (5)->name == "Cutoff");
    CHECK (song.tracks[0].controlLine (7) == nullptr);

    CHECK (edit::setAutomationPoint (song, id, 5, { 960, 64.0f }) == 0);
    CHECK (edit::setAutomationPoint (song, id, 5, { 0, 200.0f }) == 0);
    CHECK (edit::setAutomationPoint (song, id, 9, { 0, 1.0f }) == -1); // no such line
    CHECK (song.tracks[0].effects[1].automation[0].points.size() == 2);
    CHECK (song.tracks[0].effects[1].automation[0].points[0].value == 127.0f);
    CHECK (song.tracks[0].volumeAutomation.empty());

    const auto loaded = project_file::fromJson (project_file::toJson (song));
    CHECK (loaded == song);

    // Only lines with points play.
    const auto snap = buildSnapshot (song);
    REQUIRE (snap.controlLanes.size() == 1);
    CHECK (snap.controlLanes[0].track == id);
    CHECK (snap.controlLanes[0].slot == 1);
    CHECK (snap.controlLanes[0].control == 7);

    // The effect moves up: the line goes with it.
    std::swap (song.tracks[0].effects[0], song.tracks[0].effects[1]);
    CHECK (buildSnapshot (song).controlLanes[0].slot == 0);

    edit::removeAutomationPoint (song, id, 5, 0);
    CHECK (song.tracks[0].effects[0].automation[0].points.size() == 1);
    edit::clearAutomation (song, id, 5); // the line goes away
    CHECK (song.tracks[0].laneCount() == 6);
    CHECK (song.tracks[0].controlLine (5)->name == "Resonance");
}

namespace
{
    /** An effect with one control: the gain. */
    struct Fader final : Processor
    {
        void prepare (const ProcessSpec&) override {}
        void process (const ProcessContext&, AudioBlock& audio, const MidiEventBuffer&, MidiEventBuffer&) noexcept override
        {
            for (int c = 0; c < audio.numChannels; ++c)
                for (int i = 0; i < audio.numSamples; ++i)
                    audio.channels[c][i] *= gain;
        }
        void setControl (int control, float value) noexcept override
        {
            if (control == 3)
                gain = value;
        }
        float gain = 1.0f;
    };
}

TEST_CASE ("Automation of a plugin control moves the control as the song plays", "[automation]")
{
    Song song; // 120 BPM: one beat = 24000 samples
    song.metronome.enabled = false;
    const auto id = edit::addAudioTrack (song, "Audio").id;
    AudioClip clip;
    clip.file = "steady.wav";
    clip.length = 960 * 4;
    edit::addAudioClip (song, id, clip);
    song.tracks[0].effects.resize (2);
    song.tracks[0].effects[0].id = "missing"; // this plugin did not load
    song.tracks[0].effects[1].id = "fader";
    const int lane = edit::addControlLine (song, id, 1, 3, "Gain");
    edit::setAutomationPoint (song, id, lane, { 0, 127.0f });
    edit::setAutomationPoint (song, id, lane, { 1920, 0.0f }); // falls over two beats
    auto sample = std::make_shared<Sample>();
    sample->channels.assign (1, std::vector<float> (96000, 0.5f));
    const std::shared_ptr<const Sample> steady = sample;

    Engine e;
    e.prepare (48000.0, 512);
    e.setSnapshot (std::make_shared<const PlaybackSnapshot> (buildSnapshot (song, {}, [&] (const std::string&) { return steady; })));
    auto fader = std::make_shared<Fader>();
    auto racks = std::make_shared<RackSet>();
    RackSet::Bus bus;
    bus.track = id;
    bus.effects.push_back (fader);
    bus.effectSlots.push_back (1); // the second slot of the track
    racks->buses.push_back (std::move (bus));
    racks->prepare (512);
    e.setRacks (racks);

    // Stopped: the control stays where the user put it.
    Renderer still (e, 512);
    still.run (2048, [] { return 512; });
    CHECK (fader->gain == 1.0f);

    e.play();
    Renderer r (e, 512);
    r.keepAudio = true;
    r.run (72000, [] { return 512; });
    CHECK (r.audio[12000] == Approx (0.5 * 0.75).margin (0.01));  // a quarter of the way down
    CHECK (r.audio[36000] == Approx (0.5 * 0.25).margin (0.01));
    CHECK (r.audio[60000] == Approx (0.0).margin (0.001));        // after the fall
    CHECK (fader->gain == Approx (0.0).margin (0.001));
}

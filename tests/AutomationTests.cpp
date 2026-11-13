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

    CHECK (edit::setAutomationPoint (song, id, false, { 1920, 100.0f }) == 0);
    CHECK (edit::setAutomationPoint (song, id, false, { 960, 20.0f }) == 0);
    CHECK (edit::setAutomationPoint (song, id, false, { 3840, 200.0f }) == 2); // clamped to 127
    const auto& line = song.tracks[0].volumeAutomation;
    REQUIRE (line.size() == 3);
    CHECK (line[2].value == 127.0f);
    CHECK (automation::valueAt (line, 0.0, 0.0f) == 20.0f);      // level before the first point
    CHECK (automation::valueAt (line, 1440.0, 0.0f) == Approx (60.0f));
    CHECK (automation::valueAt (line, 9999.0, 0.0f) == 127.0f);  // level after the last point

    CHECK (edit::setAutomationPoint (song, id, false, { 960, 40.0f }) == 0); // the same time: replaced
    CHECK (song.tracks[0].volumeAutomation.size() == 3);
    edit::setAutomationPoint (song, id, true, { 0, 0.0f });

    const auto loaded = project_file::fromJson (project_file::toJson (song));
    CHECK (loaded.tracks[0].volumeAutomation == song.tracks[0].volumeAutomation);
    CHECK (loaded.tracks[0].panAutomation == song.tracks[0].panAutomation);

    edit::removeAutomationPoint (song, id, false, 1);
    CHECK (song.tracks[0].volumeAutomation.size() == 2);
    edit::clearAutomation (song, id, false);
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
    edit::setAutomationPoint (song, id, false, { 0, 0.0f });
    edit::setAutomationPoint (song, id, false, { 1920, 127.0f }); // a fade in over two beats

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
    edit::setAutomationPoint (song, id, false, { 960, 100.0f });
    edit::setAutomationPoint (song, id, false, { 1920, 0.0f }); // full until beat 2, silent from beat 3
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

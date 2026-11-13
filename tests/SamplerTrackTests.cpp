#include "TestRender.h"

#include "arranger/core/ProjectFile.h"
#include "arranger/core/SongEdit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace arranger;
using Catch::Approx;
using test::Renderer;

namespace
{
    std::shared_ptr<DrumSampler> clickSampler (float level)
    {
        auto s = std::make_shared<Sample>();
        s->sampleRate = 48000.0;
        s->channels.assign (1, std::vector<float> (64, level));
        DrumKit kit;
        DrumPad pad;
        pad.note = 36;
        pad.sample = s;
        kit.pads.push_back (pad);
        auto sampler = std::make_shared<DrumSampler> (std::move (kit));
        sampler->prepare ({ 48000.0, 512 });
        return sampler;
    }

    std::vector<std::size_t> hitStarts (const std::vector<float>& audio)
    {
        std::vector<std::size_t> starts;
        for (std::size_t i = 0; i < audio.size(); ++i)
            if (audio[i] != 0.0f && (i == 0 || audio[i - 1] == 0.0f))
                starts.push_back (i);
        return starts;
    }
}

TEST_CASE ("A looping clip repeats its pattern to the end of the clip")
{
    Song song;
    auto& t = edit::addTrack (song, "Beat");
    t.output = { TrackOutput::Kind::midiPort, "Synth", 10 };
    Clip c;
    c.length = 960 * 4;      // one bar
    c.loopLength = 960;      // a one-beat pattern
    c.notes = { { 0, 120, 36, 100 }, { 480, 960, 38, 90 } }; // the second note runs past the loop end
    edit::addClip (song, t.id, c);

    const auto snap = buildSnapshot (song);
    REQUIRE (snap.tracks.size() == 1);
    std::vector<Tick> kicks, snareOffs;
    for (const auto& e : snap.tracks[0].events)
    {
        if ((e.bytes[0] & 0xf0) == 0x90 && e.bytes[1] == 36) kicks.push_back (e.at);
        if ((e.bytes[0] & 0xf0) == 0x80 && e.bytes[1] == 38) snareOffs.push_back (e.at);
    }
    CHECK (kicks == std::vector<Tick> { 0, 960, 1920, 2880 });
    CHECK (snareOffs == std::vector<Tick> { 960, 1920, 2880, 3840 }); // cut at each loop end

    // Without a loop length the clip plays once, as before.
    Song plain = song;
    auto once = *plain.tracks[0].clips[0];
    once.loopLength = 0;
    plain.tracks[0].clips[0] = std::make_shared<const Clip> (once);
    CHECK (buildSnapshot (plain).tracks[0].events.size() == 4);
}

TEST_CASE ("Sampler tracks play their own sampler, in time, and follow mute")
{
    Song song;
    song.metronome.enabled = false;
    auto& kick = edit::addTrack (song, "Kit A");
    kick.output = { TrackOutput::Kind::sampler, "", 10 };
    const auto kickId = kick.id;
    auto& other = edit::addTrack (song, "Kit B");
    other.output = { TrackOutput::Kind::sampler, "", 10 };
    const auto otherId = other.id;

    Clip a;
    a.length = 960 * 4;
    a.loopLength = 960;
    a.notes = { { 0, 120, 36, 127 } };
    edit::addClip (song, kickId, a);
    Clip b;
    b.notes = { { 480, 120, 36, 127 } };
    edit::addClip (song, otherId, b);
    auto set = std::make_shared<SamplerSet>();
    set->entries.push_back ({ kickId, clickSampler (0.5f) });
    set->entries.push_back ({ otherId, clickSampler (0.25f) });

    Engine e;
    e.prepare (48000.0, 512);
    e.setSamplers (set);
    e.loadSong (song);
    CHECK (MidiEvent::isSamplerPort (e.samplerPort (kickId)));
    CHECK (e.samplerPort (kickId) != e.samplerPort (otherId));
    CHECK (e.samplerPort (9999) == MidiEvent::kNoPort);
    e.play();

    Renderer r (e, 512);
    r.keepAudio = true;
    r.runRandom (96000, 7); // two seconds = one bar at 120 BPM
    CHECK (r.sent.empty()); // nothing leaks to hardware ports

    const auto starts = hitStarts (r.audio);
    REQUIRE (starts.size() == 5);
    CHECK (starts[0] == 0);
    CHECK (starts[1] == 12000); // Kit B on the half beat
    CHECK (starts[2] == 24000);
    CHECK (starts[3] == 48000);
    CHECK (starts[4] == 72000);
    CHECK (r.audio[10] == Approx (0.5f));
    CHECK (r.audio[12010] == Approx (0.25f));

    song.tracks[0].mixer.mute = true;
    e.loadSong (song);
    e.stop();
    e.locate (0);
    r.runFixed (4800, 512);
    e.play();
    r.audio.clear();
    r.runFixed (48000, 512);
    for (const auto s : hitStarts (r.audio))
        CHECK (r.audio[s + 5] == Approx (0.25f)); // only Kit B
}

TEST_CASE ("The mixer strip of a sampler track sets its level")
{
    Song song;
    song.metronome.enabled = false;
    auto& t = edit::addTrack (song, "Kit");
    t.output = { TrackOutput::Kind::sampler, "", 1 };
    t.mixer.volume = 50; // a quarter of the level (square law, 100 = full)
    const auto id = t.id;
    Clip c;
    c.notes = { { 0, 120, 36, 127 } };
    edit::addClip (song, id, c);

    auto set = std::make_shared<SamplerSet>();
    set->entries.push_back ({ id, clickSampler (0.8f) });
    Engine e;
    e.prepare (48000.0, 512);
    e.setSamplers (set);
    e.loadSong (song);
    e.play();
    Renderer r (e, 512);
    r.keepAudio = true;
    r.runFixed (2048, 512);
    CHECK (r.audio[10] == Approx (0.2f));
}

TEST_CASE ("Kits and loop lengths are saved in the project file")
{
    Song song;
    auto& t = edit::addTrack (song, "Break");
    t.output = { TrackOutput::Kind::sampler, "", 10 };
    t.kit.name = "Amen";
    t.kit.pads.push_back ({ "Kick", 36, "/samples/amen.wav", 0, 11025, -3.0f, -0.25f, 2.0f, 0, true });
    t.kit.pads.push_back ({ "Hat", 42, "/samples/amen.wav", 11025, 0, 0.0f, 0.5f, 0.0f, 1, false });
    Clip c;
    c.loopLength = 1920;
    c.notes = { { 0, 240, 36, 110 } };
    edit::addClip (song, song.tracks[0].id, c);

    const auto back = project_file::fromJson (project_file::toJson (song));
    CHECK (back == song);
    REQUIRE (back.tracks[0].kit.pads.size() == 2);
    CHECK (back.tracks[0].kit.pads[1].chokeGroup == 1);
    CHECK (back.tracks[0].clips[0]->loopLength == 1920);
    CHECK (back.tracks[0].kit.padForNote (42)->name == "Hat");
    CHECK (back.tracks[0].kit.padForNote (40) == nullptr);
}

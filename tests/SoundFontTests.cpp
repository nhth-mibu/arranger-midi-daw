#include "TestRender.h"
#include "TestSoundFont.h"

#include "arranger/core/SongEdit.h"
#include "arranger/soundfont/SoundFontPlayer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace arranger;
using test::Renderer;

namespace
{
    float peakOf (const std::vector<float>& audio, std::size_t from, std::size_t to)
    {
        float p = 0.0f;
        for (auto i = from; i < std::min (to, audio.size()); ++i)
            p = std::max (p, std::abs (audio[i]));
        return p;
    }
}

TEST_CASE ("SoundFont player loads presets from an SF2")
{
    const auto path = test::writeTestSoundFont();
    SoundFontPlayer player (path.string(), 48000.0, 512);
    REQUIRE (player.presets().size() == 1);
    CHECK (player.presets()[0].name == "Test Sine");
    CHECK (player.presets()[0].bank == 0);
    CHECK (player.name() == "arranger-test");
}

TEST_CASE ("A missing or broken SoundFont is reported")
{
    CHECK_THROWS_AS (SoundFontPlayer ("/no/such/font.sf2", 48000.0, 512), SoundFontPlayer::LoadError);

    const auto bad = std::filesystem::temp_directory_path() / "arranger-broken.sf2";
    std::ofstream (bad) << "not a soundfont";
    CHECK_THROWS_AS (SoundFontPlayer (bad.string(), 48000.0, 512), SoundFontPlayer::LoadError);
}

TEST_CASE ("SoundFont tracks make sound in time, and silence when muted")
{
    const auto path = test::writeTestSoundFont();
    auto player = std::make_shared<SoundFontPlayer> (path.string(), 48000.0, 512);

    Song song;
    song.metronome.enabled = false;
    auto& t = edit::addTrack (song, "Sine");
    t.output = { TrackOutput::Kind::soundFont, "", 1 };
    t.sound.program = 0;
    t.mixer.reverbSend = 0;
    const auto id = t.id;
    Clip c;
    c.notes = { { 960, 480, 69, 127 } }; // beat 2 at 120 BPM = 0.5 s
    edit::addClip (song, id, c);

    Engine e;
    e.prepare (48000.0, 512);
    e.setInstrument (player);
    e.loadSong (song);
    e.play();

    Renderer r (e, 512);
    r.keepAudio = true;
    r.runFixed (48000, 512);

    CHECK (peakOf (r.audio, 0, 23900) < 1.0e-4f);   // silent before the note
    CHECK (peakOf (r.audio, 24000, 30000) > 0.01f); // sound right after it starts
    CHECK (player->takeChannelPeak (0) > 0.01f);
    CHECK (player->takeChannelPeak (1) == 0.0f);

    song.tracks[0].mixer.mute = true;
    e.loadSong (song);
    e.stop();
    e.locate (0);
    r.runFixed (48000, 512); // let the release ring out
    e.play();
    r.audio.clear();
    r.runFixed (48000, 512);
    CHECK (peakOf (r.audio, 0, r.audio.size()) < 1.0e-4f);
}

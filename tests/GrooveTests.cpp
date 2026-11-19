#include "TestRender.h"

#include "arranger/core/Groove.h"
#include "arranger/core/MidiFile.h"
#include "arranger/core/Sample.h"
#include "arranger/core/SongEdit.h"
#include "arranger/soundfont/SoundFontPlayer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>

using namespace arranger;
using Catch::Approx;

namespace
{
    constexpr double kBpm = 96.0;
    constexpr Tick kQ = kTicksPerQuarter;

    double msToTicks (double ms) { return ms / (secondsPerTick (kBpm) * 1000.0); }

    /**
        A drummer with a clear feel: 8th hi-hats swung 60 % and a little ahead,
        the snare 18 ms behind, the kick on the grid, hats loud on the beat and
        soft between, with small random spread.
    */
    std::vector<groove::TimedNote> drummer (int bars, unsigned seed, double swing = 60.0, double snareMs = 18.0, double hatsMs = -4.0)
    {
        std::mt19937 rng (seed);
        std::normal_distribution<double> jitter (0.0, msToTicks (3.0));
        std::normal_distribution<double> vjitter (0.0, 4.0);
        std::vector<groove::TimedNote> out;
        const auto add = [&] (double at, int pitch, double vel) {
            out.push_back ({ std::max (0.0, at + jitter (rng)), pitch, std::clamp (static_cast<int> (std::lround (vel + vjitter (rng))), 1, 127) });
        };
        for (int b = 0; b < bars; ++b)
        {
            const double bar = static_cast<double> (b * 4 * kQ);
            for (int e = 0; e < 8; ++e)
            {
                const double straight = bar + e * kQ / 2.0;
                const double at = e % 2 == 0 ? straight : bar + (e / 2) * kQ + swing / 100.0 * 2.0 * kQ / 2.0;
                add (at + msToTicks (hatsMs), 42, e % 2 == 0 ? 96.0 : 62.0);
            }
            add (bar, 36, 112.0);
            add (bar + 2 * kQ + swing / 100.0 * kQ, 36, 92.0); // the kick on the & swings too
            add (bar + 2 * kQ, 36, 100.0);
            add (bar + kQ + msToTicks (snareMs), 38, 116.0);
            add (bar + 3 * kQ + msToTicks (snareMs), 38, 120.0);
        }
        return out;
    }

    /** The same beat played by a machine: on the grid, all at velocity 100. */
    Clip stiffDrums (int bars)
    {
        Clip c;
        c.length = 4 * kQ * bars;
        for (int b = 0; b < bars; ++b)
        {
            const Tick bar = b * 4 * kQ;
            for (int e = 0; e < 8; ++e)
                c.notes.push_back ({ bar + e * kQ / 2, kQ / 4, 42, 100 });
            c.notes.push_back ({ bar, kQ / 4, 36, 100 });
            c.notes.push_back ({ bar + 2 * kQ + kQ / 2, kQ / 4, 36, 100 });
            c.notes.push_back ({ bar + 2 * kQ, kQ / 4, 36, 100 });
            c.notes.push_back ({ bar + kQ, kQ / 4, 38, 100 });
            c.notes.push_back ({ bar + 3 * kQ, kQ / 4, 38, 100 });
        }
        edit::normalise (c);
        return c;
    }

    Clip notesClip (const std::vector<groove::TimedNote>& notes, int bars)
    {
        Clip c;
        c.length = 4 * kQ * bars;
        for (const auto& n : notes)
            c.notes.push_back ({ static_cast<Tick> (std::llround (n.at)), kQ / 4, static_cast<std::uint8_t> (n.pitch), static_cast<std::uint8_t> (n.velocity) });
        edit::normalise (c);
        return c;
    }

    Song drumSong (const Clip& clip)
    {
        Song s;
        s.tempo = TempoMap (kBpm);
        s.metronome.enabled = false;
        auto& t = edit::addTrack (s, "Drums");
        t.output = { TrackOutput::Kind::soundFont, "", 10 };
        t.sound.program = 0;
        t.mixer.reverbSend = 0;
        edit::addClip (s, t.id, clip);
        return s;
    }

    std::vector<groove::TimedNote> timed (const Song& s)
    {
        return groove::notesOf (s.tracks.front());
    }

    std::string fluidR3()
    {
        for (const char* p : { "/usr/share/sounds/sf2/FluidR3_GM.sf2", "/usr/share/soundfonts/FluidR3_GM.sf2" })
            if (std::filesystem::exists (p))
                return p;
        return {};
    }

    /** Plays a song through the SoundFont player, like the app, and returns the mono audio. */
    std::vector<float> render (const Song& song, const std::string& font, double seconds, double rate = 44100.0)
    {
        auto player = std::make_shared<SoundFontPlayer> (font, rate, 512);
        Engine e;
        e.prepare (rate, 512);
        e.setInstrument (player);
        e.loadSong (song);
        e.play();
        test::Renderer r (e, 512);
        r.keepAudio = true;
        r.runFixed (static_cast<std::int64_t> (seconds * rate), 512);
        return r.audio;
    }

    void saveWav (const std::vector<float>& mono, const std::string& name, double rate = 44100.0)
    {
        const char* dir = std::getenv ("ARRANGER_GROOVE_OUT");
        if (dir == nullptr)
            return;
        Sample s;
        s.sampleRate = rate;
        s.channels = { mono, mono };
        std::filesystem::create_directories (dir);
        wav::write (s, (std::filesystem::path (dir) / name).string(), 16);
    }

    void saveMidi (const Song& s, const std::string& name)
    {
        const char* dir = std::getenv ("ARRANGER_GROOVE_OUT");
        if (dir == nullptr)
            return;
        std::filesystem::create_directories (dir);
        midi_file::save (s, std::filesystem::path (dir) / name);
    }

    void report (const std::string& what, const groove::Match& m)
    {
        std::cout << what << ": timing error " << m.timingErrorMs << " ms, accents r=" << m.accentCorrelation << ", swing diff "
                  << m.swingDifference << ", score " << m.score << " (" << m.steps << " steps)\n";
    }
}

TEST_CASE ("Groove: the grid is the coarsest one that fits the notes")
{
    TimeSignature sig;
    CHECK (groove::findGrid (drummer (4, 1), sig) == kQ / 2); // swung 8ths are 8ths, not 16ths

    std::vector<groove::TimedNote> sixteenths, triplets;
    for (int i = 0; i < 32; ++i)
        sixteenths.push_back ({ i * kQ / 4.0, 42, 90 });
    for (int i = 0; i < 24; ++i)
        triplets.push_back ({ i * kQ / 3.0, 42, 90 });
    CHECK (groove::findGrid (sixteenths, sig) == kQ / 4);
    CHECK (groove::findGrid (triplets, sig) == kQ / 3);
}

TEST_CASE ("Groove from MIDI keeps the swing, the late snare and the accents")
{
    const auto g = groove::fromNotes (drummer (8, 2), {}, { 0, 1, 0, std::numeric_limits<Tick>::max(), true });
    REQUIRE_FALSE (g.empty());
    CHECK (g.grid == kQ / 2);
    CHECK (g.steps() == 8);
    REQUIRE (g.hasLane (groove::Lane::snare));
    REQUIRE (g.hasLane (groove::Lane::hats));

    auto withTempo = g;
    withTempo.bpm = kBpm;
    const auto s = groove::summarise (withTempo);
    CHECK (s.swing == Approx (60.0).margin (2.5));
    CHECK (s.behindMs[static_cast<int> (groove::Lane::snare)] == Approx (18.0).margin (3.0));
    CHECK (s.behindMs[static_cast<int> (groove::Lane::kick)] == Approx (0.0).margin (3.0));
    CHECK (s.accents[0] > s.accents[1]); // the beat louder than the off-beat
    const auto text = groove::describe (withTempo);
    CHECK (text.find ("wing 5") != std::string::npos);
    CHECK (text.find ("snare") != std::string::npos);
}

TEST_CASE ("Groove applied to a stiff part gives it the feel of the source")
{
    const auto source = drummer (8, 3);
    const auto g = groove::fromNotes (source, {}, { 0, 1, 0, std::numeric_limits<Tick>::max(), true });

    auto song = drumSong (stiffDrums (8));
    groove::ApplyOptions o;
    o.timing = 1.0;
    o.velocity = 1.0;
    const int changed = groove::apply (song, {}, g, o);
    CHECK (changed > 60);

    const auto after = groove::fromNotes (timed (song), {}, { g.grid, 1, 0, std::numeric_limits<Tick>::max(), true });
    const auto m = groove::compare (after, g, kBpm);
    report ("MIDI to MIDI", m);
    CHECK (m.timingErrorMs < 2.0);
    CHECK (m.accentCorrelation > 0.95);
    const auto snare = groove::compare (after, g, kBpm, groove::Lane::snare);
    CHECK (snare.timingErrorMs < 2.0);

    // Half the timing amount: half the way.
    auto half = drumSong (stiffDrums (8));
    o.timing = 0.5;
    groove::apply (half, {}, g, o);
    const auto h = groove::fromNotes (timed (half), {}, { g.grid, 1, 0, std::numeric_limits<Tick>::max(), true });
    auto hs = groove::summarise (h);
    CHECK (hs.swing == Approx (55.0).margin (2.0));
}

TEST_CASE ("Groove: the kick can stay tight while the snare takes the feel")
{
    const auto g = groove::fromNotes (drummer (4, 4, 50.0, 25.0, 0.0), {}, { 0, 1, 0, std::numeric_limits<Tick>::max(), true });
    auto song = drumSong (stiffDrums (4));
    groove::ApplyOptions o;
    o.laneAmount[static_cast<int> (groove::Lane::snare)] = 0.0;
    groove::apply (song, {}, g, o);
    for (const auto& n : song.tracks.front().clips.front()->notes)
        if (n.pitch == 38)
            CHECK (n.start % kQ == 0);
}

TEST_CASE ("Groove: notes never change order, and a melodic part uses the main lane")
{
    groove::Feel f;
    f.swing = 70.0;
    f.behindMs = 20.0;
    const auto g = groove::fromFeel (f);
    Clip c;
    c.length = 4 * kQ;
    for (int i = 0; i < 16; ++i)
        c.notes.push_back ({ i * kQ / 4, kQ / 4, static_cast<std::uint8_t> (60 + i), 100 });
    groove::apply (c, g, {});
    for (std::size_t i = 1; i < c.notes.size(); ++i)
        CHECK (c.notes[i].pitch > c.notes[i - 1].pitch); // still in order: sorted by start
}

TEST_CASE ("Groove from words")
{
    const auto f = groove::feelFromWords ("lazy swing, behind the beat", 100.0);
    CHECK (f.swing > 58.0);
    CHECK (f.behindMs > 8.0);
    const auto g = groove::fromFeel (f);
    const auto s = groove::summarise (g);
    CHECK (s.swing == Approx (f.swing).margin (0.5));
    CHECK (s.behindMs[0] == Approx (f.behindMs).margin (0.5));

    const auto pushed = groove::feelFromWords ("pushed, ahead of the beat, 8 ms", 100.0);
    CHECK (pushed.behindMs == Approx (-8.0));
    const auto backbeat = groove::feelFromWords ("straight 16ths with a backbeat", 100.0);
    CHECK (backbeat.grid == kQ / 4);
    CHECK (backbeat.swing == 50.0);
    CHECK (backbeat.accents[4] > backbeat.accents[0]); // beat 2 louder than beat 1
    const auto lane = groove::feelFromWords ("tight kick, lazy snare", 100.0);
    CHECK (lane.snareMs > 5.0);
    CHECK (groove::feelFromWords ("swing 58%").swing == Approx (58.0));
    CHECK (groove::builtIns().size() >= 8);
}

TEST_CASE ("Groove file round trip")
{
    auto g = groove::fromNotes (drummer (4, 5), {}, { 0, 2, 0, std::numeric_limits<Tick>::max(), true });
    g.name = "Test";
    const auto back = groove::fromJson (groove::toJson (g));
    CHECK (back.name == "Test");
    CHECK (back.steps() == g.steps());
    CHECK (back.lane (groove::Lane::snare).size() == g.lane (groove::Lane::snare).size());
    CHECK (back.lane (groove::Lane::all)[3].offset == Approx (g.lane (groove::Lane::all)[3].offset).margin (0.01));
    CHECK_THROWS (groove::fromJson ("{}"));
}

/**
    The listening test: play the source drummer and the stiff beat with the
    groove on it through FluidR3 GM (as the app does), find the hits in both
    recordings, and compare the feel. Also take the groove from the recording
    alone (the audio source), put it on the stiff beat, and check that the
    result has the drummer's feel. Set ARRANGER_GROOVE_OUT to a folder to keep
    the WAV and MIDI files to listen to.
*/
TEST_CASE ("Groove listening test with FluidR3 GM", "[listen]")
{
    const auto font = fluidR3();
    if (font.empty())
    {
        WARN ("FluidR3_GM.sf2 is not installed: skipped");
        return;
    }
    constexpr int bars = 8;
    const double seconds = bars * 4 * 60.0 / kBpm + 1.0;
    const auto sourceNotes = drummer (bars, 7);
    const auto sourceSong = drumSong (notesClip (sourceNotes, bars));
    const auto sourceAudio = render (sourceSong, font, seconds);
    const auto truth = groove::fromNotes (sourceNotes, {}, { 0, 1, 0, std::numeric_limits<Tick>::max(), true });

    // The audio source: hits from the recording, the tempo known (the user's song tempo).
    const auto hits = groove::findHits (sourceAudio, 44100.0);
    CHECK (hits.size() >= bars * 10);
    const auto fromAudio = groove::fromHits (hits, {}, kBpm, { truth.grid, 1, 0, std::numeric_limits<Tick>::max(), true });
    REQUIRE_FALSE (fromAudio.empty());
    std::cout << "source (MIDI):  " << groove::describe ([&] { auto t = truth; t.bpm = kBpm; return t; }()) << "\n";
    std::cout << "source (audio): " << groove::describe (fromAudio) << "\n";
    const auto audioVsTruth = groove::compare (fromAudio, truth, kBpm);
    report ("groove from audio vs the drummer's notes", audioVsTruth);
    CHECK (audioVsTruth.timingErrorMs < 3.0);
    CHECK (audioVsTruth.accentCorrelation > 0.9);
    for (auto l : { groove::Lane::kick, groove::Lane::snare, groove::Lane::hats })
        CHECK (groove::compare (fromAudio, truth, kBpm, l).timingErrorMs < 3.0);

    const auto stiff = drumSong (stiffDrums (bars));
    const auto stiffAudio = render (stiff, font, seconds);
    for (const auto& [name, g] : { std::pair<std::string, groove::Groove> { "midi", truth }, { "audio", fromAudio } })
    {
        auto grooved = stiff;
        groove::ApplyOptions o;
        o.timing = 1.0;
        o.velocity = 1.0;
        groove::apply (grooved, {}, g, o);
        const auto audio = render (grooved, font, seconds);

        // Compare the recordings the same way: hits found in each.
        const auto heardSource = groove::fromHits (groove::findHits (sourceAudio, 44100.0), {}, kBpm, { truth.grid, 1, 0, std::numeric_limits<Tick>::max(), true });
        const auto heardGrooved = groove::fromHits (groove::findHits (audio, 44100.0), {}, kBpm, { truth.grid, 1, 0, std::numeric_limits<Tick>::max(), true });
        const auto heardStiff = groove::fromHits (groove::findHits (stiffAudio, 44100.0), {}, kBpm, { truth.grid, 1, 0, std::numeric_limits<Tick>::max(), true });
        const auto before = groove::compare (heardStiff, heardSource, kBpm);
        const auto after = groove::compare (heardGrooved, heardSource, kBpm);
        report ("heard: stiff vs source (" + name + " groove)", before);
        report ("heard: grooved vs source (" + name + " groove)", after);
        const auto notesAfter = groove::compare (groove::fromNotes (timed (grooved), {}, { truth.grid, 1, 0, std::numeric_limits<Tick>::max(), true }), truth, kBpm);
        report ("notes: grooved vs the drummer (" + name + " groove)", notesAfter);
        CHECK (after.score > before.score + 15.0);
        CHECK (after.timingErrorMs < 2.0);
        CHECK (notesAfter.timingErrorMs < 2.0);
        CHECK (notesAfter.accentCorrelation > 0.85);
        saveWav (audio, "grooved-from-" + name + ".wav");
        saveMidi (grooved, "grooved-from-" + name + ".mid");
    }
    saveWav (sourceAudio, "source-drummer.wav");
    saveWav (stiffAudio, "stiff.wav");
    saveMidi (sourceSong, "source-drummer.mid");
    saveMidi (stiff, "stiff.mid");
}


/**
    The chat source: "lazy swing, behind the beat" put on a stiff band (drums,
    bass, piano), played through FluidR3 GM, and the feel heard in the result
    against the song's own bar lines.
*/
TEST_CASE ("Groove from words, heard in the result", "[listen]")
{
    const auto font = fluidR3();
    if (font.empty())
    {
        WARN ("FluidR3_GM.sf2 is not installed: skipped");
        return;
    }
    constexpr int bars = 8;
    const double seconds = bars * 4 * 60.0 / kBpm + 1.0;
    auto song = drumSong (stiffDrums (bars));
    {
        auto& bass = edit::addTrack (song, "Bass");
        bass.output = { TrackOutput::Kind::soundFont, "", 2 };
        bass.sound.program = 33;
        bass.mixer.reverbSend = 0;
        Clip c;
        c.length = 4 * kQ * bars;
        const int roots[] = { 40, 45, 43, 38 };
        for (int b = 0; b < bars; ++b)
            for (int e = 0; e < 8; ++e)
                c.notes.push_back ({ b * 4 * kQ + e * kQ / 2, kQ / 2 - 40, static_cast<std::uint8_t> (roots[b % 4] + (e % 4 == 3 ? 7 : 0)), 100 });
        edit::addClip (song, bass.id, c);
    }
    const auto stiff = song;
    const auto feel = groove::feelFromWords ("lazy swing, behind the beat", kBpm);
    const auto g = groove::fromFeel (feel);
    groove::ApplyOptions o;
    o.timing = 1.0;
    o.velocity = 1.0;
    groove::apply (song, {}, g, o);

    // Hear each part alone against the song's bar lines (the song starts at 0 s).
    const groove::AudioGrid known { kBpm, 0.0, 1.0 };
    for (std::size_t part = 0; part < 2; ++part)
    {
        auto solo = song;
        solo.tracks[1 - part].mixer.mute = true;
        auto soloStiff = stiff;
        soloStiff.tracks[1 - part].mixer.mute = true;
        const auto audio = render (solo, font, seconds);
        const auto heard = groove::fromHits (groove::findHits (audio, 44100.0), {}, known, { g.grid, 1, 0, std::numeric_limits<Tick>::max(), part == 0 });
        const auto heardStiff = groove::fromHits (groove::findHits (render (soloStiff, font, seconds), 44100.0), {}, known, { g.grid, 1, 0, std::numeric_limits<Tick>::max(), part == 0 });
        auto h = groove::summarise (heard);
        auto hs = groove::summarise (heardStiff);
        // The SoundFont's own attack delay is in both: take it off.
        const double latency = hs.behindMs[0];
        std::cout << (part == 0 ? "drums" : "bass") << " heard: swing " << h.swing << "%, behind " << h.behindMs[0] - latency << " ms (wanted "
                  << feel.swing << "%, " << feel.behindMs << " ms); stiff: swing " << hs.swing << "%, latency " << latency << " ms\n";
        CHECK (h.swing == Approx (feel.swing).margin (2.0));
        CHECK (h.behindMs[0] - latency == Approx (feel.behindMs).margin (3.0));
        CHECK (hs.swing == Approx (50.0).margin (2.0));
        saveWav (audio, std::string ("words-lazy-swing-") + (part == 0 ? "drums" : "bass") + ".wav");
    }
    saveWav (render (song, font, seconds), "words-lazy-swing.wav");
    saveWav (render (stiff, font, seconds), "words-stiff.wav");
    saveMidi (song, "words-lazy-swing.mid");
}

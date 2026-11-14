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
        racks->buses.push_back ({ ids[0], nullptr, { std::make_shared<Gain> (0.5f), std::make_shared<Gain> (0.5f) }, {}, {}, {}, {} });
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
        racks->buses.push_back ({ ids[0], nullptr, { std::make_shared<Gain> (0.0f) }, {}, {}, {}, {} });
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
    track.instrument = { "Internal", "tone", "Tone", "", false, {} };
    track.effects.push_back ({ "Internal", "gain", "Gain", "AAEC", true, {} });
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
        racks->buses.push_back ({ id, std::make_shared<Tone>(), { std::make_shared<Gain> (0.5f) }, {}, {}, {}, {} });
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
    song.master.effects.push_back ({ "VST3", "abc", "Limiter", "", false, {} });
    const auto loaded = project_file::fromJson (project_file::toJson (song));
    CHECK (loaded.tracks[0].output.kind == TrackOutput::Kind::plugin);
    CHECK (loaded.tracks[0].instrument == song.tracks[0].instrument);
    CHECK (loaded.tracks[0].effects == song.tracks[0].effects);
    CHECK (loaded.master.effects == song.master.effects);
    CHECK (loaded == song);
}

namespace
{
    /** An effect that gives its sound `samples` late and says so. */
    struct Late final : Processor
    {
        explicit Late (int samples) { line.setLength (samples); }
        void prepare (const ProcessSpec&) override {}
        void process (const ProcessContext&, AudioBlock& audio, const MidiEventBuffer&, MidiEventBuffer&) noexcept override
        {
            line.process (audio.channels[0], audio.numChannels > 1 ? audio.channels[1] : nullptr, audio.numSamples);
        }
        int latencySamples() const noexcept override { return static_cast<int> (line.left.size()); }
        DelayLine line;
    };
}

TEST_CASE ("Delay compensation keeps tracks together when a plugin is late", "[racks]")
{
    // Three audio tracks play the same click at beat 2 (sample 24000).
    Song song;
    song.metronome.enabled = false;
    TrackId ids[3] = {};
    for (auto& id : ids)
    {
        AudioClip clip;
        clip.file = "click.wav";
        clip.start = 960;
        clip.length = 960;
        id = edit::addAudioTrack (song, "Audio").id;
        edit::addAudioClip (song, id, clip);
    }
    auto click = std::make_shared<Sample>();
    click->channels.assign (1, std::vector<float> (4800, 0.0f));
    click->channels[0][1000] = 1.0f; // past the fade at the clip start
    const std::shared_ptr<const Sample> sample = click;

    Engine e;
    e.prepare (48000.0, 512);
    e.setSnapshot (std::make_shared<const PlaybackSnapshot> (buildSnapshot (song, {}, [&] (const std::string&) { return sample; })));
    auto racks = std::make_shared<RackSet>();
    racks->buses.push_back ({ ids[0], nullptr, { std::make_shared<Late> (300), std::make_shared<Late> (200) }, {}, {}, {}, {} }); // 500 late
    racks->buses.push_back ({ ids[1], nullptr, { std::make_shared<Late> (100) }, {}, {}, {}, {} });                               // 100 late
    racks->master.push_back (std::make_shared<Late> (50));                                                                     // the third track has no bus
    racks->prepare (512);
    CHECK (racks->busLatency == 500);
    CHECK (racks->totalLatency() == 550);
    e.setRacks (racks);

    const auto out = play (e, 48000);
    // All three clicks arrive together, 550 samples late; nothing at the place without compensation.
    const std::size_t at = 24000 + 1000 + 550;
    CHECK (out[at] == Approx (3.0).margin (0.01));
    float other = 0.0f;
    for (std::size_t i = 24000; i < 30000; ++i)
        if (i != at)
            other = std::max (other, std::abs (out[i]));
    CHECK (other < 0.01f);
}

TEST_CASE ("A track on the built-in instrument can have its own effects", "[racks]")
{
    /** Stands in for the SoundFont player: channel 1 plays 0.5 and channel 2 plays 0.25, all the time. */
    struct TwoChannels final : Processor
    {
        void prepare (const ProcessSpec&) override {}
        void process (const ProcessContext&, AudioBlock& audio, const MidiEventBuffer&, MidiEventBuffer&) noexcept override
        {
            const float levels[] = { 0.5f, 0.25f };
            for (std::size_t ch = 0; ch < 2; ++ch)
            {
                float* l = routed != nullptr && routed->left[ch] != nullptr ? routed->left[ch] : audio.channels[0];
                float* r = routed != nullptr && routed->right[ch] != nullptr ? routed->right[ch] : audio.channels[1];
                for (int i = 0; i < audio.numSamples; ++i)
                {
                    l[i] += levels[ch];
                    r[i] += levels[ch];
                }
            }
        }
        void setChannelOutputs (const ChannelOutputs* o) noexcept override { routed = o; }
        const ChannelOutputs* routed = nullptr;
    };

    Song song;
    song.metronome.enabled = false;
    auto& one = edit::addTrack (song, "One");
    one.output.kind = TrackOutput::Kind::soundFont;
    one.output.channel = 1;
    const auto first = one.id;
    auto& two = edit::addTrack (song, "Two");
    two.output.kind = TrackOutput::Kind::soundFont;
    two.output.channel = 2;
    const auto second = two.id;

    Engine e;
    e.prepare (48000.0, 512);
    e.setSnapshot (std::make_shared<const PlaybackSnapshot> (buildSnapshot (song)));
    e.setInstrument (std::make_shared<TwoChannels>());
    CHECK (play (e, 4800)[2400] == Approx (0.75).margin (0.001));

    // Silence the second track's channel with an effect: only the first is left.
    auto racks = std::make_shared<RackSet>();
    racks->buses.push_back ({ second, nullptr, { std::make_shared<Gain> (0.0f) }, {}, {}, {}, {} });
    racks->prepare (512);
    e.setRacks (racks);
    Renderer r (e, 512);
    r.keepAudio = true;
    r.runRandom (4800, 5);
    CHECK (r.audio[2400] == Approx (0.5).margin (0.001));
    (void) first;
}

TEST_CASE ("Buses: a track goes to a bus, sends go to buses, and removing a bus frees its tracks", "[buses]")
{
    Song song;
    const auto audio = edit::addAudioTrack (song, "Audio").id;
    auto& midi = edit::addTrack (song, "Synth");
    midi.output.kind = TrackOutput::Kind::midiPort;
    const auto port = midi.id;
    const auto drums = edit::addBus (song, "Drums").id;
    const auto verb = edit::addBus (song, "Reverb").id;
    CHECK (drums != verb);
    CHECK (drums != audio);
    CHECK (song.buses[0].colour != song.buses[1].colour);

    edit::setTrackBus (song, audio, drums);
    edit::setTrackBus (song, port, drums);  // a MIDI port track makes no sound in the app
    edit::setTrackBus (song, audio, 999);   // no such bus: no change
    CHECK (song.tracks[0].bus == drums);
    CHECK (song.tracks[1].bus == 0);

    edit::setSend (song, audio, verb, 90);
    edit::setSend (song, audio, verb, 80); // the same send again
    edit::setSend (song, port, verb, 90);
    CHECK (song.tracks[0].sends.size() == 1);
    CHECK (edit::sendLevel (song.tracks[0], verb) == 80);
    CHECK (song.tracks[1].sends.empty());

    const auto loaded = project_file::fromJson (project_file::toJson (song));
    CHECK (loaded == song);
    CHECK (loaded.buses == song.buses);

    edit::removeBus (song, drums);
    CHECK (song.tracks[0].bus == 0);
    CHECK (song.buses.size() == 1);
    edit::setSend (song, audio, verb, 0); // level 0 removes the send
    CHECK (song.tracks[0].sends.empty());
}

TEST_CASE ("Buses mix their tracks, run their effects and have their own fader", "[buses]")
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
    const auto group = edit::addBus (song, "Group").id;
    const auto returns = edit::addBus (song, "Return").id;
    edit::setTrackBus (song, ids[0], group);
    edit::setSend (song, ids[1], returns, 50); // (50 / 100)^2 = a quarter
    const auto sample = steady();

    const auto render = [&] (const Song& s, int lateGroup = 0) {
        Engine e;
        e.prepare (48000.0, 512);
        e.setSnapshot (std::make_shared<const PlaybackSnapshot> (buildSnapshot (s, {}, [&] (const std::string&) { return sample; })));
        auto racks = std::make_shared<RackSet>();
        for (const auto id : ids)
        {
            RackSet::Bus b;
            b.track = id;
            racks->buses.push_back (std::move (b));
        }
        RackSet::MixBus g;
        g.id = group;
        g.effects.push_back (std::make_shared<Gain> (0.5f));
        if (lateGroup > 0)
            g.effects.push_back (std::make_shared<Late> (lateGroup));
        racks->mixBuses.push_back (std::move (g));
        RackSet::MixBus r;
        r.id = returns;
        r.effects.push_back (std::make_shared<Gain> (2.0f));
        racks->mixBuses.push_back (std::move (r));
        racks->prepare (512);
        e.setRacks (racks);
        return play (e, 24000);
    };

    // Track 1 through the group (halved); track 2 to the master and a quarter of it through the return (doubled).
    CHECK (render (song)[12000] == Approx (0.25 + 0.5 + 0.25).margin (0.001));

    auto quiet = song;
    quiet.buses[0].volume = 50; // the group fader: a quarter
    CHECK (render (quiet)[12000] == Approx (0.0625 + 0.5 + 0.25).margin (0.001));

    auto muted = song;
    muted.buses[1].mute = true;
    CHECK (render (muted)[12000] == Approx (0.25 + 0.5).margin (0.001));

    // The send is after the track fader.
    auto lower = song;
    lower.tracks[1].mixer.volume = 50;
    CHECK (render (lower)[12000] == Approx (0.25 + 0.125 + 0.0625).margin (0.001));

    // A late effect on a bus: everything else waits for it, so the start of the sound is the same for all.
    const auto late = render (song, 300);
    CHECK (late[200] == Approx (0.0).margin (0.001));
    CHECK (late[12000] == Approx (1.0).margin (0.001));
}

TEST_CASE ("Overlapping audio clips: the take on top plays, with crossfades at its edges", "[takes]")
{
    Song song;
    const auto id = edit::addAudioTrack (song, "Vocal").id;
    AudioClip a;
    a.name = "Take 1";
    a.file = "one.wav";
    a.start = 0;
    a.length = 960 * 8;
    edit::addAudioClip (song, id, a);
    AudioClip b = a;
    b.id = 0;
    b.name = "Take 2";
    b.start = 960 * 2;
    b.length = 960 * 2;
    b.layer = edit::layerAbove (song.tracks[0], b.start, b.end());
    CHECK (b.layer == 1);
    const auto takeTwo = edit::addAudioClip (song, id, b);

    // Take 1 plays around take 2; they cross over the short default crossfade.
    auto pieces = edit::audioPieces (song.tracks[0]);
    REQUIRE (pieces.size() == 3);
    CHECK (pieces[0].clip->name == "Take 1");
    CHECK (pieces[0].end == 960 * 2 + edit::kAudioCrossfade);
    CHECK (pieces[0].fadeOut == edit::kAudioCrossfade);
    CHECK (pieces[1].clip->name == "Take 2");
    CHECK (pieces[1].fadeIn == edit::kAudioCrossfade);
    CHECK (pieces[1].fadeOut == edit::kAudioCrossfade);
    CHECK (pieces[2].start == 960 * 4 - edit::kAudioCrossfade);
    CHECK (pieces[2].fadeIn == edit::kAudioCrossfade);

    // A longer fade in on take 2 is a longer crossfade.
    edit::modifyAudioClip (song, takeTwo, [] (AudioClip& c) { c.fadeIn = 240; });
    pieces = edit::audioPieces (song.tracks[0]);
    CHECK (pieces[0].end == 960 * 2 + 240);
    CHECK (pieces[0].fadeOut == 240);

    // Choose take 1: it covers take 2, which is now silent.
    CHECK (edit::audioClipsAt (song.tracks[0], 960 * 3).front()->name == "Take 2");
    edit::bringToFront (song, song.tracks[0].audio[0]->id);
    CHECK (edit::audioClipsAt (song.tracks[0], 960 * 3).front()->name == "Take 1");
    pieces = edit::audioPieces (song.tracks[0]);
    REQUIRE (pieces.size() == 1);
    CHECK (pieces[0].clip->name == "Take 1");

    // Clips that only touch both play whole; the layer is saved.
    const auto loaded = project_file::fromJson (project_file::toJson (song));
    CHECK (loaded == song);
}

TEST_CASE ("A take on top replaces the sound below it", "[takes]")
{
    Song song; // 120 BPM: one beat = 24000 samples
    song.metronome.enabled = false;
    const auto id = edit::addAudioTrack (song, "Vocal").id;
    AudioClip low;
    low.file = "low.wav";
    low.length = 960 * 4;
    edit::addAudioClip (song, id, low);
    AudioClip high = low;
    high.file = "high.wav";
    high.start = 960;
    high.length = 960;
    high.layer = 1;
    edit::addAudioClip (song, id, high);
    const auto make = [] (float v) {
        auto s = std::make_shared<Sample>();
        s->channels.assign (1, std::vector<float> (96000, v));
        return std::shared_ptr<const Sample> (s);
    };
    const auto quiet = make (0.25f), loud = make (0.75f);

    Engine e;
    e.prepare (48000.0, 512);
    e.setSnapshot (std::make_shared<const PlaybackSnapshot> (buildSnapshot (song, {}, [&] (const std::string& f) { return f == "high.wav" ? loud : quiet; })));
    const auto out = play (e, 72000);
    CHECK (out[12000] == Approx (0.25).margin (0.001)); // beat 1: the lower take
    CHECK (out[36000] == Approx (0.75).margin (0.001)); // beat 2: only the take on top
    CHECK (out[60000] == Approx (0.25).margin (0.001)); // beat 3: the lower take again
    for (std::size_t i = 23000; i < 26000; ++i)          // the crossfade: no jump
        REQUIRE (std::abs (out[i] - out[i - 1]) < 0.01f);
}

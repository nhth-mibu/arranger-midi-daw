// The engine under load: an audio thread renders without a break while the UI
// thread plays, stops, jumps, loops, launches clips and changes the song. Run
// these under ThreadSanitizer and AddressSanitizer (sanitizers.yml): a data race,
// a freed snapshot still in use or a lost note shows there.

#include "KitchenSink.h"
#include "TestRender.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <map>
#include <tuple>
#include <random>
#include <thread>

using namespace arranger;

namespace
{
    /** A song whose every track plays to a MIDI port, so the engine sends what it plays. */
    Song portSong (int tracks, int notesPerTrack, unsigned seed)
    {
        std::mt19937 rng (seed);
        Song s;
        s.loop = { true, 0, 3840 * 4 };
        for (int t = 0; t < tracks; ++t)
        {
            auto& track = edit::addTrack (s, "T" + std::to_string (t));
            track.output = { TrackOutput::Kind::midiPort, "Out", 1 + t % 16 };
            Clip c;
            c.length = 3840 * 4;
            for (int n = 0; n < notesPerTrack; ++n)
                c.notes.push_back ({ static_cast<Tick> (rng() % (3840 * 4 - 10)), 1 + static_cast<Tick> (rng() % 960),
                                     static_cast<std::uint8_t> (rng() % 128), static_cast<std::uint8_t> (1 + rng() % 127) });
            edit::normalise (c);
            const auto id = track.id;
            edit::addClip (s, id, c);
            edit::setSlot (s, id, 0, c);
            edit::setSlot (s, id, 1, c);
        }
        return s;
    }

    /** Runs the audio thread until stopped, in random block sizes. */
    struct AudioThread
    {
        Engine& engine;
        std::atomic<bool> running { true };
        std::atomic<std::int64_t> blocks { 0 }, noteOns { 0 }, noteOffs { 0 };
        std::thread thread;

        explicit AudioThread (Engine& e) : engine (e)
        {
            thread = std::thread ([this] {
                std::mt19937 rng (5);
                std::vector<float> l (512), r (512);
                MidiEventBuffer in (16), out (4096);
                while (running.load())
                {
                    const int n = 1 + static_cast<int> (rng() % 512);
                    float* chans[] = { l.data(), r.data() };
                    AudioBlock block { chans, 2, n };
                    out.clear();
                    engine.process (block, in, out);
                    for (const auto& ev : out)
                    {
                        if (ev.isNoteOn())
                            ++noteOns;
                        else if ((ev.bytes[0] & 0xF0) == 0x80 || ((ev.bytes[0] & 0xF0) == 0x90 && ev.bytes[2] == 0))
                            ++noteOffs;
                    }
                    ++blocks;
                }
            });
        }

        void stop()
        {
            running = false;
            thread.join();
        }

        ~AudioThread()
        {
            if (thread.joinable())
                stop();
        }
    };
}

TEST_CASE ("The engine survives thousands of transport and song changes while it plays", "[engine][stress]")
{
    Engine e;
    e.prepare (48000.0, 512);
    e.loadSong (portSong (8, 64, 1));
    AudioThread audio (e);

    std::mt19937 rng (42);
    const auto songA = portSong (8, 64, 2);
    const auto songB = portSong (32, 16, 3);
    const auto sink = test::kitchenSinkSong();
    for (int i = 0; i < 10'000; ++i)
    {
        switch (rng() % 10)
        {
            case 0: e.play(); break;
            case 1: e.stop(); break;
            case 2: e.togglePlay(); break;
            case 3: e.locate (static_cast<Tick> (rng() % (3840 * 8))); break;
            case 4: e.loadSong (rng() % 3 == 0 ? songA : (rng() % 2 == 0 ? songB : sink)); break;
            case 5: e.launchScene (static_cast<int> (rng() % 3), rng() % 2 == 0 ? 0 : 960); break;
            case 6: e.stopAllSlots (rng() % 2 == 0 ? 0 : 960); break;
            case 7: e.setRecording (rng() % 2 == 0); break;
            case 8: e.collectGarbage(); break;
            case 9: std::this_thread::yield(); break;
        }
        while (e.popSlotChange()) {}
        while (e.popRecorded()) {}
    }

    // Let the audio thread take the last commands, then stop it.
    const auto seen = audio.blocks.load();
    while (audio.blocks.load() < seen + 20)
        std::this_thread::yield();
    audio.stop();
    e.collectGarbage();
    CHECK (audio.blocks.load() > 20);
}

TEST_CASE ("Every note the engine starts, it also ends", "[engine][stress]")
{
    Engine e;
    e.prepare (48000.0, 512);
    e.loadSong (portSong (16, 128, 7));
    test::Renderer r (e, 512);
    std::mt19937 rng (11);

    // Play with jumps, stops, loops and song changes between blocks.
    for (int i = 0; i < 400; ++i)
    {
        switch (rng() % 6)
        {
            case 0: e.play(); break;
            case 1: e.stop(); break;
            case 2: e.locate (static_cast<Tick> (rng() % (3840 * 4))); break;
            case 3: e.loadSong (portSong (16, 128, static_cast<unsigned> (rng()))); break;
            case 4: e.launchScene (static_cast<int> (rng() % 2), 0); break;
            default: break;
        }
        r.runRandom (4800, static_cast<unsigned> (i));
    }
    e.stop();
    e.stopAllSlots (0);
    r.runFixed (48000, 512);

    // Count what is still sounding per port, channel and pitch.
    std::map<std::tuple<int, int, int>, int> sounding;
    for (const auto& s : r.sent)
    {
        const int kind = s.event.bytes[0] & 0xF0;
        const auto key = std::make_tuple (int (s.event.port), s.event.bytes[0] & 0x0F, int (s.event.bytes[1]));
        if (kind == 0x90 && s.event.bytes[2] > 0)
            ++sounding[key];
        else if (kind == 0x80 || kind == 0x90)
            sounding[key] = 0; // a note-off ends every start of that key on hardware
        else if (kind == 0xB0 && (s.event.bytes[1] == 123 || s.event.bytes[1] == 120))
            for (auto& [k, n] : sounding)
                if (std::get<0> (k) == s.event.port && std::get<1> (k) == (s.event.bytes[0] & 0x0F))
                    n = 0; // all notes off / all sound off
    }
    int stuck = 0;
    for (const auto& [k, n] : sounding)
        stuck += n > 0 ? 1 : 0;
    CHECK (stuck == 0);
}

TEST_CASE ("The audio device can stop and start again many times", "[engine][stress]")
{
    Engine e;
    const auto song = portSong (4, 32, 9);
    std::mt19937 rng (3);
    for (int i = 0; i < 200; ++i)
    {
        const double rates[] = { 44100.0, 48000.0, 96000.0, 22050.0 };
        const int sizes[] = { 16, 64, 256, 1024, 4096 };
        const int size = sizes[rng() % 5];
        e.prepare (rates[rng() % 4], size);
        if (i % 3 == 0)
            e.loadSong (song);
        e.play();
        test::Renderer r (e, size);
        r.runRandom (size * 4, static_cast<unsigned> (i));
        e.released();
        e.collectGarbage();
    }
    CHECK (e.isPlaying());
}

TEST_CASE ("Extreme songs play without trouble", "[engine][edge]")
{
    SECTION ("an empty song")
    {
        Engine e;
        e.prepare (48000.0, 512);
        e.loadSong ({});
        e.play();
        test::Renderer r (e, 512);
        r.runRandom (48000 * 4, 1);
        CHECK (e.positionTicks() > 0.0);
    }
    SECTION ("1,000 tracks")
    {
        Engine e;
        e.prepare (48000.0, 512);
        e.loadSong (portSong (1000, 4, 2));
        e.play();
        test::Renderer r (e, 512);
        r.runRandom (48000, 2);
        CHECK_FALSE (r.sent.empty());
    }
    SECTION ("100,000 notes in one clip")
    {
        Engine e;
        e.prepare (48000.0, 512);
        e.loadSong (portSong (1, 100'000, 3));
        e.play();
        test::Renderer r (e, 512);
        r.runRandom (48000 * 2, 3);
        CHECK_FALSE (r.sent.empty());
    }
    SECTION ("one-tick notes and very slow and very fast tempos")
    {
        for (const double bpm : { 20.0, 400.0, 999.0, 1.0 })
        {
            Song s = portSong (2, 50, 4);
            edit::forEachClip (s, [] (Clip& c) {
                for (auto& n : c.notes)
                    n.length = 1;
            });
            s.tempo.setPoints ({ { 0, bpm } });
            Engine e;
            e.prepare (48000.0, 512);
            e.loadSong (s);
            e.play();
            test::Renderer r (e, 512);
            r.runRandom (48000 * 3, 4);
            CHECK (e.positionTicks() > 0.0);
        }
    }
    SECTION ("odd time signatures and a loop of one tick")
    {
        Song s = portSong (2, 50, 5);
        s.timeSignature = { 13, 8 };
        s.loop = { true, 100, 101 };
        Engine e;
        e.prepare (48000.0, 64);
        e.loadSong (s);
        e.play();
        test::Renderer r (e, 64);
        r.runRandom (48000, 5);
        CHECK (e.positionTicks() >= 100.0);
        CHECK (e.positionTicks() <= 101.0);
    }
}

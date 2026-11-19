// The audio thread must never allocate: an allocation can take a lock and wait,
// and a late audio block is a click. This test program replaces operator new
// to count allocations made inside Engine::process(). It is its own program, so
// the replacement does not touch the other tests, and it is left out of
// sanitizer builds, which replace operator new themselves.

#include "KitchenSink.h"
#include "TestRender.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <new>
#include <random>

namespace
{
    thread_local bool inAudio = false;
    std::atomic<long> audioAllocations { 0 };

    void* allocate (std::size_t n)
    {
        if (inAudio)
            ++audioAllocations;
        if (void* p = std::malloc (n == 0 ? 1 : n))
            return p;
        throw std::bad_alloc();
    }
}

void* operator new (std::size_t n) { return allocate (n); }
void* operator new[] (std::size_t n) { return allocate (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

using namespace arranger;

namespace
{
    Song portSong (unsigned seed)
    {
        std::mt19937 rng (seed);
        Song s;
        s.loop = { true, 0, 3840 * 2 };
        for (int t = 0; t < 8; ++t)
        {
            auto& track = edit::addTrack (s, "T" + std::to_string (t));
            track.output = { TrackOutput::Kind::midiPort, "Out", 1 + t };
            Clip c;
            c.length = 3840 * 2;
            for (int n = 0; n < 200; ++n)
                c.notes.push_back ({ static_cast<Tick> (rng() % 7000), 1 + static_cast<Tick> (rng() % 480), static_cast<std::uint8_t> (rng() % 128), 100 });
            c.controls = { { 0, ControlEvent::Kind::controller, 7, 100 }, { 100, ControlEvent::Kind::pitchBend, 0, 500 } };
            edit::normalise (c);
            const auto id = track.id;
            edit::addClip (s, id, c);
            edit::setSlot (s, id, 0, c);
            edit::setAutomationPoint (s, id, 0, { 0, 10.0f });
            edit::setAutomationPoint (s, id, 0, { 3840, 120.0f });
        }
        return s;
    }

    /** Renders like the audio thread, counting allocations inside process() only. */
    void render (Engine& e, int blocks, std::mt19937& rng)
    {
        std::vector<float> l (512), r (512);
        MidiEventBuffer in (64), out (4096);
        for (int i = 0; i < blocks; ++i)
        {
            const int n = 1 + static_cast<int> (rng() % 512);
            float* chans[] = { l.data(), r.data() };
            AudioBlock block { chans, 2, n };
            out.clear();
            in.clear();
            if (rng() % 4 == 0)
                in.add (MidiEvent::noteOn (0, 1, static_cast<std::uint8_t> (rng() % 128), 90)); // a keyboard plays along
            inAudio = true;
            e.process (block, in, out);
            inAudio = false;
        }
    }
}

TEST_CASE ("The engine never allocates on the audio thread", "[engine][realtime]")
{
    Engine e;
    e.prepare (48000.0, 512);
    e.setClickPort (99);
    std::mt19937 rng (17);

    e.loadSong (portSong (1));
    e.play();
    render (e, 2000, rng);

    // Transport and song changes between blocks, as the UI makes them.
    for (int i = 0; i < 300; ++i)
    {
        switch (rng() % 7)
        {
            case 0: e.play(); break;
            case 1: e.stop(); break;
            case 2: e.locate (static_cast<Tick> (rng() % 8000)); break;
            case 3: e.loadSong (i % 2 == 0 ? portSong (static_cast<unsigned> (i)) : test::kitchenSinkSong()); break;
            case 4: e.launchScene (0, 960); break;
            case 5: e.stopAllSlots (0); break;
            case 6: e.setRecording (rng() % 2 == 0); break;
        }
        render (e, 20, rng);
        while (e.popSlotChange()) {}
        while (e.popRecorded()) {}
        e.collectGarbage();
    }
    CHECK (audioAllocations.load() == 0);
}

TEST_CASE ("The allocation counter works", "[engine][realtime]")
{
    const auto before = audioAllocations.load();
    inAudio = true;
    void* p = ::operator new (16); // a direct call: the compiler may not leave it out
    inAudio = false;
    ::operator delete (p);
    CHECK (audioAllocations.load() == before + 1);
}

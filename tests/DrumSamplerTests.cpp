#include "arranger/engine/DrumSampler.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace arranger;
using Catch::Approx;

namespace
{
    std::shared_ptr<const Sample> constant (float value, std::size_t frames, double rate = 48000.0, int channels = 1)
    {
        auto s = std::make_shared<Sample>();
        s->sampleRate = rate;
        s->channels.assign (static_cast<std::size_t> (channels), std::vector<float> (frames, value));
        return s;
    }

    DrumPad pad (int note, std::shared_ptr<const Sample> s)
    {
        DrumPad p;
        p.note = note;
        p.sample = std::move (s);
        return p;
    }

    struct Rig
    {
        DrumSampler sampler;
        std::vector<float> left, right;
        MidiEventBuffer in { 64 }, out { 8 };

        explicit Rig (DrumKit kit, double rate = 48000.0) : sampler (std::move (kit)) { sampler.prepare ({ rate, 512 }); }

        void run (int frames)
        {
            std::vector<float> l (static_cast<std::size_t> (frames), 0.0f), r (l);
            float* chans[] = { l.data(), r.data() };
            AudioBlock block { chans, 2, frames };
            sampler.process ({}, block, in, out);
            in.clear();
            left.insert (left.end(), l.begin(), l.end());
            right.insert (right.end(), r.begin(), r.end());
        }
    };
}

TEST_CASE ("A pad plays its sample from the exact sample of the note, then stops")
{
    DrumKit kit;
    kit.pads.push_back (pad (36, constant (0.5f, 100)));
    Rig rig (kit);
    rig.in.add (MidiEvent::noteOn (10, 10, 36, 127));
    rig.in.add (MidiEvent::noteOn (20, 10, 40, 127)); // no pad on this note
    rig.run (256);

    CHECK (rig.left[9] == 0.0f);
    CHECK (rig.left[10] == Approx (0.5f));
    CHECK (rig.right[10] == Approx (0.5f)); // centre pan keeps the level
    CHECK (rig.left[109] == Approx (0.5f));
    CHECK (rig.left[110] == 0.0f);
    CHECK (rig.sampler.takePeak() == Approx (0.5f));
    CHECK (rig.sampler.takePeak() == 0.0f);
}

TEST_CASE ("Velocity, pad gain and pan set the level")
{
    DrumKit kit;
    auto p = pad (36, constant (1.0f, 1000));
    p.gain = 0.5f;
    p.pan = -1.0f;
    kit.pads.push_back (p);
    Rig rig (kit);
    rig.in.add (MidiEvent::noteOn (0, 1, 36, 64));
    rig.run (64);

    const float expected = 0.5f * (64.0f * 64.0f) / (127.0f * 127.0f) * 1.41421356f;
    CHECK (rig.left[5] == Approx (expected).epsilon (0.001));
    CHECK (rig.right[5] == Approx (0.0f).margin (1.0e-6));
}

TEST_CASE ("Samples at another rate, and tuned pads, play at the right speed")
{
    DrumKit kit;
    kit.pads.push_back (pad (36, constant (0.5f, 24000, 24000.0))); // 1 s at 24 kHz
    auto up = pad (37, constant (0.5f, 48000));                     // 1 s, an octave up = 0.5 s
    up.tuneSemitones = 12.0;
    kit.pads.push_back (up);

    for (const auto& [note, seconds] : { std::pair { 36, 1.0 }, std::pair { 37, 0.5 } })
    {
        Rig rig (kit);
        rig.in.add (MidiEvent::noteOn (0, 1, note, 127));
        for (int i = 0; i < 100; ++i)
            rig.run (512);
        const auto end = static_cast<std::size_t> (seconds * 48000.0);
        CHECK (rig.left[end - 4] != 0.0f);
        CHECK (rig.left[end + 4] == 0.0f);
    }
}

TEST_CASE ("Slices play only their own part of the loop")
{
    auto loop = std::make_shared<Sample>();
    loop->sampleRate = 48000.0;
    loop->channels.assign (1, std::vector<float> (300));
    for (std::size_t i = 0; i < 300; ++i)
        loop->channels[0][i] = i < 100 ? 0.1f : (i < 200 ? 0.2f : 0.3f);

    const auto kit = kitFromSlices (loop, slicing::byGrid (300, 3), 60);
    REQUIRE (kit.pads.size() == 3);
    CHECK (kit.pads[2].note == 62);

    Rig rig (kit);
    rig.in.add (MidiEvent::noteOn (0, 1, 61, 127));
    rig.run (256);
    CHECK (rig.left[0] == Approx (0.2f));
    CHECK (rig.left[99] == Approx (0.2f));
    CHECK (rig.left[100] == 0.0f);
}

TEST_CASE ("Choke groups cut each other, and gated pads stop at note-off")
{
    DrumKit kit;
    auto closed = pad (42, constant (0.25f, 48000));
    auto open = pad (46, constant (0.5f, 48000));
    closed.chokeGroup = open.chokeGroup = 1;
    auto gated = pad (50, constant (0.5f, 48000));
    gated.oneShot = false;
    kit.pads = { closed, open, gated };

    Rig rig (kit);
    rig.in.add (MidiEvent::noteOn (0, 10, 46, 127));
    rig.in.add (MidiEvent::noteOn (1000, 10, 42, 127));
    rig.run (4096);
    CHECK (rig.left[999] == Approx (0.5f));
    CHECK (rig.left[2000] == Approx (0.25f)); // the open hat has faded out, only the closed one sounds
    for (std::size_t i = 1001; i < 1300; ++i)
        REQUIRE (std::abs (rig.left[i] - rig.left[i - 1]) < 0.01f); // faded, not clicked

    Rig gate (kit);
    gate.in.add (MidiEvent::noteOn (0, 10, 50, 127));
    gate.in.add (MidiEvent::noteOff (500, 10, 50));
    gate.run (4096);
    CHECK (gate.left[499] == Approx (0.5f));
    CHECK (gate.left[1000] == 0.0f);

    Rig oneShot (kit);
    oneShot.in.add (MidiEvent::noteOn (0, 10, 42, 127));
    oneShot.in.add (MidiEvent::noteOff (500, 10, 42));
    oneShot.run (4096);
    CHECK (oneShot.left[1000] == Approx (0.25f)); // one-shots ignore note-off
}

TEST_CASE ("More notes than voices never breaks: the oldest voice goes")
{
    DrumKit kit;
    for (int n = 0; n < 60; ++n)
        kit.pads.push_back (pad (36 + n, constant (0.01f, 480000)));
    Rig rig (kit);
    for (int n = 0; n < 60; ++n)
        rig.in.add (MidiEvent::noteOn (n, 1, 36 + n, 127));
    rig.run (512);
    CHECK (rig.left[400] == Approx (0.01f * DrumSampler::kVoices).epsilon (0.01));
    rig.sampler.reset();
    rig.run (2048);
    CHECK (rig.left.back() == 0.0f);
}

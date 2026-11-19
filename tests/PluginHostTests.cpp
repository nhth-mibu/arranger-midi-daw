// The plugin host with real VST3 plugins: the two test plugins in tests/plugins
// (a gain effect and a sine instrument). Loads them as the app does, runs them
// through HostedProcessor (the bridge to the engine), and checks sound,
// controls, bypass, and that their settings survive a save.
//
// Built only with -DARRANGER_BUILD_APP=ON -DARRANGER_BUILD_TEST_PLUGINS=ON.

#include "plugins/PluginHost.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace arranger;

namespace
{
    std::unique_ptr<juce::AudioPluginInstance> load (const char* bundle, juce::PluginDescription& found)
    {
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> types;
        format.findAllTypesForFile (types, juce::String::fromUTF8 (bundle));
        REQUIRE (types.size() == 1);
        found = *types[0];
        juce::String error;
        auto instance = format.createInstanceFromDescription (found, 48000.0, 512, error);
        INFO (error);
        REQUIRE (instance != nullptr);
        return instance;
    }

    ProcessContext context (int n, std::int64_t at)
    {
        ProcessContext c;
        c.playing = true;
        c.numSamples = n;
        c.sampleTime = at;
        c.sampleRate = 48000.0;
        c.tempoBpm = 120.0;
        return c;
    }

    /** Runs one block of a constant signal through the processor; returns the left output. */
    std::vector<float> run (HostedProcessor& p, float input, const MidiEventBuffer& midi = MidiEventBuffer (4), int n = 512)
    {
        std::vector<float> l (static_cast<std::size_t> (n), input), r (static_cast<std::size_t> (n), input);
        float* chans[] = { l.data(), r.data() };
        AudioBlock block { chans, 2, n };
        MidiEventBuffer out (4);
        p.process (context (n, 0), block, midi, out);
        return l;
    }

    float peak (const std::vector<float>& v)
    {
        float m = 0.0f;
        for (const auto x : v)
            m = std::max (m, std::abs (x));
        return m;
    }
}

TEST_CASE ("A VST3 effect loads, changes the sound, follows its control and bypass", "[plugins]")
{
    juce::PluginDescription d;
    HostedProcessor gain (load (ARRANGER_TEST_GAIN_VST3, d), false);
    gain.prepare ({ 48000.0, 512 });

    CHECK (d.name.contains ("Gain"));
    const auto slot = PluginHost::slotFor (d);
    CHECK (slot.format == "VST3");
    CHECK_FALSE (slot.id.empty());
    CHECK (gain.latencySamples() == 0);

    // Its default gain is 0.5.
    auto out = run (gain, 0.8f);
    CHECK (std::abs (out[100] - 0.4f) < 1e-4f);

    // Control 0 is the gain parameter (0..1), as automation sets it.
    gain.setControl (0, 0.25f);
    out = run (gain, 0.8f);
    CHECK (std::abs (out[100] - 0.2f) < 1e-3f);

    gain.setBypass (true);
    out = run (gain, 0.8f);
    CHECK (out[100] == 0.8f);
    gain.setBypass (false);

    // Blocks bigger than prepared are skipped, not overrun.
    out = run (gain, 0.8f, MidiEventBuffer (4), 4096);
    CHECK (out[100] == 0.8f);
}

TEST_CASE ("A VST3 effect's settings survive a save and load", "[plugins]")
{
    juce::PluginDescription d;
    HostedProcessor a (load (ARRANGER_TEST_GAIN_VST3, d), false);
    a.prepare ({ 48000.0, 512 });
    a.setControl (0, 0.1f);
    run (a, 1.0f);
    const auto saved = a.state();
    CHECK_FALSE (saved.empty());

    HostedProcessor b (load (ARRANGER_TEST_GAIN_VST3, d), false);
    b.prepare ({ 48000.0, 512 });
    b.setState (saved);
    const auto out = run (b, 1.0f);
    CHECK (std::abs (out[100] - 0.1f) < 1e-3f);

    // Bad saved settings do no harm.
    HostedProcessor c (load (ARRANGER_TEST_GAIN_VST3, d), false);
    c.prepare ({ 48000.0, 512 });
    c.setState ("not base64 at all \xff");
    c.setState ("");
    CHECK (std::abs (run (c, 1.0f)[100] - 0.5f) < 1e-3f);
}

TEST_CASE ("A VST3 instrument plays a note on its sample", "[plugins]")
{
    juce::PluginDescription d;
    HostedProcessor tone (load (ARRANGER_TEST_TONE_VST3, d), true);
    tone.prepare ({ 48000.0, 512 });
    CHECK (d.isInstrument);

    // Silence without notes; the instrument ignores the input audio.
    CHECK (peak (run (tone, 0.0f)) < 1e-6f);

    MidiEventBuffer midi (4);
    midi.add (MidiEvent::noteOn (200, 1, 69, 127));
    const auto out = run (tone, 0.0f, midi);
    float before = 0.0f;
    for (int i = 0; i < 200; ++i)
        before = std::max (before, std::abs (out[static_cast<std::size_t> (i)]));
    CHECK (before < 1e-6f);
    CHECK (peak (out) > 0.01f);

    // The note keeps sounding until a note off (or all notes off, which Stop sends).
    CHECK (peak (run (tone, 0.0f)) > 0.01f);
    MidiEventBuffer off (4);
    off.add (MidiEvent::make (0, 0xB0, 123, 0, 0)); // all notes off, channel 1
    run (tone, 0.0f, off);
    CHECK (peak (run (tone, 0.0f)) < 1e-6f);
    tone.reset(); // does no harm
}

TEST_CASE ("A plugin that is not on this computer gives a clear reason", "[plugins]")
{
    PluginHost host;
    PluginSlot slot;
    slot.format = "VST3";
    slot.id = "VST3-Missing-12345678-87654321";
    slot.name = "Missing Plugin";
    juce::String error;
    CHECK (host.create (slot, false, 48000.0, 512, error) == nullptr);
    CHECK (error.contains ("Missing Plugin"));
}

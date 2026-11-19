// A test plugin that misbehaves on purpose, for the plugin host tests.
// ARRANGER_TEST_PLUGIN_MODE picks how, when the plugin is made:
//   crash    - crashes (as a broken plugin does during a scan)
//   hang     - never returns
//   latency  - reports a latency that changes after it starts (1000 samples, then 2000)
//   (other)  - a plain pass-through effect
#include <juce_audio_utils/juce_audio_utils.h>

#include <chrono>
#include <cstdlib>
#include <thread>

namespace
{
    juce::String mode()
    {
        const char* m = std::getenv ("ARRANGER_TEST_PLUGIN_MODE");
        return m != nullptr ? juce::String (m) : juce::String();
    }

    void misbehaveNow()
    {
        if (mode() == "crash")
            std::abort();
        if (mode() == "hang")
            for (;;)
                std::this_thread::sleep_for (std::chrono::seconds (1));
    }

    // Broken plugins often fail as soon as their file is loaded, before any plugin is made.
    const bool loaded = [] {
        misbehaveNow();
        return true;
    }();
}

class TestMisbehave final : public juce::AudioProcessor
{
public:
    TestMisbehave()
        : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        misbehaveNow();
    }

    const juce::String getName() const override { return "Arranger Test Misbehave"; }
    void prepareToPlay (double, int) override
    {
        if (mode() == "latency")
            setLatencySamples (getLatencySamples() == 0 ? 1000 : 2000);
    }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestMisbehave(); }

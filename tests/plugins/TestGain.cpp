// A test effect: one "Gain" control (0 to 1, starts at 0.5). The output is the input times the gain.
#include <juce_audio_utils/juce_audio_utils.h>

class TestGain final : public juce::AudioProcessor
{
public:
    TestGain()
        : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        addParameter (gain = new juce::AudioParameterFloat (juce::ParameterID { "gain", 1 }, "Gain", 0.0f, 1.0f, 0.5f));
    }

    const juce::String getName() const override { return "Arranger Test Gain"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override { buffer.applyGain (gain->get()); }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& data) override { juce::MemoryOutputStream (data, false).writeFloat (gain->get()); }
    void setStateInformation (const void* data, int size) override
    {
        if (size >= 4)
            *gain = juce::MemoryInputStream (data, static_cast<size_t> (size), false).readFloat();
    }

private:
    juce::AudioParameterFloat* gain;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestGain(); }

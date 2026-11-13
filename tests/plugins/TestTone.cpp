// A test instrument: each note plays a sine wave at the note's pitch, at a level of 0.25.
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <cmath>

class TestTone final : public juce::AudioProcessor
{
public:
    TestTone() : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}

    const juce::String getName() const override { return "Arranger Test Tone"; }
    void prepareToPlay (double rate, int) override { sampleRate = rate; }
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        auto event = midi.begin();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            for (; event != midi.end() && (*event).samplePosition <= i; ++event)
            {
                const auto m = (*event).getMessage();
                if (m.isNoteOn()) down[static_cast<size_t> (m.getNoteNumber())] = true;
                else if (m.isNoteOff()) down[static_cast<size_t> (m.getNoteNumber())] = false;
                else if (m.isAllNotesOff() || m.isAllSoundOff()) down.fill (false);
            }
            float v = 0.0f;
            for (size_t n = 0; n < down.size(); ++n)
                if (down[n])
                {
                    phase[n] += 440.0 * std::pow (2.0, (static_cast<double> (n) - 69.0) / 12.0) / sampleRate;
                    v += 0.25f * static_cast<float> (std::sin (6.283185307179586 * phase[n]));
                }
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.setSample (c, i, v);
        }
    }

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    double sampleRate = 48000.0;
    std::array<bool, 128> down {};
    std::array<double, 128> phase {};
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestTone(); }

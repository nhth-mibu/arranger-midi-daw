#pragma once

#include "Theme.h"
#include "../Session.h"

namespace arranger::ui
{

/** Remote control of the style player in a Yamaha PSR-S970/S770. */
class PsrStyleView final : public juce::Component, private juce::Timer
{
public:
    explicit PsrStyleView (Session&);
    ~PsrStyleView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void send (const MidiBytes&);
    void sendSection (int code);
    void sendTempo();
    void sendChord();
    void updateStatus();
    const DeviceProfile* profile() const;

    Session& session;
    juce::Label title, status, sectionLabel, tempoLabel, chordLabel, styleLabel, splitLabel;
    juce::OwnedArray<juce::TextButton> sectionButtons;
    juce::Slider tempo;
    juce::TextButton sendTempoButton { "Send Tempo" };
    juce::ComboBox root, chordType, bass;
    juce::TextButton sendChordButton { "Send Chord" };
    juce::Slider styleNumber, splitPoint;
    juce::TextButton sendStyleButton { "Send Style No." }, sendSplitButton { "Send Split" };
};

} // namespace arranger::ui

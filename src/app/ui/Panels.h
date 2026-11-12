#pragma once

#include "LiveState.h"
#include "Theme.h"
#include "../Session.h"

#include <functional>

namespace arranger::ui
{

/** Draws a rounded panel card. */
void drawCard (juce::Graphics&, juce::Rectangle<float>);

/** Right-hand inspector: the selected track's output and sound, song, metronome, audio and MIDI status. */
class Inspector final : public juce::Component, private juce::ChangeListener
{
public:
    Inspector (Session&, const LiveState&);
    ~Inspector() override;

    std::function<void()> onAudioSettings;   ///< open the device dialog
    std::function<void()> onChooseSoundFont; ///< pick an SF2 file

    /** MIDI output port names offered as track outputs. */
    void setOutputPorts (const juce::StringArray&);

    /** Re-read the song into the controls. */
    void songChanged();
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { songChanged(); }
    void showSoundMenu();

    Session& session;
    const LiveState& live;
    juce::StringArray outputPorts;

    juce::TextEditor trackName;
    juce::ComboBox outputBox, channelBox;
    juce::TextButton soundButton, soundFontButton { "SoundFont..." };

    juce::TextEditor titleEditor;
    juce::ComboBox keyBox;
    juce::ToggleButton clickToggle { "Metronome" }, accentToggle { "Accent" };
    juce::Slider levelSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::TextButton audioButton { "Audio and MIDI settings" };

    juce::Rectangle<int> trackLabel, fontInfo, projectLabel, metronomeLabel, audioLabel, audioInfo, midiLabel, midiInfo;
    juce::String shownAudio, shownMidi, shownFont;
    int shownBeat = -1;
    juce::Rectangle<int> beatLights;
};

/** Bottom bar: MIDI inputs with activity dots, shortcut hints. */
class StatusBar final : public juce::Component
{
public:
    explicit StatusBar (const LiveState&);
    void refresh();
    void paint (juce::Graphics&) override;

private:
    const LiveState& live;
    juce::String shown;
};

} // namespace arranger::ui

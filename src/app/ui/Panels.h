#pragma once

#include "LiveState.h"
#include "Theme.h"

#include <functional>

namespace arranger::ui
{

/** Draws a rounded panel card. */
void drawCard (juce::Graphics&, juce::Rectangle<float>);

/** The track list on the left. */
class TracksPanel final : public juce::Component
{
public:
    explicit TracksPanel (const Song&);
    void paint (juce::Graphics&) override;

private:
    const Song& song;
};

/** Ruler, bar grid and playhead. Clips and the piano roll arrive in Phase 1. */
class TimelineView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    TimelineView (const Song&, const LiveState&);

    /** 0 Timeline, 1 Score, 2 Beat, 3 Clips. Only the Timeline exists in Phase 0. */
    void setView (int index);
    void refresh();

    std::function<void (Tick)> onLocate;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    float barWidth() const;
    juce::Rectangle<int> gridArea() const;
    int playheadX() const;

    const Song& song;
    const LiveState& live;
    int view = 0;
    std::int64_t firstBar = 0; ///< pages along with the playhead
    int shownPlayheadX = -1;
};

/** Right-hand inspector: project, metronome, audio and MIDI status. */
class Inspector final : public juce::Component
{
public:
    Inspector (Song&, const LiveState&);

    std::function<void()> onSongEdited;      ///< title, key or metronome changed
    std::function<void()> onAudioSettings;   ///< open the device dialog

    /** Re-read the song into the controls (after open / new). */
    void songChanged();
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Song& song;
    const LiveState& live;

    juce::TextEditor titleEditor;
    juce::ComboBox keyBox;
    juce::ToggleButton clickToggle { "Metronome" }, accentToggle { "Accent first beat" };
    juce::Slider levelSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::TextButton audioButton { "Audio and MIDI settings" };

    juce::Rectangle<int> projectLabel, keyLabel, metronomeLabel, levelLabel, audioLabel, audioInfo, midiLabel, midiInfo;
    juce::String shownAudio, shownMidi;
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

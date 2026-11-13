#pragma once

#include "Icons.h"
#include "LiveState.h"
#include "Theme.h"
#include "../Session.h"

#include <functional>

namespace arranger::ui
{

/** Square transport button with a vector icon. `onColour` tints it while toggled on. */
class IconButton final : public juce::Button
{
public:
    IconButton (const juce::String& name, icons::Icon, juce::String onColourKey = "accent");
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    icons::Icon icon;
    juce::String onColourKey;
};

/** The position / tempo / meter display. Drag or double-click the tempo to change it; click the meter for a menu. */
class LcdDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    LcdDisplay();

    void setValues (double positionTicks, double tempoBpm, TimeSignature);

    std::function<void (double)> onTempoChange;
    std::function<void (TimeSignature)> onMeterChange;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<int> cell (int index) const;
    void editTempo();

    double position = 0.0, tempo = 120.0;
    TimeSignature meter;
    double dragStartTempo = 0.0;
    bool draggingTempo = false;
    juce::TextEditor tempoEditor;
};

/** Timeline | Mixer | Score | Beat | Clips. */
class ViewTabs final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ViewTabs();
    std::function<void (int)> onChange;

    int selected() const noexcept { return current; }
    void setSelected (int index);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> tabBounds (int index) const;
    juce::StringArray names { "Timeline", "Mixer", "Score", "Beat", "Clips" };
    int current = 0;
};

class HeaderBar final : public juce::Component
{
public:
    HeaderBar (Session&, const LiveState&);

    IconButton stopButton { "Stop", icons::Icon::stop };
    IconButton playButton { "Play", icons::Icon::play, "playing" };
    IconButton recordButton { "Record", icons::Icon::record, "recording" };
    IconButton loopButton { "Loop", icons::Icon::loop };
    IconButton clickButton { "Metronome", icons::Icon::metronome };
    LcdDisplay lcd;
    ViewTabs tabs;

    /** Refresh from the song and live state. Repaints only what changed. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<int> titleArea, statusArea;
    Session& session;
    const LiveState& live;
    juce::String shownStatus, shownTitle;
};

} // namespace arranger::ui

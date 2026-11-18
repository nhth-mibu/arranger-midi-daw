#pragma once

#include "../Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace arranger::ui
{

/**
    Kids Mode: a big, friendly screen over the whole window. Any key on a
    MIDI keyboard (or on the computer, or a click on the circle) plays the
    song; it pauses when no key is pressed for a while.

    A small bar at the bottom is for grown-ups: the wait before the pause,
    whether the keys play their own notes, and the way out.
*/
class KidsView final : public juce::Component, private juce::Timer
{
public:
    explicit KidsView (Session&);
    ~KidsView() override;

    /** Takes the settings from the session (call when they change). */
    void refresh();

    /** For screenshots without an audio device: show it playing, with some notes in the air. */
    void setPreview (bool playing);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    void burst (double now);
    bool playingNow() const;
    juce::Rectangle<float> circleBounds() const;

    Session& session;
    juce::Label pauseLabel { {}, "Pause after" };
    juce::Slider pauseSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ToggleButton keysSound { "Keys also play their own notes" };
    juce::TextButton exitButton { "Exit Kids Mode" };

    struct Note
    {
        double born = 0.0;
        float angle = 0.0f;
        int colour = 0;
    };
    std::vector<Note> notes;
    std::vector<double> rings;
    int seenPresses = 0;
    double phase = 0.0;
    bool preview = false;
    juce::Random random;
    juce::Rectangle<int> controlBar;
};

} // namespace arranger::ui

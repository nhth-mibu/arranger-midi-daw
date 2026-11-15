#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace arranger::ui
{

namespace shortcuts
{
    /** Says which command's current key presses belong in this component's tip. */
    void setCommand (juce::Component&, const juce::String& commandName);

    /** Adds "[Cmd+S]" (or the user's keys) for a command to a tip. */
    juce::String withShortcut (juce::String tip, const juce::ApplicationCommandManager&, const juce::String& commandName);
}

/** The tip's pale yellow face, kept apart from the theme's dark panels. */
inline const juce::Colour tooltipBackgroundColour { 0xfff5edc8 };
inline const juce::Colour tooltipTextColour { 0xff000000 };
inline const juce::Colour tooltipOutlineColour { 0xffd8cda2 };

/**
    Tooltips that stay out of the way: a long delay, no tip while a modifier
    is held, the mouse is down, or the user is scrolling, and a soft yellow
    face instead of the dark panel colour.
*/
class AppTooltipWindow final : public juce::TooltipWindow
{
public:
    AppTooltipWindow (juce::Component* parent, juce::ApplicationCommandManager&);
    ~AppTooltipWindow() override;

    juce::String getTipFor (juce::Component&) override;

private:
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool suppressed() const;
    void checkState();

    struct Watchdog final : juce::Timer
    {
        explicit Watchdog (AppTooltipWindow& o) : owner (o) {}
        void timerCallback() override { owner.checkState(); }
        AppTooltipWindow& owner;
    };

    Watchdog watchdog { *this };
    juce::ApplicationCommandManager& commands;
    bool mouseHeld = false;
    double lastScrollMs = 0.0;
};

} // namespace arranger::ui

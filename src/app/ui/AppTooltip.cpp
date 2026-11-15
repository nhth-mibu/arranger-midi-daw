#include "AppTooltip.h"

namespace arranger::ui
{

AppTooltipWindow::AppTooltipWindow (juce::Component* parent)
    : juce::TooltipWindow (parent, 6000)
{
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xfff5edc8));
    setColour (juce::TooltipWindow::textColourId, juce::Colours::black);
    setColour (juce::TooltipWindow::outlineColourId, juce::Colour (0xffd8cda2));
    juce::Desktop::getInstance().addGlobalMouseListener (this);
    watchdog.startTimer (120);
}

AppTooltipWindow::~AppTooltipWindow()
{
    watchdog.stopTimer();
    juce::Desktop::getInstance().removeGlobalMouseListener (this);
}

bool AppTooltipWindow::suppressed() const
{
    const auto mods = juce::ModifierKeys::getCurrentModifiersRealtime();
    if (mods.isShiftDown() || mods.isCtrlDown() || mods.isCommandDown() || mods.isAltDown())
        return true;
    if (mouseHeld)
        return true;
    if (juce::Desktop::getInstance().getMainMouseSource().isDragging())
        return true;
    return juce::Time::getMillisecondCounterHiRes() - lastScrollMs < 800.0;
}

juce::String AppTooltipWindow::getTipFor (juce::Component& component)
{
    if (suppressed())
        return {};
    return juce::TooltipWindow::getTipFor (component);
}

void AppTooltipWindow::checkState()
{
    if (suppressed())
        hideTip();
}

void AppTooltipWindow::mouseDown (const juce::MouseEvent&)
{
    mouseHeld = true;
    hideTip();
}

void AppTooltipWindow::mouseDrag (const juce::MouseEvent&)
{
    mouseHeld = true;
    hideTip();
}

void AppTooltipWindow::mouseUp (const juce::MouseEvent&)
{
    mouseHeld = false;
}

void AppTooltipWindow::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&)
{
    lastScrollMs = juce::Time::getMillisecondCounterHiRes();
    hideTip();
}

} // namespace arranger::ui

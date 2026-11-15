#include "AppTooltip.h"

namespace arranger::ui
{

namespace
{
    juce::String keyText (const juce::KeyPress& key)
    {
        auto text = key.getTextDescription();
        text = text.replace ("command + ", "Cmd+");
        text = text.replace ("ctrl + ", "Ctrl+");
        text = text.replace ("shift + ", "Shift+");
        text = text.replace ("option + ", "Alt+");
        text = text.replace ("alt + ", "Alt+");
        if (text == "spacebar") return "Space";
        if (text == "escape") return "Esc";
        if (text == "return") return "Enter";
        if (text == "backspace") return "Backspace";
        if (text == "home") return "Home";
        if (text == "end") return "End";
        if (text == "page up") return "Page Up";
        if (text == "page down") return "Page Down";
        if (text == "cursor left") return "Left";
        if (text == "cursor right") return "Right";
        if (text == "cursor up") return "Up";
        if (text == "cursor down") return "Down";
        return text;
    }

}

namespace shortcuts
{
    void setCommand (juce::Component& component, const juce::String& commandName)
    {
        component.getProperties().set ("shortcutCommand", commandName);
    }

    juce::String withShortcut (juce::String tip, const juce::ApplicationCommandManager& commands, const juce::String& commandName)
    {
        if (tip.isEmpty() || commandName.isEmpty())
            return tip;
        const juce::ApplicationCommandInfo* command = nullptr;
        for (int i = 0; i < commands.getNumCommands(); ++i)
            if (const auto* info = commands.getCommandForIndex (i); info != nullptr && info->shortName == commandName)
            {
                command = info;
                break;
            }
        if (command == nullptr || commands.getKeyMappings() == nullptr)
            return tip;

        juce::StringArray keys;
        for (const auto& key : commands.getKeyMappings()->getKeyPressesAssignedToCommand (command->commandID))
            keys.add (keyText (key));
        if (keys.isEmpty())
            return tip;
        return tip + " [" + keys.joinIntoString (" / ") + "]";
    }
}

AppTooltipWindow::AppTooltipWindow (juce::Component* parent, juce::ApplicationCommandManager& manager)
    : juce::TooltipWindow (parent, 6000), commands (manager)
{
    // TooltipWindow paints through its look-and-feel; AppLookAndFeel sets the
    // pale yellow tip colours there.
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
    auto tip = juce::TooltipWindow::getTipFor (component);
    const auto commandName = component.getProperties()["shortcutCommand"].toString();
    return shortcuts::withShortcut (tip, commands, commandName);
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

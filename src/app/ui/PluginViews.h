#pragma once

#include "Theme.h"
#include "../Session.h"

#include <functional>
#include <map>

namespace arranger::ui
{

/** The windows of the plugins. One window per plugin; it closes when its plugin leaves the song. */
class PluginWindows final : private juce::ChangeListener
{
public:
    explicit PluginWindows (Session&);
    ~PluginWindows() override;

    /** Opens (or brings forward) the window of a slot. Track 0 is the master; index -1 is the instrument. */
    void show (TrackId track, int index);
    void closeAll();

private:
    class Window;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    Session& session;
    std::map<std::string, std::unique_ptr<Window>> windows;
};

/**
    The plugins of one track, or of the master (track 0): the instrument of a
    plugin track, then the insert effects in the order the sound goes through
    them.
*/
class PluginRackPanel final : public juce::Component, private juce::ChangeListener
{
public:
    PluginRackPanel (Session&, PluginWindows&, TrackId track);
    ~PluginRackPanel() override;

    std::function<void()> onScan; ///< the user asked for a plugin scan

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    /** A menu of plugins by maker. `chosen` gets the plugin. */
    static void showPluginMenu (Session&, bool instruments, juce::Component* target, std::function<void (const PluginSlot&)> chosen, std::function<void()> scan);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuild();
    const std::vector<PluginSlot>* effects() const;
    void editEffects (const char* name, const std::function<void (std::vector<PluginSlot>&)>&);
    juce::Rectangle<int> rowBounds (int row) const;
    int rowCount() const;

    Session& session;
    PluginWindows& windows;
    TrackId track;
    juce::TextButton addButton { "Add Effect..." };
    bool hasInstrument = false;
};

/** Scans the plugin folders with a progress window. Calls `done` on the UI thread with a summary. */
void scanPlugins (PluginHost&, std::function<void (juce::String summary)> done);

} // namespace arranger::ui

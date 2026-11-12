#pragma once

#include "AppLookAndFeel.h"
#include "HeaderBar.h"
#include "Panels.h"

#include "arranger/engine/Engine.h"
#include "../../platform/juce/JuceDeviceHost.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace arranger::ui
{

/**
    The main window's content: header, track list, timeline, inspector and
    status bar over one Song. Owns the menus and keyboard commands.
*/
class MainComponent final : public juce::Component,
                            public juce::ApplicationCommandTarget,
                            public juce::MenuBarModel,
                            private juce::Timer
{
public:
    enum CommandIds
    {
        cmdNew = 1,
        cmdOpen,
        cmdSave,
        cmdSaveAs,
        cmdPlayStop,
        cmdGoToStart,
        cmdToggleMetronome,
        cmdAudioSettings,
        cmdDarkTheme,
        cmdLightTheme,
    };

    MainComponent (Engine&, platform::JuceDeviceHost&, juce::PropertiesFile* settings);
    ~MainComponent() override;

    /** Window title: song name, file, unsaved mark. */
    std::function<void (const juce::String&)> onTitleChanged;

    bool openFile (const juce::File&, bool showErrors);
    void setSong (const Song&);
    void setThemeDark (bool dark);

    /** For screenshots without an audio device: pretend the playhead is here. */
    void setPreviewPosition (double ticks, bool playing);

    juce::ApplicationCommandManager& commandManager() noexcept { return commands; }

    // Component
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override {}

    /** Ask to save unsaved changes, then call `then` unless cancelled. */
    void confirmDiscard (std::function<void()> then);

private:
    void timerCallback() override;
    void pollLiveState();
    void songEdited (bool sendToEngine);
    void updateTitle();
    void stopOrReturn();
    void save (bool askForFile);
    void showAudioSettings();

    Engine& engine;
    platform::JuceDeviceHost& devices;
    juce::PropertiesFile* settings;

    Song song;
    LiveState live;
    bool dirty = false;
    juce::File currentFile;
    bool previewMode = false;

    AppLookAndFeel lookAndFeel;
    juce::ApplicationCommandManager commands;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<juce::FileChooser> chooser;

    HeaderBar header { song, live };
    TracksPanel tracks { song };
    TimelineView timeline { song, live };
    Inspector inspector { song, live };
    StatusBar statusBar { live };

    juce::Rectangle<int> tracksCard, timelineCard, inspectorCard;
};

} // namespace arranger::ui

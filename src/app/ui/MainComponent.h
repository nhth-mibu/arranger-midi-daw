#pragma once

#include "AppLookAndFeel.h"
#include "BeatView.h"
#include "ClipGridView.h"
#include "HeaderBar.h"
#include "MixerView.h"
#include "Panels.h"
#include "PianoRoll.h"
#include "TimelineViews.h"

#include "../Session.h"
#include "arranger/engine/Engine.h"
#include "../../platform/juce/JuceDeviceHost.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace arranger::ui
{

/**
    The main window's content: header, track list and arrangement (or the
    mixer), the piano roll under them, the inspector and the status bar.
    Owns the session, the menus and keyboard commands.
*/
class MainComponent final : public juce::Component,
                            public juce::ApplicationCommandTarget,
                            public juce::MenuBarModel,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    enum CommandIds
    {
        cmdNew = 1,
        cmdOpen,
        cmdSave,
        cmdSaveAs,
        cmdImportMidi,
        cmdExportMidi,
        cmdOpenStyle,
        cmdSaveStyle,
        cmdMakeStyle,
        cmdSectionFromLoop,
        cmdChooseSoundFont,
        cmdUndo,
        cmdRedo,
        cmdDelete,
        cmdDuplicate,
        cmdSplit,
        cmdSelectAll,
        cmdQuantise,
        cmdQuantiseOnRecord,
        cmdAddTrack,
        cmdPlayStop,
        cmdGoToStart,
        cmdRecord,
        cmdLoop,
        cmdToggleMetronome,
        cmdShowTimeline,
        cmdShowMixer,
        cmdShowBeat,
        cmdShowClips,
        cmdCloseEditor,
        cmdAudioSettings,
        cmdDeviceFolder,
        cmdDarkTheme,
        cmdLightTheme,
    };

    MainComponent (Engine&, platform::JuceDeviceHost&, juce::PropertiesFile* settings);
    ~MainComponent() override;

    /** Window title: song name, file, unsaved mark. */
    std::function<void (const juce::String&)> onTitleChanged;

    /** Opens a project, or imports a MIDI file (.mid). */
    bool openFile (const juce::File&, bool showErrors);
    void setThemeDark (bool dark);

    /** 0 timeline, 1 mixer, 2 score, 3 beat, 4 clips. */
    void showView (int index);

    /** Opens the first clip of the first track in the note editor (screenshots). */
    void openFirstClipInEditor();

    /** Opens the slicer for a loop on the selected track (also used for screenshots). */
    void showSlicer (const juce::File& loop) { openSlicer (loop); }

    /** For screenshots without an audio device: pretend the playhead is here. */
    void setPreviewPosition (double ticks, bool playing);

    juce::ApplicationCommandManager& commandManager() noexcept { return commands; }
    Session& getSession() noexcept { return session; }

    // Component
    void paint (juce::Graphics&) override;
    void resized() override;

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
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void pollLiveState();
    void updateTitle();
    void save (bool askForFile);
    void importMidi();
    void exportMidi();
    void openStyle();
    void saveStyle();
    void chooseSoundFont();
    void loadKitSamples();
    void sliceLoop();
    void openSlicer (const juce::File&);
    void showAudioSettings();
    void loadDeviceProfiles();
    static juce::File deviceFolder();
    void quantise();
    bool editorHasFocus() const;

    Engine& engine;
    platform::JuceDeviceHost& devices;
    juce::PropertiesFile* settings;

    Session session { engine, devices };
    LiveState live;
    bool previewMode = false;
    int view = 0;
    int outputPollCountdown = 0;

    AppLookAndFeel lookAndFeel;
    juce::ApplicationCommandManager commands;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<juce::FileChooser> chooser;

    TimelineGeometry geometry;
    HeaderBar header { session, live };
    TrackList trackList { session, geometry };
    Arrangement arrangement { session, geometry };
    MixerView mixer { session };
    BeatView beat { session };
    ClipGridView clipGrid { session };
    PianoRoll pianoRoll { session };
    Inspector inspector { session, live };
    StatusBar statusBar { live };

    juce::Component::SafePointer<juce::DialogWindow> slicerWindow;
    juce::Rectangle<int> mainCard, editorCard, inspectorCard;
};

} // namespace arranger::ui

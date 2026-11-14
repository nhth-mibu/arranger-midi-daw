#pragma once

#include "AppLookAndFeel.h"
#include "../McpService.h"
#include "BeatView.h"
#include "ClipGridView.h"
#include "HeaderBar.h"
#include "MixerView.h"
#include "../AssistantChat.h"
#include "Panels.h"
#include "PianoRoll.h"
#include "PluginViews.h"
#include "ScoreView.h"
#include "TimelineViews.h"

#include "../Session.h"
#include "arranger/engine/Engine.h"
#include "../../platform/juce/JuceDeviceHost.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace arranger::ui
{

/**
    The main window's content: header, the current view, the piano roll and
    the mixer panel under it, the inspector and the status bar.
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
        cmdImportMusicXml,
        cmdExportMusicXml,
        cmdImportAudio,
        cmdOpenStyle,
        cmdSaveStyle,
        cmdMakeStyle,
        cmdSectionFromLoop,
        cmdFillFromStyle,
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
        cmdAddBus,
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
        cmdScanPlugins,
        cmdMasterEffects,
        cmdDeviceFolder,
        cmdDarkTheme,
        cmdLightTheme,
        cmdShortcuts,
        cmdCut,   // new commands go last: saved shortcuts keep their ids
        cmdCopy,
        cmdPaste,
        cmdAllowAi,
        cmdCopyAiSetup,
        cmdAiChat,
        cmdStyleFolder,
    };

    MainComponent (Engine&, platform::JuceDeviceHost&, juce::PropertiesFile* settings, PluginHost* plugins = nullptr);
    ~MainComponent() override;

    /** Window title: song name, file, unsaved mark. */
    std::function<void (const juce::String&)> onTitleChanged;

    /** Opens a project, or imports a MIDI file (.mid), a style or a MusicXML file. */
    bool openFile (const juce::File&, bool showErrors);
    void setThemeDark (bool dark);
    /** Starts or stops the service that lets AI assistants change the song (Options > Allow AI Assistants). */
    void setAiAllowed (bool);

    enum View { viewTimeline = 0, viewScore, viewBeat, viewClips };

    /** A View value. */
    void showView (int index);

    /** The mixer panel under the view. The height is in pixels. */
    void setMixerShown (bool shown);
    void setMixerHeight (int height);
    bool isMixerShown() const noexcept { return mixerShown; }

    /**
        Writes a section of a style file onto new tracks of the Timeline, for
        a number of bars, with one chord for each bar ("C Am F G").
        False when the file can't be read or has no such section.
    */
    bool fillFromStyle (const juce::File& styleFile, const juce::String& section, const juce::String& chords, int bars, int startBar, bool useStyleTempo);

    /** The AI chat window (Options > AI Chat). Sends `message` when it is not empty (tests and screenshots). */
    void openAiChat (const juce::String& message = {});

    /** Opens the first clip of the first track in the note editor (screenshots). */
    void openFirstClipInEditor();

    /** Opens the slicer for a loop on the selected track (also used for screenshots). */
    void showSlicer (const juce::File& loop) { openSlicer (loop); }

    /** For screenshots without an audio device: pretend the playhead is here. */
    void setPreviewPosition (double ticks, bool playing);

    juce::ApplicationCommandManager& commandManager() noexcept { return commands; }
    /** Puts back the shortcuts that the user changed. Call after all commands are registered. */
    void restoreShortcuts();
    /** The Keyboard Shortcuts window's content (the caller owns it). */
    std::unique_ptr<juce::Component> makeShortcutEditor();
    Session& getSession() noexcept { return session; }
    ScoreView& scoreView() noexcept { return score; }
    ClipGridView& clipGridView() noexcept { return clipGrid; }
    Arrangement& timeline() noexcept { return arrangement; }
    TimelineGeometry& timelineGeometry() noexcept { return geometry; }
    PluginWindows& pluginWindowList() noexcept { return pluginWindows; }

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
    void importMusicXml();
    void importAudio();
    void scanForPlugins();
    void showPlugins (TrackId track, juce::Component* anchor);
    void exportMusicXml();
    void openStyle();
    void chooseStyleToFill();
    void askHowToFill (const juce::File& styleFile);
    void saveStyle();
    void chooseSoundFont();
    void loadKitSamples();
    void sliceLoop();
    void openSlicer (const juce::File&);
    void showAudioSettings();
    void showShortcuts();
    void loadDeviceProfiles();
    static juce::File deviceFolder();
    void quantise();
    bool editorHasFocus() const;
    void copyOrCut (bool cut);
    void copyAiSetup();
    void showAiChat();
    void chooseStyleFolder();
    std::unique_ptr<McpService> aiService;
    std::unique_ptr<AssistantChat> aiChat;
    juce::Component::SafePointer<juce::DocumentWindow> chatWindow;
    void paste();

    Engine& engine;
    platform::JuceDeviceHost& devices;
    juce::PropertiesFile* settings;

    PluginHost* pluginHost;
    Session session { engine, devices };
    PluginWindows pluginWindows { session };
    LiveState live;
    bool previewMode = false;
    int view = viewTimeline;
    bool mixerShown = true;
    int mixerHeight = 280;
    int mixerHeightAtDragStart = 0;
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
    /** The bar between the view and the mixer: drag it to change the mixer's height. */
    struct ResizeBar final : juce::Component
    {
        ResizeBar() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { if (onStart) onStart(); }
        void mouseDrag (const juce::MouseEvent& e) override { if (onDrag) onDrag (e.getDistanceFromDragStartY()); }
        void mouseDoubleClick (const juce::MouseEvent&) override { if (onReset) onReset(); }
        std::function<void()> onStart, onReset;
        std::function<void (int)> onDrag;
    };
    ResizeBar mixerBar;
    BeatView beat { session };
    ScoreView score { session };
    ClipGridView clipGrid { session };
    PianoRoll pianoRoll { session };
    Inspector inspector { session, live };
    StatusBar statusBar { live };

    juce::Component::SafePointer<juce::DialogWindow> slicerWindow;
    juce::Rectangle<int> mainCard, editorCard, inspectorCard, mixerCard;
};

} // namespace arranger::ui

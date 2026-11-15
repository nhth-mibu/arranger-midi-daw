#pragma once

#include <map>
#include "Theme.h"
#include "../Session.h"

namespace arranger::ui
{

/**
    The Clip grid: one column for each track, one row for each scene. A slot
    holds a clip that loops when you launch it. A scene launches a full row.
    While a slot plays, the track's Timeline clips are silent.
*/
class ClipGridView final : public juce::Component, private juce::ChangeListener
{
public:
    explicit ClipGridView (Session&);
    ~ClipGridView() override;

    /** Moves the progress bars; call often while showing. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /**
        Puts an audio file in a slot as a loop. If the track is not an audio
        track, the app makes a new audio track for it. The loop follows the
        song tempo when its length fits a number of whole bars.
    */
    bool addAudioToSlot (const juce::File&, TrackId, int scene);

    /** Resize the slot grid in one direction. */
    void zoomHorizontally (double factor);
    void zoomVertically (double factor);

    /** Cut, Copy and Paste on the slot that was clicked last (also an empty slot, for Paste). */
    bool hasChosenSlot() const;
    void copySlot (bool cut);
    void pasteSlot();

private:
    /** Where a point is: `track` -1 = the scene column; `scene` -1 = track names, sceneCount = the stop row. */
    struct Hit
    {
        int track = -2, scene = -2;
        bool onButton = false; ///< on the play/stop button at the left of a cell
        bool valid() const { return track > -2 && scene > -2; }
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void syncControls();
    int columnWidth() const;
    int rowHeight() const;
    int visibleScenes() const;
    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> cell (int track, int scene) const;
    Hit hitTest (juce::Point<int>) const;
    void clampScroll();

    void drawSlot (juce::Graphics&, juce::Rectangle<float>, const Track&, int scene, const SlotStatus&);
    void newClip (TrackId, int scene);
    void audioSlotMenu (TrackId, int scene);
    void chooseAudioFor (TrackId, int scene);
    const std::vector<float>& slotPeaks (const std::string& file);
    void slotMenu (TrackId, int scene);
    void sceneMenu (int scene);
    void renameScene (int scene);
    void renameSlot (TrackId, int scene);
    void startRename (juce::Rectangle<int>, const juce::String& text, std::function<void (std::string)> done);

    static constexpr int kCutItem = 9001, kCopyItem = 9002, kPasteItem = 9003;
    void addClipboardItems (juce::PopupMenu&, bool filled) const;
    TrackId chosenTrack = 0; ///< the slot clicked last
    int chosenScene = -1;

    Session& session;
    juce::Label title, launchLabel;
    juce::ComboBox launchBox;
    juce::TextButton recordButton { "Record to Timeline" }, stopAllButton { "Stop all" }, chordButton;
    juce::TextButton fromTimelineButton { "From Timeline..." };
    std::unique_ptr<juce::AlertWindow> fromTimelineWindow;
    void fillFromTimeline();
    void chordMenu();
    std::unique_ptr<juce::TextEditor> nameEditor;
    std::unique_ptr<juce::FileChooser> chooser;
    std::map<std::string, std::vector<float>> peaks; ///< a small picture of each audio file in a slot
    int scrollX = 0, scrollY = 0;
    float zoomX = 1.0f, zoomY = 1.0f;
    bool blinkOn = false;

    static constexpr int kHeader = 52, kNames = 36, kSceneColumn = 132, kRow = 46, kStopRow = 36;
};

} // namespace arranger::ui

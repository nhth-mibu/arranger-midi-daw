#pragma once

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
    int visibleScenes() const;
    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> cell (int track, int scene) const;
    Hit hitTest (juce::Point<int>) const;
    void clampScroll();

    void drawSlot (juce::Graphics&, juce::Rectangle<float>, const Track&, int scene, const SlotStatus&);
    void newClip (TrackId, int scene);
    void slotMenu (TrackId, int scene);
    void sceneMenu (int scene);
    void renameScene (int scene);
    void renameSlot (TrackId, int scene);
    void startRename (juce::Rectangle<int>, const juce::String& text, std::function<void (std::string)> done);

    Session& session;
    juce::Label title, launchLabel;
    juce::ComboBox launchBox;
    juce::TextButton recordButton { "Record to Timeline" }, stopAllButton { "Stop all" };
    std::unique_ptr<juce::TextEditor> nameEditor;
    int scrollX = 0, scrollY = 0;
    bool blinkOn = false;

    static constexpr int kHeader = 52, kNames = 36, kSceneColumn = 132, kRow = 46, kStopRow = 36;
};

} // namespace arranger::ui

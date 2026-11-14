#pragma once

#include "Theme.h"
#include "../Session.h"

namespace arranger::ui
{

/**
    Note editor for the clip open in the editor (double-click a clip).

    Click on empty space to draw a note, drag a note to move it, drag its right
    edge to change its length, drag on empty space to select several. The lane
    at the bottom sets velocity. Notes snap to the grid; hold Alt (Option) to
    place freely.
*/
class PianoRoll final : public juce::Component,
                        public juce::SettableTooltipClient,
                        private juce::ChangeListener
{
public:
    explicit PianoRoll (Session&);
    ~PianoRoll() override;

    /** Follow the playhead while playing. */
    void refresh();

    void deleteSelected();
    void selectAll();
    /** Cut or Copy the selected notes to the clipboard. */
    void copySelected (bool cut);
    /** Pastes notes from the clipboard at a song position (the playhead, or the clip start when the playhead is outside the clip). */
    void paste (Tick at = -1);
    /** Quantises the selected notes, or all notes when none are selected. */
    void quantiseNotes();
    /** Moves the selection by semitones (arrow keys). */
    void transpose (int semitones);

    bool hasNoteSelection() const noexcept { return ! selected.empty(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    ClipPtr clip() const;
    void frameClip();

    juce::Rectangle<int> keysArea() const;
    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> velocityArea() const;
    juce::Rectangle<int> toolbarArea() const;

    float tickToX (double absTick) const noexcept;
    double xToTick (float x) const noexcept;
    float pitchToY (int pitch) const noexcept;
    int yToPitch (float y) const noexcept;
    juce::Rectangle<float> noteBounds (const Clip&, const Note&) const;

    Tick snap (double absTick, const juce::ModifierKeys&) const;
    Tick gridStep() const;
    int noteAt (juce::Point<float>, bool* rightEdge = nullptr) const;
    bool isSelected (const Note&) const;

    /** Applies a change to the edited clip's notes as one undo step. */
    void showMenu (Tick at);
    void changeNotes (const std::string& what, const std::function<void (std::vector<Note>&)>&, const std::string& mergeKey = {});

    Session& session;
    ClipId shownClip = 0;
    bool needsFrame = false;
    std::vector<Note> selected;  ///< by value; notes have no ids

    double pixelsPerTick = 0.06;
    double scrollTicks = 0.0;
    float noteHeight = 12.0f;
    float scrollPitch = 84.0f;   ///< pitch at the top edge

    enum class Drag { none, move, resize, marquee, velocity, draw };
    Drag drag = Drag::none;
    Note dragNote;               ///< the note under the mouse when the drag started
    std::vector<Note> dragOriginal;
    Tick lastLength = kTicksPerQuarter / 4;
    juce::Rectangle<float> marquee;
    int lastPreviewPitch = -1;

    juce::ComboBox gridBox;
    juce::TextButton quantiseButton { "Quantise" }, closeButton { "Close" };
    int shownPlayheadX = -1;
};

} // namespace arranger::ui

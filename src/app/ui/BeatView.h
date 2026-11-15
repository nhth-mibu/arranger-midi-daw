#pragma once

#include "Theme.h"
#include "../Session.h"

namespace arranger::ui
{

/**
    The Beat view: a step sequencer for the selected track. Rows are the pads
    of the track's kit (or General MIDI drums), columns are steps. It edits an
    ordinary clip whose first part repeats, so the same pattern shows in the
    Timeline and the note editor.
*/
class BeatView final : public juce::Component, private juce::ChangeListener
{
public:
    explicit BeatView (Session&);
    ~BeatView() override;

    /** Moves the playhead column; call often while showing. */
    void refresh();

    /** The clip being edited (0 if the track has none yet). */
    ClipId patternClip() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Cut or Copy the notes of the pattern (or of one row) to the clipboard. Cut clears them. */
    void copyPattern (bool cut, int row = -1);
    /** Pastes notes into the pattern from its start, in place of the notes there. With a row, all notes go on that row. */
    void pastePattern (int row = -1);

private:
    void showRowMenu (int row);
    struct Row
    {
        int note = 36;
        juce::String name;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void syncControls();
    std::vector<Row> rows() const;
    ClipPtr clip() const;
    Tick patternLength (const Clip&) const;
    Tick stepTicks() const;
    /** A long clip shows four bars at a time: the part with the playhead. */
    Tick windowStart() const;
    Tick windowLength (const Clip&) const;
    void moveWindow (int direction);
    int stepCount() const;
    int cellSize() const;
    juce::Point<int> gridOrigin() const;
    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<float> cell (int row, int step) const;
    bool hit (juce::Point<int>, int& row, int& step) const;
    int velocityAt (const Clip&, int note, int step) const; ///< 0 = no note
    void setStep (int row, int step, int velocity);
    void newPattern();
    void setBars (int bars);

    Session& session;
    juce::ComboBox stepBox, barsBox;
    juce::TextButton earlierButton { "<" }, laterButton { ">" };
    juce::TextButton newButton { "New pattern" }, clearButton { "Clear" }, placeButton { "Put in bars..." };
    std::unique_ptr<juce::AlertWindow> placeWindow;
    void placePattern();
    juce::Label title;
    int shownStep = -1;
    Tick shownWindow = 0;
    int paintVelocity = 0;   ///< while dragging: the value being painted (0 erases)
    bool painting = false;
    int stepsPerBeat = 4;

    static constexpr int kHeader = 52, kNames = 148;
};

} // namespace arranger::ui

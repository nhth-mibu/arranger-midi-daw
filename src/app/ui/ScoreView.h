#pragma once

#include "Theme.h"
#include "../Session.h"

#include "arranger/core/ScoreLayout.h"
#include "arranger/core/SongEdit.h"

namespace arranger::ui
{

/**
    The Score view: the selected track as printed music, on pages. The
    notes are quantised for display only; the song does not change. The same
    pages go to a PDF file for printing.
*/
class ScoreView final : public juce::Component, private juce::ChangeListener
{
public:
    explicit ScoreView (Session&);
    ~ScoreView() override;

    /** Writes the score of the selected track to a PDF file. */
    bool exportPdf (const juce::File&);

    /** The pages as they are now (for tests and the screenshot tool). */
    const score::Layout& currentLayout();

    // ---- editing ---------------------------------------------------------------
    /** Note input: a click on a staff, a letter key or a key of the MIDI keyboard writes a note. */
    void setNoteInput (bool);
    bool noteInput() const noexcept { return inputOn; }
    void deleteSelected();
    void selectAll();
    /** Puts a chord symbol, lyric, dynamic or articulation at the selected notes (or at the input line). */
    void addMarkAtSelection (ScoreMark::Kind kind, const std::string& text) { addMark (kind, text); }
    bool hasSelection() const noexcept { return ! selection.empty(); }

    bool keyPressed (const juce::KeyPress&) override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuild();
    void choosePdf();
    void print();
    juce::String partTitle() const;

    class Pages;
    friend class Pages;

    void pageClicked (int page, float x, float y, const juce::ModifierKeys&);
    void pageDragged (int page, float y);
    void pageHover (int page, float x, float y);
    edit::NoteKey keyOf (int source) const;
    void setSelection (std::vector<edit::NoteKey>);
    void changeSelection (const std::string& name, const std::function<void (Note&)>&, const std::string& mergeKey = {});
    void enterNote (Tick at, int pitch, int velocity, bool advance);
    std::vector<Tick> markTicks() const;
    void showMarkMenu();
    void askText (ScoreMark::Kind);
    void addMark (ScoreMark::Kind, const std::string& text);
    void stepNote (int pitch, int velocity);
    Tick inputLength() const;
    int stepPitch (int pitch, int steps) const;
    TrackId scoreTrack() const;

    Session& session;
    juce::TextButton inputButton { "Write notes" }, dotButton { "." }, markButton { "Add..." };
    juce::ComboBox valueBox;
    std::vector<notation::NoteRef> refs;      ///< where each note of the score comes from
    std::vector<edit::NoteKey> selection;
    bool inputOn = false;
    Tick cursor = 0;                          ///< note input: the next note goes here
    int cursorStaff = 0;
    Tick lastStepStart = 0;
    int keysHeld = 0;
    int lastPitch = 60;
    int dragStartPosition = 0, dragSteps = 0;
    bool dragging = false;
    struct Ghost { int page = -1; float x = 0, y = 0; } ghost;
    int keyFifths = 0;
    bool drums = false;
    juce::Label title, info;
    juce::ComboBox stavesBox;
    juce::TextButton zoomOut { "-" }, zoomIn { "+" }, pdfButton { "Export PDF..." }, printButton { "Print..." };
    juce::Viewport viewport;
    std::unique_ptr<Pages> pages;
    std::unique_ptr<juce::FileChooser> chooser;

    score::Layout layout;
    std::vector<juce::Path> ink; ///< everything printed on each page, in spaces
    std::uint64_t builtRevision = ~std::uint64_t {};
    TrackId builtTrack = 0;
    int builtStaves = -1;
    Tick builtRoom = -1;
    float zoom = 1.0f;
};

/** Turns a page of the layout into one filled outline, in staff spaces. */
juce::Path scorePagePath (const score::Page&);

} // namespace arranger::ui

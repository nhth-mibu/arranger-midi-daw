#pragma once

#include "Theme.h"
#include "../Session.h"

#include "arranger/core/ScoreLayout.h"

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
    Session& session;
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
    float zoom = 1.0f;
};

/** Turns a page of the layout into one filled outline, in staff spaces. */
juce::Path scorePagePath (const score::Page&);

} // namespace arranger::ui

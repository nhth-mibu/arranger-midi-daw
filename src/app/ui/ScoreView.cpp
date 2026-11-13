#include "ScoreView.h"

#include "BinaryData.h"

#include "arranger/core/Notation.h"
#include "arranger/core/Pdf.h"

namespace arranger::ui
{

namespace
{
    juce::Typeface::Ptr bravura()
    {
        static auto tf = juce::Typeface::createSystemTypefaceFor (BinaryData::Bravura_otf, BinaryData::Bravura_otfSize);
        return tf;
    }

    juce::Font textFont (const score::Text& t)
    {
        juce::FontOptions o;
        if (t.serif)
            o = juce::FontOptions (juce::Font::getDefaultSerifFontName(), t.size, t.style == score::Text::Style::bold ? juce::Font::bold
                                                                                  : (t.style == score::Text::Style::italic ? juce::Font::italic : juce::Font::plain));
        return juce::Font (o.withPointHeight (t.size));
    }

    constexpr float kPaperGap = 3.0f; // spaces between pages on screen
}

juce::Path scorePagePath (const score::Page& page)
{
    juce::Path path;
    const juce::Font music (juce::FontOptions (bravura()).withPointHeight (4.0f));
    std::map<std::pair<char32_t, float>, juce::Path> cache;
    for (const auto& g : page.glyphs)
    {
        auto& shape = cache[{ g.code, g.scale }];
        if (shape.isEmpty())
        {
            juce::GlyphArrangement ga;
            const juce::juce_wchar text[] = { static_cast<juce::juce_wchar> (g.code), 0 };
            ga.addLineOfText (music.withPointHeight (4.0f * g.scale), juce::String (juce::CharPointer_UTF32 (reinterpret_cast<const juce::CharPointer_UTF32::CharType*> (text))), 0.0f, 0.0f);
            ga.createPath (shape);
        }
        path.addPath (shape, juce::AffineTransform::translation (g.x, g.y));
    }
    for (const auto& l : page.lines)
    {
        if (l.y1 == l.y2 || l.x1 == l.x2)
        {
            // Straight lines as rectangles: crisp, and simple in the PDF.
            const float x1 = std::min (l.x1, l.x2) - (l.x1 == l.x2 ? l.width / 2.0f : 0.0f);
            const float x2 = std::max (l.x1, l.x2) + (l.x1 == l.x2 ? l.width / 2.0f : 0.0f);
            const float y1 = std::min (l.y1, l.y2) - (l.y1 == l.y2 ? l.width / 2.0f : 0.0f);
            const float y2 = std::max (l.y1, l.y2) + (l.y1 == l.y2 ? l.width / 2.0f : 0.0f);
            path.addRectangle (x1, y1, x2 - x1, y2 - y1);
        }
        else
            path.addLineSegment ({ l.x1, l.y1, l.x2, l.y2 }, l.width);
    }
    for (const auto& s : page.shapes)
    {
        if (s.points.size() < 3)
            continue;
        path.startNewSubPath (s.points[0].first, s.points[0].second);
        for (std::size_t i = 1; i < s.points.size(); ++i)
            path.lineTo (s.points[i].first, s.points[i].second);
        path.closeSubPath();
    }
    for (const auto& t : page.texts)
    {
        juce::GlyphArrangement ga;
        const auto font = textFont (t);
        const auto text = juce::String::fromUTF8 (t.text.c_str());
        ga.addLineOfText (font, text, 0.0f, 0.0f);
        const float w = ga.getBoundingBox (0, -1, true).getWidth();
        const float x = t.align == score::Text::Align::left ? t.x : (t.align == score::Text::Align::centre ? t.x - w / 2.0f : t.x - w);
        juce::Path p;
        ga.createPath (p);
        path.addPath (p, juce::AffineTransform::translation (x, t.y));
    }
    path.setUsingNonZeroWinding (true);
    return path;
}

// ---------------------------------------------------------------------------
class ScoreView::Pages final : public juce::Component
{
public:
    explicit Pages (ScoreView& v) : view (v) {}

    float scale() const { return 7.0f * view.zoom; } // pixels per space

    void updateSize()
    {
        float h = kPaperGap, w = 0.0f;
        for (const auto& p : view.layout.pages)
        {
            h += p.height + kPaperGap;
            w = std::max (w, p.width);
        }
        const int width = std::max (view.viewport.getMaximumVisibleWidth(), juce::roundToInt ((w + 2.0f * kPaperGap) * scale()));
        setSize (width, juce::roundToInt (h * scale()));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = currentTheme();
        g.fillAll (t.colour ("background"));
        const float s = scale();
        float y = kPaperGap * s;
        const auto clip = g.getClipBounds().toFloat();
        for (std::size_t i = 0; i < view.layout.pages.size(); ++i)
        {
            const auto& page = view.layout.pages[i];
            const juce::Rectangle<float> paper ((static_cast<float> (getWidth()) - page.width * s) / 2.0f, y, page.width * s, page.height * s);
            y += (page.height + kPaperGap) * s;
            if (! paper.expanded (20.0f).intersects (clip))
                continue;
            juce::DropShadow (juce::Colours::black.withAlpha (0.35f), 18, { 0, 4 }).drawForRectangle (g, paper.toNearestInt());
            g.setColour (juce::Colour (0xfffbf8f1));
            g.fillRect (paper);
            g.setColour (juce::Colour (0xff1d1b18));
            if (i < view.ink.size())
                g.fillPath (view.ink[i], juce::AffineTransform::scale (s).translated (paper.getX(), paper.getY()));
        }
    }

private:
    ScoreView& view;
};

// ---------------------------------------------------------------------------
ScoreView::ScoreView (Session& s) : session (s)
{
    session.addChangeListener (this);
    pages = std::make_unique<Pages> (*this);

    addAndMakeVisible (title);
    title.setText ("Score", juce::dontSendNotification);
    addAndMakeVisible (info);
    info.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (stavesBox);
    stavesBox.addItem ("Staves: automatic", 1);
    stavesBox.addItem ("One staff", 2);
    stavesBox.addItem ("Grand staff", 3);
    stavesBox.setSelectedId (1, juce::dontSendNotification);
    stavesBox.setTooltip ("Show the track on one staff, or on two staves (treble and bass) like piano music");
    stavesBox.onChange = [this] { rebuild(); };

    for (auto* b : { &zoomOut, &zoomIn, &pdfButton, &printButton })
        addAndMakeVisible (*b);
    zoomOut.setTooltip ("Make the pages smaller");
    zoomIn.setTooltip ("Make the pages bigger");
    zoomOut.onClick = [this] { zoom = std::max (0.5f, zoom / 1.2f); pages->updateSize(); pages->repaint(); };
    zoomIn.onClick = [this] { zoom = std::min (3.0f, zoom * 1.2f); pages->updateSize(); pages->repaint(); };
    pdfButton.setTooltip ("Save the score of this track as a PDF file");
    pdfButton.onClick = [this] { choosePdf(); };
    printButton.setTooltip ("Open the score as a PDF, ready to print");
    printButton.onClick = [this] { print(); };

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (pages.get(), false);
    viewport.setScrollBarsShown (true, false);
}

ScoreView::~ScoreView()
{
    session.removeChangeListener (this);
}

void ScoreView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (isShowing())
        rebuild();
}

void ScoreView::visibilityChanged()
{
    if (isVisible())
        rebuild();
}

juce::String ScoreView::partTitle() const
{
    return juce::String::fromUTF8 (session.song().title.c_str());
}

const score::Layout& ScoreView::currentLayout()
{
    rebuild();
    return layout;
}

void ScoreView::rebuild()
{
    const auto& song = session.song();
    const auto revision = session.doc().revision();
    const int staves = stavesBox.getSelectedId();
    if (revision == builtRevision && session.selectedTrack() == builtTrack && staves == builtStaves)
        return;
    builtRevision = revision;
    builtTrack = session.selectedTrack();
    builtStaves = staves;

    const auto* track = edit::findTrack (song, builtTrack);
    if (track == nullptr && ! song.tracks.empty())
        track = &song.tracks.front();

    layout = {};
    ink.clear();
    if (track != nullptr)
    {
        notation::Options o;
        o.drums = notation::isDrumTrack (*track);
        o.staves = staves == 2 ? notation::Options::Staves::single : (staves == 3 ? notation::Options::Staves::grand : notation::Options::Staves::automatic);
        const auto notes = notation::timelineNotes (*track);
        const auto part = notation::makePart (track->name, notes, song.contentEnd(), song.timeSignature, notation::keyFifths (song.key), o);
        score::PageSetup setup;
        setup.title = song.title;
        setup.subtitle = song.key;
        setup.bpm = song.baseTempo();
        layout = score::layout (part, setup);
        for (const auto& p : layout.pages)
            ink.push_back (scorePagePath (p));
        info.setText (juce::String::fromUTF8 (track->name.c_str()) + "  \xc2\xb7  " + juce::String (layout.pages.size())
                          + (layout.pages.size() == 1 ? " page" : " pages") + (notes.empty() ? "  \xc2\xb7  This track has no notes on the Timeline" : ""),
                      juce::dontSendNotification);
    }
    else
        info.setText ("Add a track with notes to see its score", juce::dontSendNotification);
    pages->updateSize();
    pages->repaint();
}

bool ScoreView::exportPdf (const juce::File& file)
{
    rebuild();
    if (layout.pages.empty())
        return false;
    pdf::Document doc;
    for (std::size_t i = 0; i < layout.pages.size(); ++i)
    {
        const auto& page = layout.pages[i];
        const float pt = 1.75f / 25.4f * 72.0f; // points per space
        doc.beginPage (page.width * pt, page.height * pt);
        doc.setColour (0.11f, 0.105f, 0.095f);
        const auto& path = ink[i];
        float lastX = 0, lastY = 0;
        for (juce::Path::Iterator it (path); it.next();)
        {
            using I = juce::Path::Iterator;
            switch (it.elementType)
            {
                case I::startNewSubPath: doc.moveTo (it.x1 * pt, it.y1 * pt); lastX = it.x1; lastY = it.y1; break;
                case I::lineTo: doc.lineTo (it.x1 * pt, it.y1 * pt); lastX = it.x1; lastY = it.y1; break;
                case I::quadraticTo:
                {
                    // A quadratic curve as a cubic: the control points go two thirds of the way.
                    const float c1x = lastX + (it.x1 - lastX) * 2.0f / 3.0f, c1y = lastY + (it.y1 - lastY) * 2.0f / 3.0f;
                    const float c2x = it.x2 + (it.x1 - it.x2) * 2.0f / 3.0f, c2y = it.y2 + (it.y1 - it.y2) * 2.0f / 3.0f;
                    doc.cubicTo (c1x * pt, c1y * pt, c2x * pt, c2y * pt, it.x2 * pt, it.y2 * pt);
                    lastX = it.x2; lastY = it.y2;
                    break;
                }
                case I::cubicTo: doc.cubicTo (it.x1 * pt, it.y1 * pt, it.x2 * pt, it.y2 * pt, it.x3 * pt, it.y3 * pt); lastX = it.x3; lastY = it.y3; break;
                case I::closePath: doc.closePath(); break;
            }
        }
        doc.fill();
    }
    const auto bytes = doc.finish (partTitle().toStdString());
    file.getParentDirectory().createDirectory();
    return file.replaceWithData (bytes.data(), bytes.size());
}

void ScoreView::choosePdf()
{
    const auto base = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Export PDF", base.getChildFile (juce::File::createLegalFileName (partTitle()) + ".pdf"), "*.pdf");
    juce::Component::SafePointer<ScoreView> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc) {
                              if (safe == nullptr || fc.getResult() == juce::File())
                                  return;
                              if (! safe->exportPdf (fc.getResult().withFileExtension ("pdf")))
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Can't export PDF", "The file can't be written.");
                          });
}

void ScoreView::print()
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (juce::File::createLegalFileName (partTitle()) + " (score).pdf");
    if (exportPdf (file))
        file.startAsProcess();
}

void ScoreView::paint (juce::Graphics&) {}

void ScoreView::resized()
{
    const auto& t = currentTheme();
    title.setFont (t.font (15.0f, true));
    title.setColour (juce::Label::textColourId, t.colour ("text"));
    info.setFont (t.font (13.0f));
    info.setColour (juce::Label::textColourId, t.colour ("textMuted"));

    auto r = getLocalBounds().reduced (12, 0);
    auto top = r.removeFromTop (52).withSizeKeepingCentre (r.getWidth(), 32);
    title.setBounds (top.removeFromLeft (70));
    printButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (8);
    pdfButton.setBounds (top.removeFromRight (120));
    top.removeFromRight (8);
    zoomIn.setBounds (top.removeFromRight (32));
    top.removeFromRight (4);
    zoomOut.setBounds (top.removeFromRight (32));
    top.removeFromRight (12);
    stavesBox.setBounds (top.removeFromRight (180));
    info.setBounds (top);
    viewport.setBounds (getLocalBounds().withTrimmedTop (52).reduced (1, 0).withTrimmedBottom (1));
    pages->updateSize();
}

} // namespace arranger::ui

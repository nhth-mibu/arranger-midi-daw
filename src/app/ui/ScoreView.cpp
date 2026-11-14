#include "ScoreView.h"

#include "BinaryData.h"

#include "arranger/core/ChordFinder.h"
#include "arranger/core/Notation.h"
#include "arranger/core/Pdf.h"

#include <limits>

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

    /** The paper of a page, in pixels. */
    juce::Rectangle<float> paperOf (std::size_t index) const
    {
        const float s = scale();
        float y = kPaperGap * s;
        for (std::size_t i = 0; i < view.layout.pages.size(); ++i)
        {
            const auto& page = view.layout.pages[i];
            const juce::Rectangle<float> paper ((static_cast<float> (getWidth()) - page.width * s) / 2.0f, y, page.width * s, page.height * s);
            if (i == index)
                return paper;
            y += (page.height + kPaperGap) * s;
        }
        return {};
    }

    /** The page under a point, and the point in the page's staff spaces. */
    bool locate (juce::Point<float> at, int& page, float& x, float& y) const
    {
        for (std::size_t i = 0; i < view.layout.pages.size(); ++i)
        {
            const auto paper = paperOf (i);
            if (paper.contains (at))
            {
                page = static_cast<int> (i);
                x = (at.x - paper.getX()) / scale();
                y = (at.y - paper.getY()) / scale();
                return true;
            }
        }
        return false;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        view.grabKeyboardFocus();
        int page = 0;
        float x = 0, y = 0;
        if (locate (e.position, page, x, y))
            view.pageClicked (page, x, y, e.mods);
        else
            view.setSelection ({});
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        int page = 0;
        float x = 0, y = 0;
        if (locate (e.position, page, x, y))
            view.pageDragged (page, y);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        view.dragging = false;
        view.session.endMerge();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        int page = -1;
        float x = 0, y = 0;
        if (! locate (e.position, page, x, y))
            page = -1;
        view.pageHover (page, x, y);
    }

    void mouseExit (const juce::MouseEvent&) override { view.pageHover (-1, 0, 0); }

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

            // Selected notes, the input cursor and the note under the pointer.
            const auto accent = juce::Colour (0xffc8742a);
            const auto toPixels = [&] (float px, float py) { return juce::Point<float> (paper.getX() + px * s, paper.getY() + py * s); };
            for (const auto& box : view.layout.notes)
            {
                if (box.page != static_cast<int> (i) || box.source < 0)
                    continue;
                const auto key = view.keyOf (box.source);
                if (std::find (view.selection.begin(), view.selection.end(), key) == view.selection.end())
                    continue;
                const auto c = toPixels (box.x, box.y);
                g.setColour (accent);
                g.fillEllipse (juce::Rectangle<float> (1.5f * s, 1.1f * s).withCentre (c));
                g.setColour (accent.withAlpha (0.25f));
                g.fillEllipse (juce::Rectangle<float> (2.6f * s, 2.2f * s).withCentre (c));
            }
            if (view.inputOn)
            {
                for (const auto& bar : view.layout.bars)
                {
                    if (bar.page != static_cast<int> (i) || bar.staff != view.cursorStaff || view.cursor < bar.start || view.cursor >= bar.start + bar.length)
                        continue;
                    const float cx = score::xOfTick (view.layout, bar.page, bar.staff, bar.top, view.cursor);
                    if (cx > 0.0f)
                    {
                        g.setColour (accent.withAlpha (0.8f));
                        g.fillRect (juce::Rectangle<float> (toPixels (cx - 0.5f, bar.top - 1.5f), toPixels (cx - 0.3f, bar.top + 5.5f)));
                    }
                }
                if (view.ghost.page == static_cast<int> (i))
                {
                    g.setColour (accent.withAlpha (0.55f));
                    g.fillEllipse (juce::Rectangle<float> (1.4f * s, 1.0f * s).withCentre (toPixels (view.ghost.x, view.ghost.y)));
                }
            }
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

    setWantsKeyboardFocus (true);
    for (auto* b : { &inputButton, &dotButton })
    {
        addAndMakeVisible (*b);
        b->setClickingTogglesState (true);
    }
    inputButton.setTooltip ("Note input (N): click a staff, push A to G, or play your MIDI keyboard to write notes");
    inputButton.onClick = [this] { setNoteInput (inputButton.getToggleState()); };
    dotButton.setTooltip ("Dotted note (.): one and a half times as long");
    addAndMakeVisible (valueBox);
    valueBox.addItem ("Whole note", 7);
    valueBox.addItem ("Half note", 6);
    valueBox.addItem ("Quarter note", 5);
    valueBox.addItem ("Eighth note", 4);
    valueBox.addItem ("16th note", 3);
    valueBox.addItem ("32nd note", 2);
    valueBox.setSelectedId (5, juce::dontSendNotification);
    valueBox.setTooltip ("The length of new notes (keys 2 to 7)");

    addAndMakeVisible (stavesBox);
    stavesBox.addItem ("Staves: automatic", 1);
    stavesBox.addItem ("One staff", 2);
    stavesBox.addItem ("Grand staff", 3);
    stavesBox.setSelectedId (1, juce::dontSendNotification);
    stavesBox.setTooltip ("Show the track on one staff, or on two staves (treble and bass) like piano music");
    stavesBox.onChange = [this] { rebuild(); };

    addAndMakeVisible (tracksBox);
    tracksBox.addItem ("All tracks", 1);
    tracksBox.addItem ("Selected track", 2);
    tracksBox.addItem ("Choose tracks...", 3);
    tracksBox.setSelectedId (1, juce::dontSendNotification);
    tracksBox.setTooltip ("Show all tracks as a full score, only the selected track, or the tracks you choose");
    tracksBox.onChange = [this] {
        if (tracksBox.getSelectedId() == 3)
            chooseTracks();
        else
            setShownTracks (static_cast<Tracks> (tracksBox.getSelectedId()));
    };

    chordsButton.setTooltip ("Find the chords in the notes, add chord symbols, and show guitar chords");
    chordsButton.onClick = [this] { showChordMenu(); };
    addAndMakeVisible (chordsButton);
    markButton.setTooltip ("Add a chord symbol (K), lyrics (L), a dynamic or an articulation at the selected note");
    markButton.onClick = [this] { showMarkMenu(); };
    for (auto* b : { &zoomOut, &zoomIn, &pdfButton, &printButton, &markButton })
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
    session.onStepNote = nullptr;
    session.removeChangeListener (this);
}

void ScoreView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (isVisible())
        rebuild();
}

void ScoreView::visibilityChanged()
{
    if (isVisible())
        rebuild();
    else
        setNoteInput (false);
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

namespace
{
    /** An audio track has no notes to print. */
    bool audioOnly (const Track& t) { return t.output.kind == TrackOutput::Kind::audio; }
}

void ScoreView::setShownTracks (Tracks mode, std::vector<TrackId> chosen)
{
    tracksMode = mode;
    chosenTracks = std::move (chosen);
    tracksBox.setSelectedId (static_cast<int> (mode), juce::dontSendNotification);
    rebuild();
}

void ScoreView::chooseTracks()
{
    // A menu with a tick at each track. It stays the same list until you choose again.
    auto now = tracksMode == Tracks::chosen ? chosenTracks : tracksToShow();
    juce::PopupMenu menu;
    int item = 1;
    std::vector<TrackId> ids;
    for (const auto& t : session.song().tracks)
    {
        if (audioOnly (t))
            continue;
        ids.push_back (t.id);
        menu.addItem (item++, juce::String::fromUTF8 (t.name.c_str()), true, std::find (now.begin(), now.end(), t.id) != now.end());
    }
    juce::Component::SafePointer<ScoreView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (tracksBox), [safe, ids, now] (int result) mutable {
        if (safe == nullptr)
            return;
        if (result > 0)
        {
            const auto id = ids[static_cast<std::size_t> (result - 1)];
            if (const auto found = std::find (now.begin(), now.end(), id); found != now.end())
                now.erase (found);
            else
                now.push_back (id);
        }
        else if (safe->tracksMode != Tracks::chosen)
        {
            safe->tracksBox.setSelectedId (static_cast<int> (safe->tracksMode), juce::dontSendNotification);
            return;
        }
        safe->setShownTracks (Tracks::chosen, std::move (now));
    });
}

std::vector<TrackId> ScoreView::tracksToShow() const
{
    const auto& song = session.song();
    std::vector<TrackId> ids;
    if (tracksMode == Tracks::selected)
    {
        if (const auto id = scoreTrack(); id != 0)
            ids.push_back (id);
        return ids;
    }
    for (const auto& t : song.tracks) // in the order of the Timeline
        if (! audioOnly (t) && (tracksMode == Tracks::all || std::find (chosenTracks.begin(), chosenTracks.end(), t.id) != chosenTracks.end()))
            ids.push_back (t.id);
    return ids;
}

void ScoreView::rebuild()
{
    const auto& song = session.song();
    const auto revision = session.doc().revision();
    const int staves = stavesBox.getSelectedId();
    const Tick room = inputOn ? cursor + 1 : -1;
    const auto shown = tracksToShow();
    if (revision == builtRevision && session.selectedTrack() == builtTrack && staves == builtStaves && room == builtRoom && shown == builtTracks)
        return;
    builtRoom = room;
    builtRevision = revision;
    builtTrack = session.selectedTrack();
    builtStaves = staves;
    builtTracks = shown;
    stavesBox.setEnabled (shown.size() <= 1);

    layout = {};
    ink.clear();
    refs.clear();
    staffTrack.clear();
    keyFifths = notation::keyFifths (song.key);
    const TrackId active = scoreTrack();
    if (const auto* t = edit::findTrack (song, active))
        drums = notation::isDrumTrack (*t);

    // Room to write: one empty bar after the last note while note input is on.
    const Tick end = std::max (song.contentEnd() + (inputOn ? song.timeSignature.ticksPerBar() : 0), cursor + 1);
    std::vector<notation::Part> parts;
    std::vector<int> offsets;
    bool anyNotes = false;
    for (const auto id : shown)
    {
        const auto* track = edit::findTrack (song, id);
        if (track == nullptr)
            continue;
        notation::Options o;
        o.drums = notation::isDrumTrack (*track);
        if (shown.size() == 1)
            o.staves = staves == 2 ? notation::Options::Staves::single : (staves == 3 ? notation::Options::Staves::grand : notation::Options::Staves::automatic);
        const auto notes = notation::timelineNotes (*track);
        const auto trackRefs = notation::timelineNoteRefs (*track);
        anyNotes = anyNotes || ! notes.empty();
        offsets.push_back (static_cast<int> (refs.size()));
        refs.insert (refs.end(), trackRefs.begin(), trackRefs.end());
        auto part = notation::makePart (track->name, notes, end, song.timeSignature, keyFifths, o);
        part.marks = track->marks;
        staffTrack.insert (staffTrack.end(), part.staves.size(), id);
        parts.push_back (std::move (part));
    }

    if (! parts.empty())
    {
        // One track keeps its own layout. More tracks go under each other as a full score.
        const auto part = parts.size() == 1 ? parts.front() : notation::makeScore (parts, offsets);
        score::PageSetup setup;
        setup.title = song.title;
        setup.subtitle = song.key;
        setup.bpm = song.baseTempo();
        setup.guitarChords = song.guitarChords;
        layout = score::layout (part, setup);
        for (const auto& p : layout.pages)
            ink.push_back (scorePagePath (p));
        const auto* first = edit::findTrack (song, shown.front());
        const auto what = parts.size() == 1 && first != nullptr ? juce::String::fromUTF8 (first->name.c_str()) : juce::String (parts.size()) + " tracks";
        info.setText (what + "  \xc2\xb7  " + juce::String (layout.pages.size()) + (layout.pages.size() == 1 ? " page" : " pages")
                          + (anyNotes ? "" : (parts.size() == 1 ? "  \xc2\xb7  This track has no notes on the Timeline" : "  \xc2\xb7  These tracks have no notes on the Timeline")),
                      juce::dontSendNotification);
        // Note input goes to the selected track: put the input line on its first staff.
        if (cursorStaff >= static_cast<int> (staffTrack.size()) || staffTrack[static_cast<std::size_t> (cursorStaff)] != active)
            for (std::size_t i = 0; i < staffTrack.size(); ++i)
                if (staffTrack[i] == active)
                {
                    cursorStaff = static_cast<int> (i);
                    break;
                }
    }
    else
        info.setText (tracksMode == Tracks::chosen ? "Choose one or more tracks to see their score" : "Add a track with notes to see its score", juce::dontSendNotification);
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

// ---------------------------------------------------------------------------
TrackId ScoreView::scoreTrack() const
{
    const auto& song = session.song();
    if (tracksMode != Tracks::selected)
    {
        // The selected track when the score shows it; if not, the first track of the score.
        TrackId first = 0;
        for (const auto& t : song.tracks)
        {
            if (audioOnly (t) || (tracksMode == Tracks::chosen && std::find (chosenTracks.begin(), chosenTracks.end(), t.id) == chosenTracks.end()))
                continue;
            if (t.id == session.selectedTrack())
                return t.id;
            if (first == 0)
                first = t.id;
        }
        return first;
    }
    if (edit::findTrack (song, session.selectedTrack()) != nullptr)
        return session.selectedTrack();
    return song.tracks.empty() ? 0 : song.tracks.front().id;
}

edit::NoteKey ScoreView::keyOf (int source) const
{
    if (source < 0 || source >= static_cast<int> (refs.size()))
        return {};
    const auto& ref = refs[static_cast<std::size_t> (source)];
    const auto clip = edit::findClipPtr (session.song(), ref.clip);
    if (clip == nullptr || ref.index >= clip->notes.size())
        return {};
    return { ref.clip, clip->notes[ref.index] };
}

void ScoreView::setSelection (std::vector<edit::NoteKey> keys)
{
    selection = std::move (keys);
    pages->repaint();
}

void ScoreView::selectAll()
{
    rebuild();
    std::vector<edit::NoteKey> all;
    for (int i = 0; i < static_cast<int> (refs.size()); ++i)
        if (const auto key = keyOf (i); key.clip != 0 && std::find (all.begin(), all.end(), key) == all.end())
            all.push_back (key);
    setSelection (std::move (all));
}

void ScoreView::changeSelection (const std::string& name, const std::function<void (Note&)>& fn, const std::string& mergeKey)
{
    if (selection.empty())
        return;
    auto keys = selection;
    session.edit (name, [&] (Song& song) { keys = edit::changeNotes (song, keys, fn); }, mergeKey);
    selection = std::move (keys);
    rebuild();
    pages->repaint();
}

void ScoreView::deleteSelected()
{
    if (selection.empty())
        return;
    const auto keys = selection;
    session.edit ("Delete Notes", [&] (Song& song) { edit::removeNotes (song, keys); });
    setSelection ({});
}

void ScoreView::copySelected (bool cut)
{
    if (selection.empty())
        return;
    session.clipboard = clipboard::copyNotes (session.song(), selection);
    if (cut)
        deleteSelected();
}

void ScoreView::paste (TrackId track, Tick at)
{
    const auto content = session.clipboard;
    if (content.kind != clipboard::Content::Kind::notes)
        return;
    if (track == 0)
        track = scoreTrack();
    if (at < 0)
        at = inputOn ? cursor : static_cast<Tick> (session.position());
    std::vector<edit::NoteKey> keys;
    session.edit ("Paste Notes", [&] (Song& s) { keys = clipboard::pasteNotes (s, content, track, at); });
    setSelection (std::move (keys));
}

Tick ScoreView::inputLength() const
{
    const int id = valueBox.getSelectedId();
    const Tick base = (kTicksPerQuarter * 4) >> (7 - std::clamp (id, 2, 7));
    return dotButton.getToggleState() ? base * 3 / 2 : base;
}

/** A pitch moved by steps of the key's scale. A note outside the scale goes to the next scale note. Drums move by one drum. */
int ScoreView::stepPitch (int pitch, int steps) const
{
    if (drums)
        return std::clamp (pitch + steps, 0, 127);
    const int tonic = ((keyFifths * 7) % 12 + 12) % 12;
    const auto inScale = [&] (int p) {
        const int degree = ((p - tonic) % 12 + 12) % 12;
        return degree == 0 || degree == 2 || degree == 4 || degree == 5 || degree == 7 || degree == 9 || degree == 11;
    };
    const int dir = steps > 0 ? 1 : -1;
    for (int n = std::abs (steps); n > 0; --n)
    {
        do
            pitch += dir;
        while (! inScale (pitch) && pitch > 0 && pitch < 127);
    }
    return std::clamp (pitch, 0, 127);
}

void ScoreView::setNoteInput (bool on)
{
    if (inputOn == on)
        return;
    inputOn = on;
    inputButton.setToggleState (on, juce::dontSendNotification);
    ghost.page = -1;
    keysHeld = 0;
    if (on)
    {
        juce::Component::SafePointer<ScoreView> safe (this);
        session.onStepNote = [safe] (int pitch, int velocity) {
            if (safe != nullptr)
                safe->stepNote (pitch, velocity);
        };
        // Start after the selected note, or at the playhead's bar.
        if (! selection.empty())
        {
            const auto clip = edit::findClipPtr (session.song(), selection.back().clip);
            cursor = clip != nullptr ? clip->start + selection.back().note.end() : 0;
        }
        else
        {
            const Tick bar = session.song().timeSignature.ticksPerBar();
            cursor = (static_cast<Tick> (session.position()) / bar) * bar;
        }
    }
    else
        session.onStepNote = nullptr;
    rebuild();
    pages->repaint();
}

void ScoreView::enterNote (Tick at, int pitch, int velocity, bool advance)
{
    const TrackId track = scoreTrack();
    if (track == 0)
        return;
    const Tick length = inputLength();
    edit::NoteKey key;
    session.edit ("Add Note", [&] (Song& song) { key = edit::addTimelineNote (song, track, at, length, pitch, velocity); });
    if (key.clip == 0)
        return;
    lastPitch = pitch;
    lastStepStart = at;
    if (advance)
        cursor = at + length;
    selection = { key };
    session.previewNote (track, pitch, velocity);
    rebuild();
    pages->repaint();
}

void ScoreView::stepNote (int pitch, int velocity)
{
    if (! inputOn || ! isVisible())
        return;
    if (velocity == 0)
    {
        keysHeld = std::max (0, keysHeld - 1);
        return;
    }
    // A key that goes down while others are held adds to the chord.
    const bool chord = keysHeld > 0;
    ++keysHeld;
    enterNote (chord ? lastStepStart : cursor, pitch, 90, ! chord);
}

std::vector<Tick> ScoreView::markTicks() const
{
    std::vector<Tick> ticks;
    for (const auto& key : selection)
        if (const auto clip = edit::findClipPtr (session.song(), key.clip))
            ticks.push_back (clip->start + key.note.start);
    if (ticks.empty() && inputOn)
        ticks.push_back (cursor);
    std::sort (ticks.begin(), ticks.end());
    ticks.erase (std::unique (ticks.begin(), ticks.end()), ticks.end());
    return ticks;
}

void ScoreView::addMark (ScoreMark::Kind kind, const std::string& text)
{
    const auto ticks = markTicks();
    const TrackId track = scoreTrack();
    if (ticks.empty() || track == 0)
        return;
    if (kind == ScoreMark::Kind::lyric)
    {
        const auto* t = edit::findTrack (session.song(), track);
        std::vector<Tick> starts;
        if (t != nullptr)
            for (const auto& n : notation::timelineNotes (*t))
                starts.push_back (n.start);
        session.edit ("Add Lyrics", [&] (Song& song) {
            if (text.empty())
                edit::setMark (song, track, { kind, ticks.front(), {} });
            else
                edit::setLyrics (song, track, ticks.front(), text, starts);
        });
    }
    else
        session.edit (kind == ScoreMark::Kind::chord ? "Add Chord Symbol" : (kind == ScoreMark::Kind::dynamic ? "Add Dynamic" : "Add Articulation"), [&] (Song& song) {
            // A chord symbol or a dynamic goes at the first selected note; an articulation goes on each one.
            for (const Tick at : ticks)
            {
                edit::setMark (song, track, { kind, at, text });
                if (kind != ScoreMark::Kind::articulation)
                    break;
            }
        });
    rebuild();
    pages->repaint();
}

void ScoreView::askText (ScoreMark::Kind kind)
{
    const auto ticks = markTicks();
    if (ticks.empty())
    {
        info.setText ("Select a note first", juce::dontSendNotification);
        return;
    }
    juce::String current;
    if (const auto* t = edit::findTrack (session.song(), scoreTrack()))
        for (const auto& m : t->marks)
            if (m.kind == kind && m.at == ticks.front())
                current = juce::String::fromUTF8 (m.text.c_str());
    const bool chord = kind == ScoreMark::Kind::chord;
    auto* window = new juce::AlertWindow (chord ? "Chord symbol" : "Lyrics",
                                          chord ? "Write the chord, for example Cm7 or Bb/D. Leave it empty to remove the chord."
                                                : "Write the words. Each word goes under one note, from the selected note on. Put a hyphen in a word to divide it: Twin-kle.",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("text", current);
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<ScoreView> safe (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window, kind] (int result) {
        if (result == 1 && safe != nullptr)
            safe->addMark (kind, window->getTextEditorContents ("text").trim().toStdString());
    }), true);
}

void ScoreView::findChords (Tick step)
{
    const auto& song = session.song();
    const auto shown = tracksToShow();
    std::vector<const Track*> tracks;
    TrackId target = 0;
    for (const auto id : shown)
        if (const auto* t = edit::findTrack (song, id))
        {
            tracks.push_back (t);
            if (target == 0)
                target = id; // chord symbols go above the top staff
        }
    if (target == 0)
        return;
    const Tick bar = song.timeSignature.ticksPerBar();
    Tick from = 0, to = std::max (song.contentEnd(), bar);
    if (! selection.empty())
    {
        Tick first = std::numeric_limits<Tick>::max(), last = 0;
        for (const auto& k : selection)
            if (const auto clip = edit::findClipPtr (song, k.clip))
            {
                first = std::min (first, clip->start + k.note.start);
                last = std::max (last, clip->start + k.note.end());
            }
        if (last > first)
        {
            from = (first / bar) * bar;
            to = ((last + bar - 1) / bar) * bar;
        }
    }
    const auto marks = chords::find (tracks, song.timeSignature, from, to, step);
    session.edit ("Find Chords", [&] (Song& s) {
        if (auto* t = edit::findTrack (s, target))
            chords::replaceChords (*t, from, to, marks);
    });
    info.setText (marks.empty() ? juce::String ("No chords found: the notes make no chord")
                                : juce::String (static_cast<int> (marks.size())) + (marks.size() == 1 ? " chord found" : " chords found"),
                  juce::dontSendNotification);
    rebuild();
    pages->repaint();
}

void ScoreView::setGuitarChords (bool on)
{
    if (session.song().guitarChords == on)
        return;
    session.edit (on ? "Show Guitar Chords" : "Hide Guitar Chords", [on] (Song& s) { s.guitarChords = on; });
    rebuild();
    pages->repaint();
}

void ScoreView::showChordMenu()
{
    const auto& song = session.song();
    const bool ready = ! markTicks().empty();
    const bool some = ! selection.empty();
    juce::PopupMenu find;
    find.addItem (10, "One chord a bar");
    find.addItem (11, "Up to two chords a bar");
    find.addItem (12, "Up to one chord a beat");
    juce::PopupMenu menu;
    menu.addSubMenu (some ? "Find Chords in the Selected Bars" : "Find Chords in the Notes", find);
    menu.addItem (1, "Chord Symbol at the Selected Note...  (K)", ready);
    menu.addItem (2, some ? "Remove Chords in the Selected Bars" : "Remove All Chords");
    menu.addSeparator();
    menu.addItem (3, "Show Guitar Chords", true, song.guitarChords);
    juce::Component::SafePointer<ScoreView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chordsButton), [safe] (int id) {
        if (safe == nullptr || id == 0)
            return;
        const auto& s = safe->session.song();
        const Tick bar = s.timeSignature.ticksPerBar();
        if (id >= 10 && id <= 12)
            safe->findChords (id == 10 ? bar : (id == 11 ? bar / 2 : s.timeSignature.ticksPerBeat()));
        else if (id == 1)
            safe->askText (ScoreMark::Kind::chord);
        else if (id == 3)
            safe->setGuitarChords (! s.guitarChords);
        else if (id == 2)
        {
            Tick from = 0, to = std::numeric_limits<Tick>::max();
            if (! safe->selection.empty())
            {
                Tick first = std::numeric_limits<Tick>::max(), last = 0;
                for (const auto& k : safe->selection)
                    if (const auto clip = edit::findClipPtr (s, k.clip))
                    {
                        first = std::min (first, clip->start + k.note.start);
                        last = std::max (last, clip->start + k.note.end());
                    }
                from = (first / bar) * bar;
                to = ((last + bar - 1) / bar) * bar;
            }
            const auto shown = safe->tracksToShow();
            safe->session.edit ("Remove Chords", [&] (Song& song) {
                for (const auto id : shown)
                    if (auto* t = edit::findTrack (song, id))
                        chords::replaceChords (*t, from, to, {});
            });
            safe->rebuild();
            safe->pages->repaint();
        }
    });
}

void ScoreView::showMarkMenu()
{
    juce::PopupMenu menu;
    const bool ready = ! markTicks().empty();
    menu.addItem (1, "Chord symbol...  (K)", ready);
    menu.addItem (2, "Lyrics...  (L)", ready);
    menu.addSeparator();
    static const char* const kDynamics[] = { "pp", "p", "mp", "mf", "f", "ff" };
    juce::PopupMenu dynamics;
    for (int i = 0; i < 6; ++i)
        dynamics.addItem (10 + i, kDynamics[i]);
    dynamics.addSeparator();
    dynamics.addItem (19, "Remove");
    menu.addSubMenu ("Dynamic", dynamics, ready);
    static const char* const kArticulations[] = { "staccato", "accent", "tenuto", "marcato", "fermata" };
    juce::PopupMenu articulations;
    for (int i = 0; i < 5; ++i)
        articulations.addItem (20 + i, juce::String (kArticulations[i]).substring (0, 1).toUpperCase() + juce::String (kArticulations[i]).substring (1));
    menu.addSubMenu ("Articulation", articulations, ready);
    menu.addSeparator();
    menu.addItem (30, "Remove all marks at the selected notes", ready);
    if (! ready)
    {
        menu.addSeparator();
        menu.addItem (99, "Select a note first", false);
    }
    juce::Component::SafePointer<ScoreView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&markButton), [safe] (int id) {
        if (safe == nullptr || id == 0)
            return;
        if (id == 1) safe->askText (ScoreMark::Kind::chord);
        else if (id == 2) safe->askText (ScoreMark::Kind::lyric);
        else if (id >= 10 && id < 16) safe->addMark (ScoreMark::Kind::dynamic, kDynamics[id - 10]);
        else if (id == 19) safe->addMark (ScoreMark::Kind::dynamic, {});
        else if (id >= 20 && id < 25) safe->addMark (ScoreMark::Kind::articulation, kArticulations[id - 20]);
        else if (id == 30)
        {
            const auto ticks = safe->markTicks();
            const TrackId track = safe->scoreTrack();
            safe->session.edit ("Remove Marks", [&] (Song& song) {
                for (const Tick at : ticks)
                    edit::clearMarks (song, track, at, at + 1);
            });
            safe->rebuild();
            safe->pages->repaint();
        }
    });
}

void ScoreView::pageClicked (int page, float x, float y, const juce::ModifierKeys& mods)
{
    // A notehead near the click?
    const score::NoteBox* nearest = nullptr;
    float best = 1.0f;
    for (const auto& box : layout.notes)
    {
        if (box.page != page || box.source < 0)
            continue;
        const float d = std::hypot ((box.x - x) * 0.8f, box.y - y);
        if (d < best)
        {
            best = d;
            nearest = &box;
        }
    }
    const auto hit = score::hitTest (layout, page, x, y);
    if (mods.isPopupMenu())
    {
        // Right-click: a note not yet selected becomes the selection; then the menu.
        if (nearest != nullptr)
            if (const auto key = keyOf (nearest->source); key.clip != 0 && std::find (selection.begin(), selection.end(), key) == selection.end())
                setSelection ({ key });
        TrackId track = 0;
        if (hit.valid && hit.staff >= 0 && hit.staff < static_cast<int> (staffTrack.size()))
            track = staffTrack[static_cast<std::size_t> (hit.staff)];
        const Tick at = hit.valid ? (hit.tick / (kTicksPerQuarter / 4)) * (kTicksPerQuarter / 4) : -1;
        const bool any = ! selection.empty();
        juce::PopupMenu m;
        m.addItem (1, "Cut", any);
        m.addItem (2, "Copy", any);
        m.addItem (3, "Paste Here", session.clipboard.kind == clipboard::Content::Kind::notes && at >= 0);
        m.addItem (4, "Delete", any);
        m.addSeparator();
        m.addItem (5, "Select All");
        juce::Component::SafePointer<ScoreView> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, track, at] (int r) {
            if (safe == nullptr) return;
            if (r == 1 || r == 2) safe->copySelected (r == 1);
            else if (r == 3) safe->paste (track, at);
            else if (r == 4) safe->deleteSelected();
            else if (r == 5) safe->selectAll();
        });
        return;
    }
    if (nearest != nullptr)
    {
        const auto key = keyOf (nearest->source);
        if (key.clip == 0)
            return;
        auto keys = selection;
        const auto found = std::find (keys.begin(), keys.end(), key);
        if (mods.isShiftDown() || mods.isCommandDown())
        {
            if (found != keys.end()) keys.erase (found);
            else keys.push_back (key);
        }
        else if (found == keys.end())
            keys = { key };
        if (nearest->staff >= 0 && nearest->staff < static_cast<int> (staffTrack.size()) && tracksMode != Tracks::selected)
            session.selectTrack (staffTrack[static_cast<std::size_t> (nearest->staff)]);
        setSelection (std::move (keys));
        dragging = hit.valid;
        dragStartPosition = hit.position;
        dragSteps = 0;
        session.previewNote (scoreTrack(), key.note.pitch, key.note.velocity);
        return;
    }
    if (inputOn && hit.valid)
    {
        const Tick grid = std::min<Tick> (inputLength(), kTicksPerQuarter / 4);
        const Tick tick = (hit.tick / grid) * grid;
        cursorStaff = hit.staff;
        if (hit.staff >= 0 && hit.staff < static_cast<int> (staffTrack.size()) && tracksMode != Tracks::selected)
        {
            const auto id = staffTrack[static_cast<std::size_t> (hit.staff)];
            session.selectTrack (id);
            if (const auto* t = edit::findTrack (session.song(), id))
                drums = notation::isDrumTrack (*t);
        }
        enterNote (tick, drums ? notation::drumPitchAt (hit.position) : notation::pitchAt (hit.clef, hit.position, keyFifths), 90, true);
        return;
    }
    setSelection ({});
}

void ScoreView::pageDragged (int page, float y)
{
    if (! dragging || selection.empty())
        return;
    // Use the staff the drag started on: only the height matters.
    for (const auto& box : layout.notes)
    {
        if (box.page != page || keyOf (box.source) != selection.front())
            continue;
        const int steps = static_cast<int> (std::lround ((box.y - y) * 2.0f));
        if (steps == 0)
            return;
        dragSteps += steps;
        changeSelection ("Move Notes", [&] (Note& n) { n.pitch = static_cast<std::uint8_t> (stepPitch (n.pitch, steps)); }, "score-drag");
        if (! selection.empty())
            session.previewNote (scoreTrack(), selection.front().note.pitch, selection.front().note.velocity, 120);
        return;
    }
}

void ScoreView::pageHover (int page, float x, float y)
{
    Ghost next;
    if (inputOn && page >= 0)
        if (const auto hit = score::hitTest (layout, page, x, y); hit.valid)
        {
            next.page = page;
            next.x = x;
            next.y = hit.top + 4.0f - static_cast<float> (hit.position) * 0.5f;
        }
    if (next.page != ghost.page || next.x != ghost.x || next.y != ghost.y)
    {
        ghost = next;
        pages->repaint();
    }
}

bool ScoreView::keyPressed (const juce::KeyPress& key)
{
    using KP = juce::KeyPress;
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    const auto character = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    if (mods.isCommandDown() || mods.isCtrlDown())
        return false; // menu shortcuts

    if (character == 'n')
    {
        setNoteInput (! inputOn);
        return true;
    }
    if (character == 'k' || character == 'l')
    {
        askText (character == 'k' ? ScoreMark::Kind::chord : ScoreMark::Kind::lyric);
        return true;
    }
    if (code == KP::escapeKey && (inputOn || ! selection.empty()))
    {
        if (inputOn) setNoteInput (false);
        else setSelection ({});
        return true;
    }
    if (character >= '2' && character <= '7')
    {
        valueBox.setSelectedId (character - '0', juce::dontSendNotification);
        return true;
    }
    if (character == '.')
    {
        dotButton.setToggleState (! dotButton.getToggleState(), juce::dontSendNotification);
        return true;
    }
    if (code == KP::deleteKey || code == KP::backspaceKey)
    {
        deleteSelected();
        return true;
    }
    if (code == KP::upKey || code == KP::downKey)
    {
        if (selection.empty())
            return false;
        const int dir = code == KP::upKey ? 1 : -1;
        const bool octave = mods.isShiftDown(), semitone = mods.isAltDown();
        changeSelection ("Move Notes", [&] (Note& n) {
            const int p = octave ? n.pitch + 12 * dir : (semitone ? n.pitch + dir : stepPitch (n.pitch, dir));
            n.pitch = static_cast<std::uint8_t> (std::clamp (p, 0, 127));
        }, "score-pitch");
        if (! selection.empty())
            session.previewNote (scoreTrack(), selection.front().note.pitch, selection.front().note.velocity, 150);
        return true;
    }
    if (code == KP::leftKey || code == KP::rightKey)
    {
        if (selection.empty())
            return false;
        const Tick step = (code == KP::rightKey ? 1 : -1) * (kTicksPerQuarter / 4);
        if (mods.isShiftDown())
            changeSelection ("Change Note Length", [&] (Note& n) { n.length = std::max<Tick> (kTicksPerQuarter / 8, n.length + step); }, "score-length");
        else
            changeSelection ("Move Notes", [&] (Note& n) { n.start = std::max<Tick> (0, n.start + step); }, "score-time");
        return true;
    }
    if (inputOn && character >= 'a' && character <= 'g')
    {
        // The letter in the octave nearest to the last note.
        static constexpr int kPc[7] = { 9, 11, 0, 2, 4, 5, 7 };
        const int tonic = ((keyFifths * 7) % 12 + 12) % 12;
        int pc = kPc[character - 'a'];
        // Use the key's sharp or flat for this letter.
        for (const int alter : { 0, keyFifths < 0 ? -1 : 1, keyFifths < 0 ? 1 : -1 })
        {
            const int degree = ((pc + alter - tonic) % 12 + 12) % 12;
            if (degree == 0 || degree == 2 || degree == 4 || degree == 5 || degree == 7 || degree == 9 || degree == 11)
            {
                pc = ((pc + alter) % 12 + 12) % 12;
                break;
            }
        }
        int pitch = (lastPitch / 12) * 12 + pc;
        if (pitch - lastPitch > 6) pitch -= 12;
        if (lastPitch - pitch > 6) pitch += 12;
        enterNote (cursor, std::clamp (pitch, 0, 127), 90, true);
        return true;
    }
    if (inputOn && character == '0')
    {
        cursor += inputLength(); // a rest
        rebuild();
        pages->repaint();
        return true;
    }
    return false;
}

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
    stavesBox.setBounds (top.removeFromRight (150));
    top.removeFromRight (8);
    tracksBox.setBounds (top.removeFromRight (140));
    top.removeFromRight (12);
    dotButton.setBounds (top.removeFromRight (28));
    top.removeFromRight (4);
    valueBox.setBounds (top.removeFromRight (130));
    top.removeFromRight (8);
    inputButton.setBounds (top.removeFromRight (104));
    top.removeFromRight (8);
    markButton.setBounds (top.removeFromRight (64));
    top.removeFromRight (8);
    chordsButton.setBounds (top.removeFromRight (72));
    top.removeFromRight (8);
    info.setBounds (top);
    viewport.setBounds (getLocalBounds().withTrimmedTop (52).reduced (1, 0).withTrimmedBottom (1));
    pages->updateSize();
}

} // namespace arranger::ui

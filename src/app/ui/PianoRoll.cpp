#include "PianoRoll.h"

#include "arranger/core/GeneralMidi.h"

#include <algorithm>
#include <cmath>

namespace arranger::ui
{

namespace
{
    constexpr int kToolbarHeight = 40;
    constexpr int kRulerHeight = 20;
    constexpr int kKeysWidth = 56;
    constexpr int kVelocityHeight = 72;
    constexpr float kEdgeGrab = 6.0f;

    const char* const kGridNames[] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };

    bool isBlackKey (int pitch)
    {
        const int pc = ((pitch % 12) + 12) % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    int gestureCounter = 0;

    void eraseOne (std::vector<Note>& notes, const Note& n)
    {
        if (auto it = std::find (notes.begin(), notes.end(), n); it != notes.end())
            notes.erase (it);
    }
}

PianoRoll::PianoRoll (Session& s) : session (s)
{
    session.addChangeListener (this);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (gridBox);
    int id = 1;
    for (const auto* name : kGridNames)
        gridBox.addItem (name, id++);
    gridBox.addItem ("Off", id);
    gridBox.setSelectedId (4, juce::dontSendNotification);
    gridBox.setTooltip ("Grid for drawing, moving and quantising notes");
    gridBox.onChange = [this] {
        const auto step = gridStep();
        session.snapGrid = step;
        if (step > 0)
            session.quantise.grid = step;
        repaint();
    };

    addAndMakeVisible (quantiseButton);
    quantiseButton.setTooltip ("Quantise the selected notes (all notes when none are selected)  [Q]");
    quantiseButton.onClick = [this] { quantiseNotes(); };

    addAndMakeVisible (closeButton);
    closeButton.setTooltip ("Close the note editor");
    closeButton.onClick = [this] { session.openInEditor (0); };

    changeListenerCallback (nullptr);
}

PianoRoll::~PianoRoll() { session.removeChangeListener (this); }

ClipPtr PianoRoll::clip() const { return edit::findClipPtr (session.song(), session.editedClip()); }

void PianoRoll::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const bool hasClip = clip() != nullptr;
    for (auto* c : std::initializer_list<juce::Component*> { &gridBox, &quantiseButton, &closeButton })
        c->setVisible (hasClip);

    if (session.editedClip() != shownClip)
    {
        shownClip = session.editedClip();
        selected.clear();
        needsFrame = true;
        if (gridArea().getWidth() > 100 && gridArea().getHeight() > 60)
            frameClip();
    }
    else if (const auto c = clip())
    {
        // Drop selected notes that no longer exist (undo, delete).
        std::erase_if (selected, [&] (const Note& n) { return std::find (c->notes.begin(), c->notes.end(), n) == c->notes.end(); });
    }
    repaint();
}

void PianoRoll::frameClip()
{
    const auto c = clip();
    if (c == nullptr)
        return;
    needsFrame = false;
    const auto grid = gridArea();
    const double width = std::max (200, grid.getWidth() - 40);
    pixelsPerTick = std::clamp (width / static_cast<double> (std::max<Tick> (c->length, kTicksPerQuarter * 4)), 0.01, 0.6);
    scrollTicks = static_cast<double> (c->start) - 20.0 / pixelsPerTick;

    if (c->notes.empty())
        scrollPitch = 84.0f;
    else
    {
        int lo = 127, hi = 0;
        for (const auto& n : c->notes)
        {
            lo = std::min<int> (lo, n.pitch);
            hi = std::max<int> (hi, n.pitch);
        }
        const float rows = static_cast<float> (grid.getHeight()) / noteHeight;
        const float centre = (static_cast<float> (lo + hi) + 1.0f) * 0.5f;
        scrollPitch = std::clamp (centre + rows * 0.5f, std::min (127.0f, rows), 128.0f);
    }
}

// ---- geometry ------------------------------------------------------------------
juce::Rectangle<int> PianoRoll::toolbarArea() const { return getLocalBounds().removeFromTop (kToolbarHeight); }

juce::Rectangle<int> PianoRoll::keysArea() const
{
    auto r = getLocalBounds().withTrimmedTop (kToolbarHeight + kRulerHeight).withTrimmedBottom (kVelocityHeight);
    return r.removeFromLeft (kKeysWidth);
}

juce::Rectangle<int> PianoRoll::gridArea() const
{
    return getLocalBounds().withTrimmedTop (kToolbarHeight + kRulerHeight).withTrimmedBottom (kVelocityHeight).withTrimmedLeft (kKeysWidth);
}

juce::Rectangle<int> PianoRoll::velocityArea() const
{
    return getLocalBounds().removeFromBottom (kVelocityHeight).withTrimmedLeft (kKeysWidth);
}

float PianoRoll::tickToX (double absTick) const noexcept
{
    return static_cast<float> (gridArea().getX() + (absTick - scrollTicks) * pixelsPerTick);
}

double PianoRoll::xToTick (float x) const noexcept
{
    return scrollTicks + (x - static_cast<float> (gridArea().getX())) / pixelsPerTick;
}

float PianoRoll::pitchToY (int pitch) const noexcept
{
    return static_cast<float> (gridArea().getY()) + (scrollPitch - static_cast<float> (pitch) - 1.0f) * noteHeight;
}

int PianoRoll::yToPitch (float y) const noexcept
{
    const float u = (y - static_cast<float> (gridArea().getY())) / noteHeight;
    return std::clamp (static_cast<int> (std::floor (scrollPitch - u)), 0, 127);
}

juce::Rectangle<float> PianoRoll::noteBounds (const Clip& c, const Note& n) const
{
    const float x1 = tickToX (static_cast<double> (c.start + n.start));
    const float x2 = tickToX (static_cast<double> (c.start + n.end()));
    return { x1, pitchToY (n.pitch), std::max (3.0f, x2 - x1), noteHeight };
}

Tick PianoRoll::gridStep() const
{
    const int id = gridBox.getSelectedId();
    if (id < 1 || id > static_cast<int> (std::size (kGridNames)))
        return 0;
    return QuantiseSettings::gridFromName (kGridNames[id - 1]);
}

Tick PianoRoll::snap (double absTick, const juce::ModifierKeys& mods) const
{
    const auto step = gridStep();
    if (step <= 0 || mods.isAltDown())
        return static_cast<Tick> (std::llround (absTick));
    return static_cast<Tick> (std::llround (absTick / static_cast<double> (step))) * step;
}

int PianoRoll::noteAt (juce::Point<float> p, bool* rightEdge) const
{
    const auto c = clip();
    if (c == nullptr)
        return -1;
    // Topmost (last drawn) first.
    for (int i = static_cast<int> (c->notes.size()) - 1; i >= 0; --i)
    {
        const auto r = noteBounds (*c, c->notes[static_cast<std::size_t> (i)]);
        if (r.contains (p))
        {
            if (rightEdge != nullptr)
                *rightEdge = p.x > r.getRight() - std::min (kEdgeGrab, r.getWidth() * 0.4f);
            return i;
        }
    }
    return -1;
}

bool PianoRoll::isSelected (const Note& n) const { return std::find (selected.begin(), selected.end(), n) != selected.end(); }

void PianoRoll::changeNotes (const std::string& what, const std::function<void (std::vector<Note>&)>& fn, const std::string& mergeKey)
{
    const auto id = session.editedClip();
    if (id == 0)
        return;
    session.edit (what, [&] (Song& s) {
        edit::modifyClip (s, id, [&] (Clip& c) {
            fn (c.notes);
            for (auto& n : c.notes)
                n.start = std::max<Tick> (0, n.start);
            // Grow the clip to whole bars when notes run past its end.
            Tick end = 0;
            for (const auto& n : c.notes)
                end = std::max (end, n.end());
            if (end > c.length)
            {
                const Tick bar = s.timeSignature.ticksPerBar();
                c.length = ((end + bar - 1) / bar) * bar;
            }
        });
    }, mergeKey);
}

// ---- commands -------------------------------------------------------------------------
void PianoRoll::deleteSelected()
{
    if (selected.empty())
        return;
    const auto gone = selected;
    selected.clear();
    changeNotes (gone.size() == 1 ? "Delete Note" : "Delete Notes", [&] (std::vector<Note>& notes) {
        for (const auto& n : gone)
            eraseOne (notes, n);
    });
}

void PianoRoll::selectAll()
{
    if (const auto c = clip())
        selected = c->notes;
    repaint();
}

void PianoRoll::quantiseNotes()
{
    const auto c = clip();
    if (c == nullptr)
        return;
    auto settings = session.quantise;
    if (const auto step = gridStep(); step > 0)
        settings.grid = step;

    // Quantise a copy holding only the chosen notes, then put them back.
    Clip part;
    part.start = c->start;
    part.length = c->length;
    part.notes = selected.empty() ? c->notes : selected;
    const auto before = part.notes;
    quantise (part, settings);

    changeNotes ("Quantise", [&] (std::vector<Note>& notes) {
        for (const auto& n : before)
            eraseOne (notes, n);
        notes.insert (notes.end(), part.notes.begin(), part.notes.end());
    });
    if (! selected.empty())
        selected = part.notes;
}

void PianoRoll::transpose (int semitones)
{
    if (selected.empty())
        return;
    for (const auto& n : selected)
        if (n.pitch + semitones < 0 || n.pitch + semitones > 127)
            return;
    const auto old = selected;
    for (auto& n : selected)
        n.pitch = static_cast<std::uint8_t> (n.pitch + semitones);
    changeNotes ("Transpose", [&] (std::vector<Note>& notes) {
        for (const auto& n : old)
            eraseOne (notes, n);
        notes.insert (notes.end(), selected.begin(), selected.end());
    }, "transpose");
    if (selected.size() == 1)
        session.previewNote (session.selectedTrack(), selected.front().pitch, selected.front().velocity);
}

// ---- drawing ----------------------------------------------------------------------------
void PianoRoll::refresh()
{
    const auto c = clip();
    if (c == nullptr)
        return;
    const float x = tickToX (session.position());
    const int px = juce::roundToInt (x);
    if (px == shownPlayheadX)
        return;

    // Page along with the playhead while playing.
    const auto grid = gridArea();
    if (session.isPlaying() && (x > static_cast<float> (grid.getRight() - 20) || x < static_cast<float> (grid.getX())))
    {
        scrollTicks = session.position() - 20.0 / pixelsPerTick;
        shownPlayheadX = -1;
        repaint();
        return;
    }
    const int top = kToolbarHeight;
    repaint (shownPlayheadX - 2, top, 5, getHeight() - top);
    repaint (px - 2, top, 5, getHeight() - top);
    shownPlayheadX = px;
}

void PianoRoll::resized()
{
    if (needsFrame && gridArea().getWidth() > 100 && gridArea().getHeight() > 60)
        frameClip();

    auto bar = toolbarArea().reduced (12, 6);
    closeButton.setBounds (bar.removeFromRight (64));
    bar.removeFromRight (8);
    quantiseButton.setBounds (bar.removeFromRight (84));
    bar.removeFromRight (8);
    gridBox.setBounds (bar.removeFromRight (82));
}

void PianoRoll::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    g.fillAll (t.colour ("panel"));

    const auto c = clip();
    if (c == nullptr)
    {
        g.setColour (t.colour ("textDim"));
        g.setFont (t.font (14.0f));
        g.drawText ("Double-click a clip to edit its notes", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto& song = session.song();
    const auto trackId = edit::trackOfClip (song, c->id);
    const auto* track = edit::findTrack (song, trackId);
    const auto colour = track != nullptr ? t.trackColour (track->colour) : t.colour ("accent");

    // Toolbar: colour dot, clip and track name.
    {
        auto bar = toolbarArea().reduced (14, 0);
        g.setColour (colour);
        g.fillEllipse (bar.removeFromLeft (10).withSizeKeepingCentre (10, 10).toFloat());
        bar.removeFromLeft (10);
        g.setColour (t.colour ("text"));
        g.setFont (t.font (14.0f, true));
        const auto name = juce::String::fromUTF8 (c->name.c_str());
        g.drawText (name.isEmpty() ? juce::String ("Clip") : name, bar.removeFromLeft (220), juce::Justification::centredLeft, true);
        if (track != nullptr)
        {
            g.setColour (t.colour ("textMuted"));
            g.setFont (t.font (13.0f));
            g.drawText (juce::String::fromUTF8 (track->name.c_str()) + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (c->notes.size()) + " notes",
                        bar.removeFromLeft (260), juce::Justification::centredLeft, true);
        }
        g.setColour (t.colour ("line"));
        g.fillRect (toolbarArea().removeFromBottom (1));
    }

    const auto grid = gridArea();
    auto keys = keysArea();
    auto vel = velocityArea();
    const auto ruler = juce::Rectangle<int> (grid.getX(), kToolbarHeight, grid.getWidth(), kRulerHeight);
    const auto sig = song.timeSignature;
    const Tick bar = sig.ticksPerBar(), beat = sig.ticksPerBeat();

    // Rows: black-key rows a shade darker, octave lines.
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (grid);
        const int topPitch = std::min (127, static_cast<int> (std::ceil (scrollPitch)));
        for (int p = topPitch; p >= 0; --p)
        {
            const float y = pitchToY (p);
            if (y > static_cast<float> (grid.getBottom()))
                break;
            if (isBlackKey (p))
            {
                g.setColour (t.colour ("background").withAlpha (0.55f));
                g.fillRect (static_cast<float> (grid.getX()), y, static_cast<float> (grid.getWidth()), noteHeight);
            }
            if (p % 12 == 0)
            {
                g.setColour (t.colour ("lineStrong"));
                g.fillRect (static_cast<float> (grid.getX()), y + noteHeight - 1.0f, static_cast<float> (grid.getWidth()), 1.0f);
            }
        }

        // Outside the clip: dimmed.
        const float cx1 = tickToX (static_cast<double> (c->start)), cx2 = tickToX (static_cast<double> (c->end()));
        g.setColour (t.colour ("background").withAlpha (0.45f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (static_cast<float> (grid.getX()), static_cast<float> (grid.getY()), cx1, static_cast<float> (grid.getBottom())));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (cx2, static_cast<float> (grid.getY()), static_cast<float> (grid.getRight()), static_cast<float> (grid.getBottom())));

        // Vertical lines: grid steps, beats, bars.
        const Tick step = gridStep() > 0 ? gridStep() : beat;
        const Tick first = std::max<Tick> (0, static_cast<Tick> (xToTick (static_cast<float> (grid.getX())) / static_cast<double> (step)) * step);
        const bool showSteps = static_cast<double> (step) * pixelsPerTick >= 6.0;
        for (Tick tick = first; tickToX (static_cast<double> (tick)) < static_cast<float> (grid.getRight()); tick += step)
        {
            const float x = std::floor (tickToX (static_cast<double> (tick)));
            if (tick % bar == 0)
                g.setColour (t.colour ("lineStrong"));
            else if (tick % beat == 0)
                g.setColour (t.colour ("line"));
            else if (showSteps)
                g.setColour (t.colour ("line").withAlpha (0.45f));
            else
                continue;
            g.fillRect (x, static_cast<float> (grid.getY()), 1.0f, static_cast<float> (grid.getHeight()));
        }

        // Notes
        for (const auto& n : c->notes)
        {
            const auto r = noteBounds (*c, n);
            if (! r.intersects (grid.toFloat()))
                continue;
            const bool sel = isSelected (n);
            const float shade = 0.55f + 0.45f * static_cast<float> (n.velocity) / 127.0f;
            auto fill = sel ? t.colour ("accent") : colour.withMultipliedBrightness (shade);
            g.setColour (fill);
            g.fillRoundedRectangle (r.reduced (0.0f, 1.0f), 2.5f);
            g.setColour (sel ? t.colour ("text") : fill.darker (0.5f));
            g.drawRoundedRectangle (r.reduced (0.5f, 1.5f), 2.5f, sel ? 1.2f : 0.8f);
            if (r.getWidth() > 30.0f && noteHeight >= 11.0f)
            {
                g.setColour (t.colour ("accentText").withAlpha (0.85f));
                g.setFont (t.font (std::min (11.0f, noteHeight - 2.0f)));
                g.drawText (juce::String (pitchClassName (n.pitch)) + juce::String (n.pitch / 12 - 1), r.reduced (4.0f, 0.0f), juce::Justification::centredLeft, false);
            }
        }

        // Notes being recorded into this clip's track.
        if (session.isRecording() && session.recordTarget() == trackId)
        {
            g.setColour (t.colour ("recording").withAlpha (0.8f));
            for (const auto& e : session.takeInProgress())
                if ((e.bytes[0] & 0xf0) == 0x90 && e.bytes[2] > 0)
                    g.fillRect (tickToX (e.tick), pitchToY (e.bytes[1]) + 1.0f, 4.0f, noteHeight - 2.0f);
        }

        if (drag == Drag::marquee)
        {
            g.setColour (t.colour ("accent").withAlpha (0.15f));
            g.fillRect (marquee);
            g.setColour (t.colour ("accent"));
            g.drawRect (marquee, 1.0f);
        }
    }

    // Ruler with bar numbers.
    g.setColour (t.colour ("panelRaised"));
    g.fillRect (ruler);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (ruler);
        g.setFont (t.font (11.0f));
        const Tick firstBar = std::max<Tick> (0, static_cast<Tick> (xToTick (static_cast<float> (grid.getX())) / static_cast<double> (bar)));
        const int every = static_cast<double> (bar) * pixelsPerTick < 40.0 ? 4 : 1;
        for (Tick b = firstBar; tickToX (static_cast<double> (b * bar)) < static_cast<float> (grid.getRight()); ++b)
        {
            if (b % every != 0)
                continue;
            const float x = tickToX (static_cast<double> (b * bar));
            g.setColour (t.colour ("lineStrong"));
            g.fillRect (x, static_cast<float> (ruler.getCentreY()), 1.0f, static_cast<float> (ruler.getHeight() / 2));
            g.setColour (t.colour ("textMuted"));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (x + 4.0f, static_cast<float> (ruler.getY()), 40.0f, static_cast<float> (ruler.getHeight())),
                        juce::Justification::centredLeft, false);
        }
    }

    // Keyboard
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (keys);
        g.setColour (t.colour ("panelRaised"));
        g.fillRect (keys);
        const int topPitch = std::min (127, static_cast<int> (std::ceil (scrollPitch)));
        for (int p = topPitch; p >= 0; --p)
        {
            const float y = pitchToY (p);
            if (y > static_cast<float> (keys.getBottom()))
                break;
            const auto row = juce::Rectangle<float> (static_cast<float> (keys.getX()), y, static_cast<float> (keys.getWidth()), noteHeight);
            if (isBlackKey (p))
            {
                g.setColour (juce::Colour (0xff2a2622));
                g.fillRoundedRectangle (row.withWidth (row.getWidth() * 0.62f).reduced (0.0f, 1.0f), 2.0f);
            }
            else
            {
                g.setColour (t.isDark ? juce::Colour (0xffd8d2c6) : juce::Colour (0xffffffff));
                g.fillRect (row.reduced (0.0f, 0.5f));
            }
            if (p == lastPreviewPitch)
            {
                g.setColour (t.colour ("accent"));
                g.fillRect (row.reduced (0.0f, 1.0f));
            }
            if (p % 12 == 0 && noteHeight >= 9.0f)
            {
                g.setColour (juce::Colour (0xff4a443b));
                g.setFont (t.font (std::min (10.5f, noteHeight)));
                g.drawText ("C" + juce::String (p / 12 - 1), row.withTrimmedRight (4.0f), juce::Justification::centredRight, false);
            }
        }
        g.setColour (t.colour ("line"));
        g.fillRect (keys.removeFromRight (1));
    }

    // Velocity lane
    g.setColour (t.colour ("panelRaised"));
    g.fillRect (vel);
    g.setColour (t.colour ("line"));
    g.fillRect (vel.removeFromTop (1));
    g.setColour (t.colour ("textDim"));
    g.setFont (t.font (11.0f));
    g.drawText ("Velocity", getLocalBounds().removeFromBottom (kVelocityHeight).removeFromLeft (kKeysWidth), juce::Justification::centred, false);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (vel);
        const auto lane = vel.toFloat().reduced (0.0f, 8.0f);
        for (const auto& n : c->notes)
        {
            const float x = tickToX (static_cast<double> (c->start + n.start));
            if (x < lane.getX() - 4.0f || x > lane.getRight())
                continue;
            const float h = lane.getHeight() * static_cast<float> (n.velocity) / 127.0f;
            const bool sel = isSelected (n);
            g.setColour (sel ? t.colour ("accent") : colour.withAlpha (0.85f));
            g.fillRect (x, lane.getBottom() - h, 2.0f, h);
            g.fillEllipse (x - 2.5f, lane.getBottom() - h - 2.5f, 7.0f, 5.0f);
        }
    }

    // Playhead
    const float px = tickToX (session.position());
    if (px >= static_cast<float> (grid.getX()) && px <= static_cast<float> (grid.getRight()))
    {
        g.setColour (t.colour ("playhead").withAlpha (session.isPlaying() ? 0.9f : 0.5f));
        g.fillRect (px, static_cast<float> (kToolbarHeight), 1.5f, static_cast<float> (getHeight() - kToolbarHeight));
    }
}

// ---- mouse ---------------------------------------------------------------------------------
void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    bool edge = false;
    if (gridArea().contains (e.getPosition()) && noteAt (e.position, &edge) >= 0)
        setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
    else if (velocityArea().contains (e.getPosition()))
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto c = clip();
    if (c == nullptr)
        return;
    ++gestureCounter;
    const auto trackId = edit::trackOfClip (session.song(), c->id);

    if (keysArea().contains (e.getPosition()))
    {
        lastPreviewPitch = yToPitch (e.position.y);
        session.previewNote (trackId, lastPreviewPitch, 100, 400);
        repaint (keysArea());
        return;
    }

    if (velocityArea().contains (e.getPosition()))
    {
        drag = Drag::velocity;
        mouseDrag (e);
        return;
    }

    if (! gridArea().contains (e.getPosition()))
        return;

    bool edge = false;
    const int index = noteAt (e.position, &edge);
    if (index >= 0)
    {
        const auto n = c->notes[static_cast<std::size_t> (index)];
        if (e.mods.isShiftDown() || e.mods.isCommandDown())
        {
            if (isSelected (n))
            {
                eraseOne (selected, n);
                repaint();
                return;
            }
            selected.push_back (n);
        }
        else if (! isSelected (n))
            selected = { n };

        dragNote = n;
        dragOriginal = selected;
        drag = edge ? Drag::resize : Drag::move;
        if (! edge)
        {
            session.previewNote (trackId, n.pitch, n.velocity);
            lastPreviewPitch = n.pitch;
        }
        repaint();
        return;
    }

    // Empty space: start a marquee; a click without dragging draws a note on mouse up.
    if (! e.mods.isShiftDown() && ! e.mods.isCommandDown())
        selected.clear();
    drag = Drag::marquee;
    marquee = { e.position, e.position };
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    const auto c = clip();
    if (c == nullptr)
        return;
    const auto key = "pianoroll-" + std::to_string (gestureCounter);

    switch (drag)
    {
        case Drag::move:
        {
            const double startAbs = static_cast<double> (c->start + dragNote.start);
            const double deltaRaw = static_cast<double> (e.getDistanceFromDragStartX()) / pixelsPerTick;
            Tick delta = snap (startAbs + deltaRaw, e.mods) - static_cast<Tick> (startAbs);
            int dp = yToPitch (e.position.y) - yToPitch (static_cast<float> (e.getMouseDownY()));
            for (const auto& n : dragOriginal)
            {
                delta = std::max (delta, -n.start);
                dp = std::clamp (dp, -static_cast<int> (n.pitch), 127 - static_cast<int> (n.pitch));
            }
            std::vector<Note> moved = dragOriginal;
            for (auto& n : moved)
            {
                n.start += delta;
                n.pitch = static_cast<std::uint8_t> (n.pitch + dp);
            }
            if (moved == selected)
                return;
            if (! moved.empty() && moved.front().pitch != selected.front().pitch)
            {
                const auto p = moved.size() == 1 ? moved.front().pitch : static_cast<std::uint8_t> (dragNote.pitch + dp);
                session.previewNote (edit::trackOfClip (session.song(), c->id), p, dragNote.velocity, 200);
            }
            const auto old = selected;
            selected = moved;
            changeNotes (moved.size() == 1 ? "Move Note" : "Move Notes", [&] (std::vector<Note>& notes) {
                for (const auto& n : old)
                    eraseOne (notes, n);
                notes.insert (notes.end(), moved.begin(), moved.end());
            }, key);
            break;
        }
        case Drag::resize:
        {
            const Tick endAbs = c->start + dragNote.end();
            const double newEnd = static_cast<double> (endAbs) + static_cast<double> (e.getDistanceFromDragStartX()) / pixelsPerTick;
            const Tick delta = snap (newEnd, e.mods) - endAbs;
            const Tick minLength = std::max<Tick> (10, e.mods.isAltDown() ? 10 : gridStep() / 2);
            std::vector<Note> changed = dragOriginal;
            for (auto& n : changed)
                n.length = std::max (minLength, n.length + delta);
            if (changed == selected)
                return;
            const auto old = selected;
            selected = changed;
            for (const auto& n : changed)
                if (n.start == dragNote.start && n.pitch == dragNote.pitch)
                    lastLength = n.length;
            changeNotes ("Change Note Length", [&] (std::vector<Note>& notes) {
                for (const auto& n : old)
                    eraseOne (notes, n);
                notes.insert (notes.end(), changed.begin(), changed.end());
            }, key);
            break;
        }
        case Drag::velocity:
        {
            const auto lane = velocityArea().toFloat().reduced (0.0f, 8.0f);
            const auto v = static_cast<std::uint8_t> (std::clamp (juce::roundToInt ((lane.getBottom() - e.position.y) / lane.getHeight() * 127.0f), 1, 127));
            // Notes under the mouse; if any of them is selected, change the whole selection.
            std::vector<Note> hit;
            for (const auto& n : c->notes)
                if (std::abs (tickToX (static_cast<double> (c->start + n.start)) - e.position.x) <= 4.0f)
                    hit.push_back (n);
            if (hit.empty())
                return;
            const bool useSelection = std::any_of (hit.begin(), hit.end(), [this] (const Note& n) { return isSelected (n); });
            const auto targets = useSelection ? selected : hit;
            if (std::all_of (targets.begin(), targets.end(), [v] (const Note& n) { return n.velocity == v; }))
                return;
            std::vector<Note> changed = targets;
            for (auto& n : changed)
                n.velocity = v;
            if (useSelection)
                selected = changed;
            else
                for (std::size_t i = 0; i < targets.size(); ++i)
                    if (auto it = std::find (selected.begin(), selected.end(), targets[i]); it != selected.end())
                        *it = changed[i];
            changeNotes ("Change Velocity", [&] (std::vector<Note>& notes) {
                for (const auto& n : targets)
                    eraseOne (notes, n);
                notes.insert (notes.end(), changed.begin(), changed.end());
            }, key);
            break;
        }
        case Drag::marquee:
        {
            marquee = juce::Rectangle<float> (e.mouseDownPosition, e.position);
            std::vector<Note> inside;
            for (const auto& n : c->notes)
                if (noteBounds (*c, n).intersects (marquee))
                    inside.push_back (n);
            selected = inside;
            repaint();
            break;
        }
        case Drag::none:
        case Drag::draw:
            break;
    }
}

void PianoRoll::mouseUp (const juce::MouseEvent& e)
{
    const auto c = clip();
    if (drag == Drag::marquee && c != nullptr && ! e.mouseWasDraggedSinceMouseDown() && ! e.mods.isShiftDown() && ! e.mods.isCommandDown())
    {
        // A plain click on empty space draws a note.
        const auto step = gridStep();
        const double abs = xToTick (e.position.x);
        Tick start = step > 0 && ! e.mods.isAltDown() ? static_cast<Tick> (std::floor (abs / static_cast<double> (step))) * step
                                                       : static_cast<Tick> (std::llround (abs));
        start = std::max<Tick> (0, start - c->start);
        Note n { start, lastLength > 0 ? lastLength : kTicksPerQuarter / 4, static_cast<std::uint8_t> (yToPitch (e.position.y)), 100 };
        changeNotes ("Add Note", [&] (std::vector<Note>& notes) { notes.push_back (n); });
        selected = { n };
        session.previewNote (edit::trackOfClip (session.song(), c->id), n.pitch, n.velocity);
    }
    drag = Drag::none;
    lastPreviewPitch = -1;
    session.endMerge();
    repaint();
}

void PianoRoll::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double-click a note to delete it.
    const auto c = clip();
    if (c == nullptr || ! gridArea().contains (e.getPosition()))
        return;
    const int index = noteAt (e.position);
    if (index < 0)
        return;
    const auto n = c->notes[static_cast<std::size_t> (index)];
    eraseOne (selected, n);
    changeNotes ("Delete Note", [&] (std::vector<Note>& notes) { eraseOne (notes, n); });
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown())
    {
        const double anchor = xToTick (e.position.x);
        pixelsPerTick = std::clamp (pixelsPerTick * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15), 0.005, 1.0);
        scrollTicks = anchor - (e.position.x - static_cast<float> (gridArea().getX())) / pixelsPerTick;
    }
    else if (e.mods.isAltDown())
    {
        noteHeight = std::clamp (noteHeight + (w.deltaY > 0 ? 1.0f : -1.0f), 6.0f, 28.0f);
    }
    else
    {
        const float dx = e.mods.isShiftDown() ? w.deltaY : w.deltaX;
        const float dy = e.mods.isShiftDown() ? 0.0f : w.deltaY;
        scrollTicks = std::max (-200.0, scrollTicks - static_cast<double> (dx) * 400.0 / pixelsPerTick * 0.25);
        const float rows = static_cast<float> (gridArea().getHeight()) / noteHeight;
        scrollPitch = std::clamp (scrollPitch + dy * 24.0f, std::min (128.0f, rows), 128.0f);
    }
    repaint();
}

void PianoRoll::mouseMagnify (const juce::MouseEvent& e, float scale)
{
    const double anchor = xToTick (e.position.x);
    pixelsPerTick = std::clamp (pixelsPerTick * static_cast<double> (scale), 0.005, 1.0);
    scrollTicks = anchor - (e.position.x - static_cast<float> (gridArea().getX())) / pixelsPerTick;
    repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& k)
{
    if (clip() == nullptr)
        return false;
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        deleteSelected();
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::upKey)
    {
        transpose (k.getModifiers().isShiftDown() ? 12 : 1);
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::downKey)
    {
        transpose (k.getModifiers().isShiftDown() ? -12 : -1);
        return true;
    }
    if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        selectAll();
        return true;
    }
    if (k == juce::KeyPress ('q', 0, 0))
    {
        quantiseNotes();
        return true;
    }
    if (k == juce::KeyPress::escapeKey)
    {
        selected.clear();
        repaint();
        return true;
    }
    return false;
}

} // namespace arranger::ui

#include "TimelineViews.h"

#include "Panels.h"

namespace arranger::ui
{

namespace
{
    juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
    const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");

    juce::String outputSummary (const Track& t)
    {
        switch (t.output.kind)
        {
            case TrackOutput::Kind::none: return "No output";
            case TrackOutput::Kind::soundFont:
                return "SoundFont" + dot + (t.sound.name.empty() ? juce::String ("Ch ") + juce::String (t.output.channel) : utf8 (t.sound.name));
            case TrackOutput::Kind::sampler:
                return "Sampler" + dot + (t.kit.pads.empty() ? juce::String ("no samples") : juce::String (t.kit.pads.size()) + " pads");
            case TrackOutput::Kind::midiPort: break;
        }
        return utf8 (t.output.port) + dot + (t.sound.name.empty() ? "Ch " + juce::String (t.output.channel) : utf8 (t.sound.name));
    }
}

// =====================================================================================
TrackList::TrackList (Session& s, TimelineGeometry& g) : session (s), geo (g)
{
    session.addChangeListener (this);
    addAndMakeVisible (addButton);
    addButton.setTooltip ("Add a track");
    addButton.onClick = [this] { addTrack(); };
}

TrackList::~TrackList() { session.removeChangeListener (this); }

void TrackList::resized()
{
    addButton.setBounds (getWidth() - 44, 14, 28, 28);
}

juce::Rectangle<int> TrackList::rowBounds (int index) const
{
    return { 0, geo.trackY (index), getWidth(), geo.trackHeight };
}

juce::Rectangle<int> TrackList::buttonBounds (int index, int which) const
{
    const auto row = rowBounds (index);
    return { getWidth() - 16 - (3 - which) * 28, row.getCentreY() - 12, 24, 24 };
}

void TrackList::addTrack()
{
    const auto& song = session.song();
    const int after = edit::trackIndex (song, session.selectedTrack());
    TrackId added = 0;
    session.edit ("Add Track", [&] (Song& s) {
        auto& t = edit::addTrack (s, "Track " + std::to_string (s.tracks.size() + 1), after);
        added = t.id;
    });
    session.selectTrack (added);
}

void TrackList::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    const auto& song = session.song();

    auto header = getLocalBounds().removeFromTop (geo.rulerHeight).reduced (20, 0);
    g.setColour (t.colour ("text"));
    g.setFont (t.font (15.0f, true));
    g.drawText ("Tracks", header, juce::Justification::centredLeft);
    g.setColour (t.colour ("textMuted"));
    g.setFont (t.font (15.0f));
    g.drawText (juce::String (song.tracks.size()), header.withTrimmedLeft (64), juce::Justification::centredLeft);

    g.saveState();
    g.reduceClipRegion (getLocalBounds().withTrimmedTop (geo.rulerHeight));

    const auto selected = session.selectedTrack();
    const auto recordTarget = session.recordTarget();
    for (int i = 0; i < static_cast<int> (song.tracks.size()); ++i)
    {
        const auto& track = song.tracks[static_cast<std::size_t> (i)];
        auto row = rowBounds (i);
        if (row.getBottom() < geo.rulerHeight || row.getY() > getHeight())
            continue;

        if (track.id == selected)
        {
            g.setColour (t.colour ("panelRaised"));
            g.fillRect (row);
        }

        const auto c = t.trackColour (utf8 (track.colour));
        g.setColour (c);
        g.fillRoundedRectangle (juce::Rectangle<float> (16.0f, static_cast<float> (row.getY() + 10), 4.0f, static_cast<float> (row.getHeight() - 20)), 2.0f);

        auto text = row.withTrimmedLeft (32).withTrimmedRight (3 * 28 + 20).reduced (0, 12);
        g.setColour (t.colour ("text"));
        g.setFont (t.font (15.0f, true));
        g.drawText (utf8 (track.name), text.removeFromTop (text.getHeight() / 2), juce::Justification::bottomLeft, true);
        g.setColour (t.colour ("textMuted"));
        g.setFont (t.font (12.5f));
        g.drawText (outputSummary (track), text.withTrimmedTop (3), juce::Justification::topLeft, true);

        // Mute, solo, record-arm
        const char* labels[] = { "M", "S", "R" };
        const bool states[] = { track.mixer.mute, track.mixer.solo, track.armed || (track.id == recordTarget && session.isRecording()) };
        const juce::Colour onColours[] = { t.colour ("warning"), t.colour ("accent"), t.colour ("recording") };
        for (int b = 0; b < 3; ++b)
        {
            const auto r = buttonBounds (i, b).toFloat();
            g.setColour (states[b] ? onColours[b].withAlpha (0.25f) : t.colour ("panelRaised"));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (states[b] ? onColours[b] : t.colour ("lineStrong"));
            g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
            g.setColour (states[b] ? onColours[b] : t.colour ("textMuted"));
            g.setFont (t.font (12.0f, true));
            g.drawText (labels[b], r, juce::Justification::centred);
        }

        g.setColour (t.colour ("line"));
        g.fillRect (row.removeFromBottom (1));
    }

    if (song.tracks.empty())
    {
        g.setColour (t.colour ("textDim"));
        g.setFont (t.font (13.0f));
        g.drawFittedText ("No tracks yet.\nClick + to add one, or\nimport a MIDI file.", getLocalBounds().withTrimmedTop (geo.rulerHeight).reduced (20).removeFromTop (80),
                          juce::Justification::topLeft, 3);
    }
    g.restoreState();
}

void TrackList::mouseDown (const juce::MouseEvent& e)
{
    const auto& song = session.song();
    const int index = geo.trackAt (e.y);
    if (index < 0 || index >= static_cast<int> (song.tracks.size()))
        return;
    const auto& track = song.tracks[static_cast<std::size_t> (index)];
    const auto id = track.id;

    for (int b = 0; b < 3; ++b)
        if (buttonBounds (index, b).contains (e.getPosition()))
        {
            if (b == 0)
                session.edit (track.mixer.mute ? "Unmute" : "Mute", [id] (Song& s) { auto* t = edit::findTrack (s, id); t->mixer.mute = ! t->mixer.mute; });
            else if (b == 1)
                session.edit (track.mixer.solo ? "Unsolo" : "Solo", [id] (Song& s) { auto* t = edit::findTrack (s, id); t->mixer.solo = ! t->mixer.solo; });
            else
                session.edit ("Record Arm", [id, exclusive = ! e.mods.isShiftDown()] (Song& s) {
                    for (auto& t : s.tracks)
                        if (t.id == id)
                            t.armed = ! t.armed;
                        else if (exclusive)
                            t.armed = false;
                });
            return;
        }

    session.selectTrack (id);
    if (e.mods.isPopupMenu())
        showMenu (id);
}

void TrackList::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int index = geo.trackAt (e.y);
    if (index >= 0 && index < static_cast<int> (session.song().tracks.size()) && e.x < getWidth() - 3 * 28 - 20)
        rename (session.song().tracks[static_cast<std::size_t> (index)].id);
    else if (index >= static_cast<int> (session.song().tracks.size()))
        addTrack();
}

void TrackList::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const int content = static_cast<int> (session.song().tracks.size()) * geo.trackHeight;
    const int maxScroll = std::max (0, content - (getHeight() - geo.rulerHeight) + geo.trackHeight);
    geo.scrollY = std::clamp (geo.scrollY - static_cast<int> (w.deltaY * 300.0f), 0, maxScroll);
    geo.changed();
}

void TrackList::rename (TrackId id)
{
    const int index = edit::trackIndex (session.song(), id);
    if (index < 0)
        return;
    nameEditor = std::make_unique<juce::TextEditor>();
    addAndMakeVisible (*nameEditor);
    nameEditor->setFont (currentTheme().font (15.0f, true));
    nameEditor->setText (utf8 (session.song().tracks[static_cast<std::size_t> (index)].name), false);
    nameEditor->setBounds (rowBounds (index).withTrimmedLeft (28).withTrimmedRight (3 * 28 + 20).withSizeKeepingCentre (getWidth() - 3 * 28 - 48, 30));
    nameEditor->selectAll();
    nameEditor->grabKeyboardFocus();
    auto commit = [this, id] {
        if (nameEditor == nullptr)
            return;
        const auto text = nameEditor->getText().trim().toStdString();
        juce::MessageManager::callAsync ([this] { nameEditor = nullptr; });
        if (! text.empty())
            session.edit ("Rename Track", [id, text] (Song& s) { if (auto* t = edit::findTrack (s, id)) t->name = text; });
    };
    nameEditor->onReturnKey = commit;
    nameEditor->onFocusLost = commit;
    nameEditor->onEscapeKey = [this] { juce::MessageManager::callAsync ([this] { nameEditor = nullptr; }); };
}

void TrackList::showMenu (TrackId id)
{
    juce::PopupMenu colours;
    int item = 100;
    for (const auto* c : kTrackColours)
    {
        juce::PopupMenu::Item it (juce::String (c).substring (0, 1).toUpperCase() + juce::String (c).substring (1));
        it.itemID = item++;
        it.colour = currentTheme().trackColour (c);
        colours.addItem (it);
    }

    juce::PopupMenu m;
    m.addItem (1, "Rename");
    m.addItem (2, "Duplicate");
    m.addSubMenu ("Colour", colours);
    m.addSeparator();
    m.addItem (3, "Move Up");
    m.addItem (4, "Move Down");
    m.addSeparator();
    m.addItem (5, "Delete Track");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, id] (int r) {
        const int index = edit::trackIndex (session.song(), id);
        if (r == 1) rename (id);
        else if (r == 2)
            session.edit ("Duplicate Track", [id, index] (Song& s) {
                const auto* src = edit::findTrack (s, id);
                if (src == nullptr)
                    return;
                Track copy = *src;
                copy.id = edit::takeId (s);
                copy.name += " copy";
                copy.armed = false;
                for (auto& c : copy.clips)
                {
                    auto clip = std::make_shared<Clip> (*c);
                    clip->id = edit::takeId (s);
                    c = clip;
                }
                s.tracks.insert (s.tracks.begin() + index + 1, std::move (copy));
            });
        else if (r == 3) session.edit ("Move Track", [id, index] (Song& s) { edit::moveTrack (s, id, index - 1); });
        else if (r == 4) session.edit ("Move Track", [id, index] (Song& s) { edit::moveTrack (s, id, index + 1); });
        else if (r == 5) session.edit ("Delete Track", [id] (Song& s) { edit::removeTrack (s, id); });
        else if (r >= 100)
        {
            const std::string colour = kTrackColours[r - 100];
            session.edit ("Track Colour", [id, colour] (Song& s) { if (auto* t = edit::findTrack (s, id)) t->colour = colour; });
        }
    });
}

// =====================================================================================
Arrangement::Arrangement (Session& s, TimelineGeometry& g) : session (s), geo (g)
{
    session.addChangeListener (this);
    setTooltip ("Double-click a lane to add a clip, double-click a clip to edit its notes. Drag in the ruler's lower half to set the loop.");
}

Arrangement::~Arrangement() { session.removeChangeListener (this); }

Tick Arrangement::snap (double tick, const juce::ModifierKeys& mods) const
{
    if (mods.isAltDown())
        return static_cast<Tick> (std::llround (tick)); // free
    const auto& sig = session.song().timeSignature;
    // Snap to bars when zoomed out, to beats when a beat is wide enough to aim at.
    const Tick unit = (mods.isAltDown() || sig.ticksPerBeat() * geo.pixelsPerTick > 24.0) ? sig.ticksPerBeat() : sig.ticksPerBar();
    return static_cast<Tick> (std::llround (tick / static_cast<double> (unit))) * unit;
}

juce::Rectangle<float> Arrangement::clipBounds (const Clip& c, int trackIndex) const
{
    const float x1 = geo.tickToX (static_cast<double> (c.start));
    const float x2 = geo.tickToX (static_cast<double> (c.end()));
    return { x1 + 1.0f, static_cast<float> (geo.trackY (trackIndex) + 5), std::max (4.0f, x2 - x1 - 2.0f), static_cast<float> (geo.trackHeight - 10) };
}

Arrangement::Hit Arrangement::hitTest (juce::Point<int> p) const
{
    Hit h;
    h.trackIndex = geo.trackAt (p.y);
    const auto& song = session.song();
    if (h.trackIndex < 0 || h.trackIndex >= static_cast<int> (song.tracks.size()))
        return h;
    const auto& track = song.tracks[static_cast<std::size_t> (h.trackIndex)];
    for (auto it = track.clips.rbegin(); it != track.clips.rend(); ++it)
    {
        const auto r = clipBounds (**it, h.trackIndex);
        if (r.contains (p.toFloat()))
        {
            h.clip = (*it)->id;
            h.rightEdge = p.x > r.getRight() - 8;
            return h;
        }
    }
    return h;
}

void Arrangement::drawClip (juce::Graphics& g, const Clip& clip, juce::Rectangle<float> r, juce::Colour c, bool selected, bool ghost) const
{
    const auto& t = currentTheme();
    g.setColour (c.withAlpha (ghost ? 0.08f : (selected ? 0.24f : 0.14f)));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (selected ? c : c.withAlpha (0.55f));
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, selected ? 2.0f : 1.0f);

    g.saveState();
    g.reduceClipRegion (r.toNearestInt());
    g.setColour (c);
    g.setFont (t.font (11.0f, true));
    g.drawText (utf8 (clip.name), r.reduced (8.0f, 4.0f).removeFromTop (14.0f), juce::Justification::topLeft, true);

    if (! clip.notes.empty())
    {
        int lo = 127, hi = 0;
        for (const auto& n : clip.notes)
        {
            lo = std::min<int> (lo, n.pitch);
            hi = std::max<int> (hi, n.pitch);
        }
        const auto noteArea = r.reduced (4.0f, 4.0f).withTrimmedTop (16.0f);
        const float range = static_cast<float> (std::max (12, hi - lo + 1));
        const float rowH = std::max (1.5f, std::min (4.0f, noteArea.getHeight() / range));
        // A pattern clip shows its first part again and again, with a line at each repeat.
        const Tick loop = clip.loopLength > 0 && clip.loopLength < clip.length ? clip.loopLength : clip.length;
        for (Tick base = 0; base < clip.length; base += loop)
        {
            if (base > 0)
            {
                g.setColour (c.withAlpha (0.35f));
                g.fillRect (geo.tickToX (static_cast<double> (clip.start + base)), r.getY() + 3.0f, 1.0f, r.getHeight() - 6.0f);
            }
            g.setColour (base == 0 ? c : c.withAlpha (0.6f));
            for (const auto& n : clip.notes)
            {
                if (n.start >= loop || base + n.start >= clip.length)
                    continue;
                const float x1 = geo.tickToX (static_cast<double> (clip.start + base + n.start));
                const float x2 = geo.tickToX (static_cast<double> (clip.start + std::min ({ base + n.end(), base + loop, clip.length })));
                if (x2 < 0 || x1 > static_cast<float> (getWidth()))
                    continue;
                const float y = noteArea.getBottom() - (static_cast<float> (n.pitch - lo) + 1.0f) / range * noteArea.getHeight();
                g.fillRect (juce::Rectangle<float> (x1, y, std::max (1.5f, x2 - x1 - 1.0f), rowH));
            }
        }
    }
    g.restoreState();
}

void Arrangement::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    const auto& song = session.song();
    const auto& sig = song.timeSignature;
    const double barTicks = static_cast<double> (sig.ticksPerBar());
    const float barW = static_cast<float> (barTicks * geo.pixelsPerTick);
    const int rulerH = geo.rulerHeight;

    // Which bars get a number: every bar if there's room, else every 2, 4, 8...
    int labelEvery = 1;
    while (barW * static_cast<float> (labelEvery) < 36.0f)
        labelEvery *= 2;

    const auto firstBar = static_cast<std::int64_t> (std::floor (geo.scrollTicks / barTicks));
    const auto lastBar = static_cast<std::int64_t> (std::ceil (geo.xToTick (static_cast<float> (getWidth())) / barTicks));

    // Lanes: alternate tint, selected track highlighted.
    for (int i = 0; i < static_cast<int> (song.tracks.size()); ++i)
    {
        const auto y = geo.trackY (i);
        if (song.tracks[static_cast<std::size_t> (i)].id == session.selectedTrack())
        {
            g.setColour (t.colour ("panelRaised").withAlpha (0.45f));
            g.fillRect (0, y, getWidth(), geo.trackHeight);
        }
        g.setColour (t.colour ("line").withAlpha (0.6f));
        g.fillRect (0, y + geo.trackHeight - 1, getWidth(), 1);
    }

    // Grid
    for (auto bar = firstBar; bar <= lastBar; ++bar)
    {
        const float x = geo.tickToX (static_cast<double> (bar) * barTicks);
        g.setColour (t.colour ("line"));
        g.fillRect (juce::Rectangle<float> (x, static_cast<float> (rulerH), 1.0f, static_cast<float> (getHeight() - rulerH)));
        if (barW / static_cast<float> (sig.numerator) > 10.0f)
        {
            g.setColour (t.colour ("line").withAlpha (0.4f));
            for (int b = 1; b < sig.numerator; ++b)
                g.fillRect (juce::Rectangle<float> (x + barW * static_cast<float> (b) / static_cast<float> (sig.numerator), static_cast<float> (rulerH), 1.0f,
                                                    static_cast<float> (getHeight() - rulerH)));
        }
    }

    // Clips
    g.saveState();
    g.reduceClipRegion (getLocalBounds().withTrimmedTop (rulerH));
    const auto& selected = session.selectedClips();
    for (int i = 0; i < static_cast<int> (song.tracks.size()); ++i)
    {
        const auto& track = song.tracks[static_cast<std::size_t> (i)];
        const auto colour = t.trackColour (utf8 (track.colour));
        for (const auto& clip : track.clips)
        {
            const bool isSel = selected.count (clip->id) > 0;
            if (drag == Drag::move && isSel)
            {
                drawClip (g, *clip, clipBounds (*clip, i), colour, false, true); // where it was
                Clip moved = *clip;
                moved.start = std::max<Tick> (0, clip->start + dragDeltaTicks);
                const int target = std::clamp (i + dragDeltaTracks, 0, static_cast<int> (song.tracks.size()) - 1);
                drawClip (g, moved, clipBounds (moved, target), colour, true, false);
            }
            else if (drag == Drag::resize && clip->id == dragClip)
            {
                Clip resized = *clip;
                resized.length = dragNewLength;
                drawClip (g, resized, clipBounds (resized, i), colour, true, false);
            }
            else
            {
                const auto r = clipBounds (*clip, i);
                if (r.getRight() >= 0 && r.getX() <= static_cast<float> (getWidth()))
                    drawClip (g, *clip, r, colour, isSel, false);
            }
        }
    }

    // Recording in progress: a red clip growing with the playhead.
    if (session.isRecording())
    {
        const int index = edit::trackIndex (song, session.recordTarget());
        const auto& take = session.takeInProgress();
        if (index >= 0)
        {
            const double startTick = take.empty() ? session.position() : take.front().tick;
            const float x1 = geo.tickToX (std::floor (startTick / barTicks) * barTicks);
            const float x2 = geo.tickToX (session.position());
            const auto r = juce::Rectangle<float> (x1 + 1, static_cast<float> (geo.trackY (index) + 5), std::max (4.0f, x2 - x1), static_cast<float> (geo.trackHeight - 10));
            g.setColour (t.colour ("recording").withAlpha (0.16f));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (t.colour ("recording"));
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.5f);
            for (const auto& e : take)
                if ((e.bytes[0] & 0xf0) == 0x90 && e.bytes[2] > 0)
                {
                    const float x = geo.tickToX (e.tick);
                    const float y = r.getBottom() - 6.0f - (static_cast<float> (e.bytes[1]) / 127.0f) * (r.getHeight() - 20.0f);
                    g.fillRect (juce::Rectangle<float> (x, y, 6.0f, 2.5f));
                }
        }
    }

    if (drag == Drag::marquee)
    {
        g.setColour (t.colour ("accent").withAlpha (0.12f));
        g.fillRect (marquee);
        g.setColour (t.colour ("accent").withAlpha (0.6f));
        g.drawRect (marquee, 1);
    }
    g.restoreState();

    // Ruler
    g.setColour (t.colour ("panel"));
    g.fillRect (0, 0, getWidth(), rulerH);
    if (song.loop.isValid())
    {
        const float x1 = geo.tickToX (static_cast<double> (song.loop.start));
        const float x2 = geo.tickToX (static_cast<double> (song.loop.end));
        const auto band = juce::Rectangle<float> (x1, 36.0f, x2 - x1, 10.0f);
        g.setColour (song.loop.enabled ? t.colour ("accent").withAlpha (0.55f) : t.colour ("lineStrong"));
        g.fillRoundedRectangle (band, 4.0f);
        if (song.loop.enabled)
        {
            g.setColour (t.colour ("accent").withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float> (x1, static_cast<float> (rulerH), x2 - x1, static_cast<float> (getHeight() - rulerH)));
        }
    }
    for (auto bar = firstBar; bar <= lastBar; ++bar)
    {
        if (bar < 0 || bar % labelEvery != 0)
            continue;
        const float x = geo.tickToX (static_cast<double> (bar) * barTicks);
        const bool major = bar % 4 == 0;
        g.setColour (major ? t.colour ("text").withAlpha (0.85f) : t.colour ("textDim"));
        g.setFont (t.font (12.5f, major));
        g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 6, 4, 60, 26), juce::Justification::centredLeft);
    }
    g.setColour (t.colour ("line"));
    g.fillRect (0, rulerH - 1, getWidth(), 1);

    // Playhead
    const int x = juce::roundToInt (geo.tickToX (session.position()));
    shownPlayheadX = x;
    g.setColour (session.isPlaying() ? t.colour ("playhead") : t.colour ("playhead").withAlpha (0.6f));
    g.fillRect (x, 24, 2, getHeight() - 24);
    juce::Path cap;
    cap.addTriangle (static_cast<float> (x) - 5.0f, 24.0f, static_cast<float> (x) + 7.0f, 24.0f, static_cast<float> (x) + 1.0f, 32.0f);
    g.fillPath (cap);

    if (song.tracks.empty())
    {
        auto card = getLocalBounds().withTrimmedTop (rulerH).withSizeKeepingCentre (juce::jmin (getWidth() - 48, 520), 112).toFloat();
        g.setColour (t.colour ("panelRaised").withAlpha (0.92f));
        g.fillRoundedRectangle (card, 10.0f);
        g.setColour (t.colour ("lineStrong"));
        g.drawRoundedRectangle (card.reduced (0.5f), 10.0f, 1.0f);
        auto text = card.reduced (24.0f, 20.0f);
        g.setColour (t.colour ("text"));
        g.setFont (t.font (16.0f, true));
        g.drawText ("Start a song", text.removeFromTop (24.0f), juce::Justification::centredLeft);
        g.setColour (t.colour ("textMuted"));
        g.setFont (t.font (13.5f));
        g.drawFittedText ("Add a track with + (or Cmd+T), then press R to record from your keyboard.\nOr import a MIDI file from the File menu.",
                          text.toNearestInt().withTrimmedTop (6), juce::Justification::topLeft, 3);
    }
}

void Arrangement::refresh()
{
    const double pos = session.position();
    const float px = geo.tickToX (pos);

    // Page along with the playhead while playing.
    if (session.isPlaying() && (px > static_cast<float> (getWidth()) - 40.0f || px < 0.0f))
    {
        geo.scrollTicks = std::max (0.0, pos - 40.0 / geo.pixelsPerTick);
        geo.changed();
        return;
    }

    const int x = juce::roundToInt (px);
    if (x != shownPlayheadX)
    {
        repaint (shownPlayheadX - 6, 0, 14, getHeight());
        repaint (x - 6, 0, 14, getHeight());
    }
    if (session.isRecording())
        repaint();
}

void Arrangement::zoom (double factor, float aroundX)
{
    const double anchor = geo.xToTick (aroundX);
    geo.pixelsPerTick = std::clamp (geo.pixelsPerTick * factor, 4.0 / 3840.0, 2000.0 / 3840.0);
    geo.scrollTicks = std::max (0.0, anchor - aroundX / geo.pixelsPerTick);
    geo.changed();
}

void Arrangement::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoom (std::pow (1.15, w.deltaY * 8.0), static_cast<float> (e.x));
        return;
    }
    const float dx = e.mods.isShiftDown() ? w.deltaY : w.deltaX;
    const float dy = e.mods.isShiftDown() ? 0.0f : w.deltaY;
    geo.scrollTicks = std::max (0.0, geo.scrollTicks - dx * 400.0 / geo.pixelsPerTick);
    const int content = static_cast<int> (session.song().tracks.size()) * geo.trackHeight;
    const int maxScroll = std::max (0, content - (getHeight() - geo.rulerHeight) + geo.trackHeight);
    geo.scrollY = std::clamp (geo.scrollY - static_cast<int> (dy * 300.0f), 0, maxScroll);
    geo.changed();
}

void Arrangement::mouseMagnify (const juce::MouseEvent& e, float scale)
{
    zoom (scale, static_cast<float> (e.x));
}

void Arrangement::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.getPosition());
    setMouseCursor (h.clip != 0 && h.rightEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void Arrangement::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::none;
    dragDeltaTicks = 0;
    dragDeltaTracks = 0;

    if (e.y < geo.rulerHeight)
    {
        const auto tick = snap (geo.xToTick (static_cast<float> (e.x)), e.mods);
        if (e.y >= 30)
        {
            drag = Drag::loop;
            loopAnchor = std::max<Tick> (0, tick);
        }
        else
        {
            drag = Drag::locate;
            const auto beat = session.song().timeSignature.ticksPerBeat();
            session.locate (std::max<Tick> (0, static_cast<Tick> (std::llround (geo.xToTick (static_cast<float> (e.x)) / static_cast<double> (beat))) * beat));
        }
        return;
    }

    const auto h = hitTest (e.getPosition());
    const auto& song = session.song();
    if (h.trackIndex >= 0 && h.trackIndex < static_cast<int> (song.tracks.size()))
        session.selectTrack (song.tracks[static_cast<std::size_t> (h.trackIndex)].id);

    if (h.clip == 0)
    {
        if (! e.mods.isShiftDown())
            session.selectClips ({});
        drag = Drag::marquee;
        marquee = {};
        return;
    }

    auto sel = session.selectedClips();
    if (e.mods.isShiftDown())
    {
        if (sel.count (h.clip) > 0)
            sel.erase (h.clip);
        else
            sel.insert (h.clip);
    }
    else if (sel.count (h.clip) == 0)
        sel = { h.clip };
    session.selectClips (sel);

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Edit Notes");
        m.addItem (2, "Duplicate");
        m.addItem (3, "Split at Playhead");
        m.addItem (4, "Join");
        m.addItem (5, "Quantise");
        m.addSeparator();
        m.addItem (6, "Delete");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, clip = h.clip] (int r) {
            if (r == 1) session.openInEditor (clip);
            else if (r == 2) duplicateSelected();
            else if (r == 3) splitAtPlayhead();
            else if (r == 4)
            {
                std::vector<ClipId> ids (session.selectedClips().begin(), session.selectedClips().end());
                session.edit ("Join Clips", [ids] (Song& s) { edit::joinClips (s, ids); });
            }
            else if (r == 5)
            {
                const auto q = session.quantise;
                const auto ids = session.selectedClips();
                session.edit ("Quantise", [ids, q] (Song& s) {
                    for (auto id : ids)
                        edit::modifyClip (s, id, [&] (Clip& c) { arranger::quantise (c, q); });
                });
            }
            else if (r == 6) deleteSelected();
        });
        return;
    }

    dragClip = h.clip;
    if (h.rightEdge)
    {
        drag = Drag::resize;
        dragNewLength = edit::findClipPtr (song, h.clip)->length;
    }
    else
        drag = Drag::move;
}

void Arrangement::mouseDrag (const juce::MouseEvent& e)
{
    const auto& song = session.song();
    switch (drag)
    {
        case Drag::move:
        {
            const auto clip = edit::findClipPtr (song, dragClip);
            if (clip == nullptr)
                return;
            const double delta = e.getDistanceFromDragStartX() / geo.pixelsPerTick;
            dragDeltaTicks = snap (static_cast<double> (clip->start) + delta, e.mods) - clip->start;
            dragDeltaTracks = juce::roundToInt (static_cast<float> (e.getDistanceFromDragStartY()) / static_cast<float> (geo.trackHeight));
            repaint();
            break;
        }
        case Drag::resize:
        {
            const auto clip = edit::findClipPtr (song, dragClip);
            if (clip == nullptr)
                return;
            const auto end = snap (geo.xToTick (static_cast<float> (e.x)), e.mods);
            dragNewLength = std::max<Tick> (song.timeSignature.ticksPerBeat(), end - clip->start);
            repaint();
            break;
        }
        case Drag::loop:
        {
            const auto tick = std::max<Tick> (0, snap (geo.xToTick (static_cast<float> (e.x)), e.mods));
            if (tick == loopAnchor)
                return;
            const LoopRange loop { true, std::min (tick, loopAnchor), std::max (tick, loopAnchor) };
            session.edit ("Set Loop", [loop] (Song& s) { s.loop = loop; }, "loop-drag");
            break;
        }
        case Drag::marquee:
        {
            marquee = juce::Rectangle<int> (e.getMouseDownPosition(), e.getPosition());
            std::set<ClipId> sel;
            for (int i = 0; i < static_cast<int> (song.tracks.size()); ++i)
                for (const auto& c : song.tracks[static_cast<std::size_t> (i)].clips)
                    if (clipBounds (*c, i).intersects (marquee.toFloat()))
                        sel.insert (c->id);
            session.selectClips (sel);
            break;
        }
        case Drag::locate:
        case Drag::none:
            break;
    }
}

void Arrangement::mouseUp (const juce::MouseEvent&)
{
    const auto& song = session.song();
    if (drag == Drag::move && (dragDeltaTicks != 0 || dragDeltaTracks != 0))
    {
        const auto ids = session.selectedClips();
        const auto deltaT = dragDeltaTicks;
        const auto deltaTr = dragDeltaTracks;
        session.edit (ids.size() > 1 ? "Move Clips" : "Move Clip", [ids, deltaT, deltaTr] (Song& s) {
            for (auto id : ids)
            {
                const auto clip = edit::findClipPtr (s, id);
                const int from = edit::trackIndex (s, edit::trackOfClip (s, id));
                if (clip == nullptr || from < 0)
                    continue;
                const int to = std::clamp (from + deltaTr, 0, static_cast<int> (s.tracks.size()) - 1);
                edit::moveClip (s, id, clip->start + deltaT, s.tracks[static_cast<std::size_t> (to)].id);
            }
        });
    }
    else if (drag == Drag::resize)
    {
        const auto id = dragClip;
        const auto length = dragNewLength;
        if (auto clip = edit::findClipPtr (song, id); clip != nullptr && clip->length != length)
            session.edit ("Resize Clip", [id, length] (Song& s) { edit::modifyClip (s, id, [length] (Clip& c) { c.length = length; }); });
    }
    session.endMerge();
    drag = Drag::none;
    dragDeltaTicks = 0;
    dragDeltaTracks = 0;
    repaint();
}

void Arrangement::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y < geo.rulerHeight)
    {
        if (session.song().loop.isValid())
            session.edit ("Loop", [] (Song& s) { s.loop.enabled = ! s.loop.enabled; });
        return;
    }
    const auto h = hitTest (e.getPosition());
    if (h.clip != 0)
    {
        session.openInEditor (h.clip);
        return;
    }
    const auto& song = session.song();
    if (h.trackIndex < 0 || h.trackIndex >= static_cast<int> (song.tracks.size()))
        return;

    // New empty clip, one bar... at the bar under the mouse.
    const auto bar = song.timeSignature.ticksPerBar();
    const auto start = static_cast<Tick> (std::floor (geo.xToTick (static_cast<float> (e.x)) / static_cast<double> (bar))) * bar;
    const auto trackId = song.tracks[static_cast<std::size_t> (h.trackIndex)].id;
    ClipId added = 0;
    session.edit ("Add Clip", [&] (Song& s) {
        Clip c;
        c.start = std::max<Tick> (0, start);
        c.length = bar * 4;
        added = edit::addClip (s, trackId, c);
    });
    session.selectClips ({ added });
    session.openInEditor (added);
}

void Arrangement::deleteSelected()
{
    const auto ids = session.selectedClips();
    if (ids.empty())
        return;
    session.edit (ids.size() > 1 ? "Delete Clips" : "Delete Clip", [ids] (Song& s) {
        for (auto id : ids)
            edit::removeClip (s, id);
    });
}

void Arrangement::duplicateSelected()
{
    const auto ids = session.selectedClips();
    if (ids.empty())
        return;
    std::set<ClipId> added;
    session.edit ("Duplicate", [&] (Song& s) {
        // Copies go right after the end of the whole selection.
        Tick first = std::numeric_limits<Tick>::max(), last = 0;
        for (auto id : ids)
            if (auto c = edit::findClipPtr (s, id))
            {
                first = std::min (first, c->start);
                last = std::max (last, c->end());
            }
        const auto bar = s.timeSignature.ticksPerBar();
        const Tick shift = ((last - first + bar - 1) / bar) * bar;
        for (auto id : ids)
            if (auto c = edit::findClipPtr (s, id))
            {
                Clip copy = *c;
                copy.id = 0;
                copy.start += shift;
                added.insert (edit::addClip (s, edit::trackOfClip (s, id), copy));
            }
    });
    session.selectClips (added);
}

void Arrangement::splitAtPlayhead()
{
    const auto at = static_cast<Tick> (std::llround (session.position()));
    auto ids = session.selectedClips();
    if (ids.empty()) // no selection: split whatever is under the playhead on the selected track
        if (const auto* t = edit::findTrack (session.song(), session.selectedTrack()))
            for (const auto& c : t->clips)
                if (c->start < at && c->end() > at)
                    ids.insert (c->id);
    if (ids.empty())
        return;
    session.edit ("Split", [ids, at] (Song& s) {
        for (auto id : ids)
            edit::splitClip (s, id, at);
    });
}

} // namespace arranger::ui

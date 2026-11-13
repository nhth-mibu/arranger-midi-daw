#include "ClipGridView.h"

#include "Icons.h"

namespace arranger::ui
{

namespace
{
    juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    struct LaunchChoice { const char* text; Tick ticks; };
    constexpr LaunchChoice kLaunch[] = {
        { "At once", 0 },
        { "Next 1/16 note", kTicksPerQuarter / 4 },
        { "Next 1/8 note", kTicksPerQuarter / 2 },
        { "Next beat", kTicksPerQuarter },
        { "Next bar", -1 },
        { "Next 2 bars", -2 },
        { "Next 4 bars", -4 },
    };

    Tick launchTicks (const LaunchChoice& c, const Song& song)
    {
        return c.ticks >= 0 ? c.ticks : -c.ticks * song.timeSignature.ticksPerBar();
    }

    juce::String sceneName (const Song& song, int scene)
    {
        if (scene >= 0 && static_cast<std::size_t> (scene) < song.scenes.size() && ! song.scenes[static_cast<std::size_t> (scene)].empty())
            return utf8 (song.scenes[static_cast<std::size_t> (scene)]);
        return "Scene " + juce::String (scene + 1);
    }

    ClipPtr slotOf (const Track& t, int scene)
    {
        return scene >= 0 && static_cast<std::size_t> (scene) < t.slots.size() ? t.slots[static_cast<std::size_t> (scene)] : nullptr;
    }
}

ClipGridView::ClipGridView (Session& s) : session (s)
{
    session.addChangeListener (this);

    addAndMakeVisible (title);
    title.setText ("Clip grid", juce::dontSendNotification);
    title.setInterceptsMouseClicks (false, false);

    addAndMakeVisible (launchLabel);
    launchLabel.setText ("Clips start:", juce::dontSendNotification);
    launchLabel.setJustificationType (juce::Justification::centredRight);
    launchLabel.setInterceptsMouseClicks (false, false);

    addAndMakeVisible (launchBox);
    int id = 1;
    for (const auto& c : kLaunch)
        launchBox.addItem (c.text, id++);
    launchBox.setTooltip ("When a clip or scene starts after you click it");
    launchBox.onChange = [this] {
        const int index = launchBox.getSelectedId() - 1;
        if (index < 0 || index >= static_cast<int> (std::size (kLaunch)))
            return;
        const auto ticks = launchTicks (kLaunch[index], session.song());
        if (ticks != session.song().launchQuantise)
            session.edit ("Change Clip Start Time", [ticks] (Song& song) { song.launchQuantise = ticks; });
    };

    addAndMakeVisible (recordButton);
    recordButton.setClickingTogglesState (true);
    recordButton.setTooltip ("Write what the Clip grid plays to the Timeline. Click again, or stop, to finish.");
    recordButton.onClick = [this] { session.setJamRecording (recordButton.getToggleState()); };

    addAndMakeVisible (stopAllButton);
    stopAllButton.setTooltip ("Stop all clips. The Timeline plays again.");
    stopAllButton.onClick = [this] { session.stopAllSlots(); };

    syncControls();
}

ClipGridView::~ClipGridView()
{
    session.removeChangeListener (this);
}

void ClipGridView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    syncControls();
    clampScroll();
    repaint();
}

void ClipGridView::syncControls()
{
    const auto& t = currentTheme();
    title.setFont (t.font (15.0f, true));
    title.setColour (juce::Label::textColourId, t.colour ("text"));
    launchLabel.setFont (t.font (13.0f));
    launchLabel.setColour (juce::Label::textColourId, t.colour ("textMuted"));

    int id = 1, selected = 0;
    for (const auto& c : kLaunch)
    {
        if (launchTicks (c, session.song()) == session.song().launchQuantise)
            selected = id;
        ++id;
    }
    launchBox.setSelectedId (selected, juce::dontSendNotification);

    recordButton.setToggleState (session.isJamRecording(), juce::dontSendNotification);
    recordButton.setColour (juce::TextButton::buttonOnColourId, t.colour ("recording"));
    recordButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

void ClipGridView::refresh()
{
    const bool blink = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
    bool busy = blink != blinkOn;
    blinkOn = blink;
    if (session.isPlaying())
        busy = true;
    if (busy)
        repaint();
}

void ClipGridView::resized()
{
    auto top = getLocalBounds().removeFromTop (kHeader).reduced (14, 10);
    stopAllButton.setBounds (top.removeFromRight (84));
    top.removeFromRight (8);
    recordButton.setBounds (top.removeFromRight (156));
    top.removeFromRight (16);
    launchBox.setBounds (top.removeFromRight (140));
    top.removeFromRight (6);
    launchLabel.setBounds (top.removeFromRight (90));
    title.setBounds (top);
    clampScroll();
}

juce::Rectangle<int> ClipGridView::gridArea() const
{
    return getLocalBounds().withTrimmedTop (kHeader + kNames).withTrimmedLeft (kSceneColumn).withTrimmedBottom (kStopRow + 8).withTrimmedRight (10);
}

int ClipGridView::columnWidth() const
{
    const int tracks = juce::jmax (1, static_cast<int> (session.song().tracks.size()));
    return juce::jlimit (118, 200, gridArea().getWidth() / tracks);
}

int ClipGridView::visibleScenes() const
{
    return juce::jmax (1, gridArea().getHeight() / kRow);
}

void ClipGridView::clampScroll()
{
    const int tracks = static_cast<int> (session.song().tracks.size());
    scrollX = juce::jlimit (0, juce::jmax (0, tracks * columnWidth() - gridArea().getWidth()), scrollX);
    scrollY = juce::jlimit (0, juce::jmax (0, session.song().sceneCount() * kRow - gridArea().getHeight()), scrollY);
}

juce::Rectangle<int> ClipGridView::cell (int track, int scene) const
{
    const auto area = gridArea();
    const int count = session.song().sceneCount();
    const int w = columnWidth();
    int x = track < 0 ? 0 : area.getX() + track * w - scrollX;
    int width = track < 0 ? kSceneColumn : w;
    if (scene < 0)
        return { x, kHeader, width, kNames };
    if (scene >= count)
        return { x, juce::jmin (area.getBottom(), area.getY() + count * kRow) + 4, width, kStopRow }; // under the last row
    return { x, area.getY() + scene * kRow - scrollY, width, kRow };
}

ClipGridView::Hit ClipGridView::hitTest (juce::Point<int> p) const
{
    Hit h;
    const auto area = gridArea();
    const int tracks = static_cast<int> (session.song().tracks.size());
    const int count = session.song().sceneCount();
    if (p.y < kHeader)
        return h;

    if (p.x < kSceneColumn)
        h.track = -1;
    else if (p.x < area.getRight())
    {
        const int t = (p.x - area.getX() + scrollX) / columnWidth();
        if (t >= tracks)
            return h;
        h.track = t;
    }
    else
        return h;

    if (p.y < area.getY())
        h.scene = -1;
    else if (cell (0, count).withX (0).withWidth (getWidth()).contains (p.x, p.y))
        h.scene = count;
    else if (p.y >= area.getBottom())
        return {};
    else
    {
        const int s = (p.y - area.getY() + scrollY) / kRow;
        if (s >= count)
            return {};
        h.scene = s;
    }
    h.onButton = p.x - cell (h.track, h.scene).getX() < 34;
    return h;
}

void ClipGridView::drawSlot (juce::Graphics& g, juce::Rectangle<float> box, const Track& track, int scene, const SlotStatus& status)
{
    const auto& t = currentTheme();
    const auto colour = t.trackColour (utf8 (track.colour));
    const auto clip = slotOf (track, scene);
    const bool playing = status.playing == scene;
    const bool waiting = status.pending == scene;
    const bool selected = clip != nullptr && session.selectedClips().count (clip->id) != 0;
    const auto button = box.withWidth (30.0f);

    if (clip == nullptr)
    {
        g.setColour (t.colour ("background"));
        g.fillRoundedRectangle (box, 6.0f);
        g.setColour (t.colour ("line"));
        g.drawRoundedRectangle (box, 6.0f, 1.0f);
        // An empty slot stops the track, as on a hardware pad grid.
        if (status.playing >= 0 || status.pending >= 0)
            icons::draw (g, icons::Icon::stop, button.reduced (9.0f), t.colour ("textDim"));
        return;
    }

    g.setColour (playing ? colour : colour.withAlpha (0.34f));
    g.fillRoundedRectangle (box, 6.0f);

    // A small picture of the notes.
    if (! clip->notes.empty() && clip->length > 0)
    {
        int low = 127, high = 0;
        for (const auto& n : clip->notes)
        {
            low = juce::jmin (low, static_cast<int> (n.pitch));
            high = juce::jmax (high, static_cast<int> (n.pitch));
        }
        const auto area = box.withTrimmedLeft (34.0f).withTrimmedRight (6.0f).withTrimmedTop (box.getHeight() * 0.55f).withTrimmedBottom (5.0f);
        const float range = static_cast<float> (juce::jmax (1, high - low));
        g.setColour ((playing ? t.colour ("accentText") : colour).withAlpha (playing ? 0.65f : 0.8f));
        for (const auto& n : clip->notes)
        {
            const float x = area.getX() + area.getWidth() * static_cast<float> (n.start) / static_cast<float> (clip->length);
            const float w = juce::jmax (2.0f, area.getWidth() * static_cast<float> (n.length) / static_cast<float> (clip->length));
            const float y = area.getBottom() - 2.0f - (area.getHeight() - 2.0f) * static_cast<float> (n.pitch - low) / range;
            g.fillRect (x, y, juce::jmin (w, area.getRight() - x), 2.0f);
        }
    }

    g.setColour (playing ? t.colour ("accentText") : t.colour ("text"));
    g.setFont (t.font (13.0f, true));
    g.drawText (clip->name.empty() ? juce::String ("Clip") : utf8 (clip->name), box.withTrimmedLeft (34.0f).withTrimmedRight (6.0f).withHeight (box.getHeight() * 0.58f),
                juce::Justification::centredLeft, true);

    if (playing)
    {
        // How far the loop is.
        const double phase = std::fmod (session.position() - status.launchTick, static_cast<double> (clip->length)) / static_cast<double> (clip->length);
        const auto bar = box.withTrimmedLeft (34.0f).withTrimmedRight (6.0f).removeFromBottom (3.0f).translated (0.0f, -2.0f);
        g.setColour (t.colour ("accentText").withAlpha (0.9f));
        g.fillRect (bar.withWidth (bar.getWidth() * static_cast<float> (phase < 0.0 ? phase + 1.0 : phase)));
    }

    const bool lit = playing || (waiting && blinkOn);
    icons::draw (g, icons::Icon::play, button.reduced (8.0f), playing ? t.colour ("accentText") : (lit ? t.colour ("ok") : t.colour ("text").withAlpha (0.75f)));

    if (waiting)
    {
        g.setColour (t.colour ("ok").withAlpha (blinkOn ? 1.0f : 0.35f));
        g.drawRoundedRectangle (box, 6.0f, 2.0f);
    }
    else if (selected)
    {
        g.setColour (t.colour ("text"));
        g.drawRoundedRectangle (box, 6.0f, 1.5f);
    }
}

void ClipGridView::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    const auto& song = session.song();
    const int tracks = static_cast<int> (song.tracks.size());
    const int count = song.sceneCount();
    const auto area = gridArea();

    if (tracks == 0)
    {
        g.setColour (t.colour ("textDim"));
        g.setFont (t.font (15.0f));
        g.drawText ("Add a track to use the Clip grid.", getLocalBounds().withTrimmedTop (kHeader), juce::Justification::centred);
        return;
    }

    std::vector<SlotStatus> status;
    for (const auto& tr : song.tracks)
        status.push_back (session.slotStatus (tr.id));

    // Slots.
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (area);
        for (int c = 0; c < tracks; ++c)
            for (int s = 0; s < count; ++s)
            {
                const auto box = cell (c, s);
                if (box.intersects (area))
                    drawSlot (g, box.toFloat().reduced (3.0f), song.tracks[static_cast<std::size_t> (c)], s, status[static_cast<std::size_t> (c)]);
            }
    }

    // Scene names, with a launch button.
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (0, area.getY(), kSceneColumn, area.getHeight());
        for (int s = 0; s < count; ++s)
        {
            const auto box = cell (-1, s).toFloat().reduced (3.0f).withTrimmedLeft (11.0f);
            bool any = false, allPlaying = true;
            for (int c = 0; c < tracks; ++c)
                if (slotOf (song.tracks[static_cast<std::size_t> (c)], s) != nullptr)
                {
                    any = true;
                    allPlaying = allPlaying && status[static_cast<std::size_t> (c)].playing == s;
                }
            g.setColour (t.colour ("panelRaised"));
            g.fillRoundedRectangle (box, 6.0f);
            // The rows that the controller's pads start.
            if (const int rows = session.surfacePadRows(); s >= session.surfaceScene() && s < session.surfaceScene() + rows)
            {
                g.setColour (t.colour ("accent"));
                g.fillRoundedRectangle (box.withWidth (3.0f).translated (-8.0f, 0.0f), 1.5f);
            }
            icons::draw (g, icons::Icon::play, box.withWidth (30.0f).reduced (8.0f),
                         any && allPlaying ? t.colour ("ok") : t.colour (any ? "text" : "textDim").withAlpha (any ? 0.8f : 0.5f));
            g.setColour (t.colour (any ? "text" : "textDim"));
            g.setFont (t.font (13.0f));
            g.drawText (sceneName (song, s), box.withTrimmedLeft (32.0f).withTrimmedRight (4.0f), juce::Justification::centredLeft, true);
        }
    }

    // Track names above, stop buttons below.
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (area.getX(), kHeader, area.getWidth(), getHeight() - kHeader);
        for (int c = 0; c < tracks; ++c)
        {
            const auto& tr = song.tracks[static_cast<std::size_t> (c)];
            const auto colour = t.trackColour (utf8 (tr.colour));
            const auto name = cell (c, -1).toFloat().reduced (3.0f, 4.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (name.withHeight (3.0f).translated (0.0f, name.getHeight() - 3.0f), 1.5f);
            g.setColour (t.colour (tr.id == session.selectedTrack() ? "text" : "textMuted"));
            g.setFont (t.font (13.0f, tr.id == session.selectedTrack()));
            g.drawText (utf8 (tr.name), name.withTrimmedLeft (4.0f).withTrimmedBottom (4.0f), juce::Justification::centredLeft, true);

            const auto stop = cell (c, count).toFloat().reduced (3.0f);
            const auto& st = status[static_cast<std::size_t> (c)];
            const bool active = st.playing >= 0 || st.pending >= 0;
            g.setColour (t.colour ("panelRaised"));
            g.fillRoundedRectangle (stop, 6.0f);
            icons::draw (g, icons::Icon::stop, stop.withWidth (30.0f).reduced (8.0f), t.colour (active ? "text" : "textDim").withAlpha (active ? 1.0f : 0.5f));
            g.setColour (t.colour (active ? "textMuted" : "textDim"));
            g.setFont (t.font (12.0f));
            g.drawText (st.pending == -1 ? "Stops soon" : (st.playing >= 0 ? "Stop" : "Timeline"), stop.withTrimmedLeft (32.0f), juce::Justification::centredLeft, true);
        }
    }
}

void ClipGridView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    scrollX -= static_cast<int> (wheel.deltaX * 240.0f);
    scrollY -= static_cast<int> (wheel.deltaY * 240.0f);
    clampScroll();
    repaint();
}

void ClipGridView::mouseDown (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.getPosition());
    if (! h.valid())
        return;
    const auto& song = session.song();
    const int count = song.sceneCount();

    if (h.track < 0)
    {
        if (h.scene < 0 || h.scene >= count)
            return;
        if (e.mods.isPopupMenu())
            sceneMenu (h.scene);
        else
            session.launchScene (h.scene);
        return;
    }

    const auto& track = song.tracks[static_cast<std::size_t> (h.track)];
    if (h.scene < 0)
    {
        session.selectTrack (track.id);
        return;
    }
    if (h.scene >= count)
    {
        session.stopSlot (track.id);
        return;
    }

    const auto clip = slotOf (track, h.scene);
    if (e.mods.isPopupMenu())
    {
        session.selectTrack (track.id);
        if (clip != nullptr)
            session.selectClips ({ clip->id });
        slotMenu (track.id, h.scene);
        return;
    }
    if (clip == nullptr)
    {
        session.selectTrack (track.id);
        if (h.onButton)
            session.stopSlot (track.id);
        return;
    }
    if (h.onButton)
    {
        session.launchSlot (track.id, h.scene);
        return;
    }
    session.selectTrack (track.id);
    session.selectClips ({ clip->id });
    if (session.editedClip() != 0)
        session.openInEditor (clip->id); // the open editor follows the selection
}

void ClipGridView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.getPosition());
    const auto& song = session.song();
    if (! h.valid() || h.scene < 0 || h.scene >= song.sceneCount())
        return;
    if (h.track < 0)
    {
        renameScene (h.scene);
        return;
    }
    const auto& track = song.tracks[static_cast<std::size_t> (h.track)];
    if (const auto clip = slotOf (track, h.scene))
    {
        if (! h.onButton)
            session.openInEditor (clip->id);
    }
    else
        newClip (track.id, h.scene);
}

void ClipGridView::newClip (TrackId track, int scene)
{
    ClipId made = 0;
    session.edit ("New Clip", [&] (Song& song) {
        Clip c;
        c.name = "Clip";
        c.length = song.timeSignature.ticksPerBar();
        made = edit::setSlot (song, track, scene, c);
    });
    session.selectTrack (track);
    session.selectClips ({ made });
    session.openInEditor (made);
}

void ClipGridView::slotMenu (TrackId track, int scene)
{
    const auto* t = edit::findTrack (session.song(), track);
    if (t == nullptr)
        return;
    const auto clip = slotOf (*t, scene);

    // A Timeline clip of this track that can go in the slot: the selected one, else the one at the playhead.
    ClipPtr source;
    for (const auto& c : t->clips)
        if (session.selectedClips().count (c->id) != 0)
            source = c;
    if (source == nullptr)
        for (const auto& c : t->clips)
            if (session.position() >= static_cast<double> (c->start) && session.position() < static_cast<double> (c->end()))
                source = c;

    juce::PopupMenu m;
    if (clip != nullptr)
    {
        m.addItem (1, "Start");
        m.addItem (2, "Edit notes");
        m.addItem (3, "Rename");
        m.addSeparator();
        juce::PopupMenu length;
        const auto bar = session.song().timeSignature.ticksPerBar();
        for (const int bars : { 1, 2, 4, 8 })
            length.addItem (20 + bars, juce::String (bars) + (bars == 1 ? " bar" : " bars"), true, clip->length == bar * bars);
        m.addSubMenu ("Length", length);
        m.addItem (4, "Copy to the Timeline at the playhead");
        m.addItem (5, "Copy to the next free slot");
        m.addSeparator();
        m.addItem (6, "Delete");
    }
    else
    {
        m.addItem (10, "New clip");
        m.addItem (11, source != nullptr ? "Copy from the Timeline: " + (source->name.empty() ? juce::String ("clip") : utf8 (source->name)) : juce::String ("Copy from the Timeline"),
                   source != nullptr);
    }

    juce::Component::SafePointer<ClipGridView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, track, scene, clip, source] (int r) {
        if (safe == nullptr || r == 0)
            return;
        auto& session = safe->session;
        const auto bar = session.song().timeSignature.ticksPerBar();
        switch (r)
        {
            case 1: session.launchSlot (track, scene); break;
            case 2: session.openInEditor (clip->id); break;
            case 3: safe->renameSlot (track, scene); break;
            case 4:
            {
                const Tick at = (static_cast<Tick> (session.position()) / bar) * bar;
                session.edit ("Copy Clip to Timeline", [&] (Song& song) {
                    Clip c = *clip;
                    c.id = 0;
                    c.start = at;
                    edit::clearRange (song, track, c.start, c.end());
                    edit::addClip (song, track, c);
                });
                break;
            }
            case 5:
                session.edit ("Copy Clip", [&] (Song& song) {
                    const auto* t = edit::findTrack (song, track);
                    int free = scene + 1;
                    while (t != nullptr && slotOf (*t, free) != nullptr)
                        ++free;
                    Clip c = *clip;
                    c.id = 0;
                    edit::setSlot (song, track, free, c);
                });
                break;
            case 6: session.edit ("Delete Clip", [&] (Song& song) { edit::clearSlot (song, track, scene); }); break;
            case 10: safe->newClip (track, scene); break;
            case 11:
                if (source != nullptr)
                    session.edit ("Copy Clip to Clip Grid", [&] (Song& song) {
                        Clip c = *source;
                        c.id = 0;
                        // A looping pattern goes in as one round of its loop.
                        if (c.loopLength > 0 && c.loopLength < c.length)
                        {
                            c.length = c.loopLength;
                            std::erase_if (c.notes, [&] (const Note& n) { return n.start >= c.length; });
                            std::erase_if (c.controls, [&] (const ControlEvent& e) { return e.at >= c.length; });
                        }
                        c.loopLength = 0;
                        edit::setSlot (song, track, scene, c);
                    });
                break;
            default:
                if (r > 20 && r <= 28)
                    session.edit ("Change Clip Length", [&] (Song& song) {
                        edit::modifyClip (song, clip->id, [&] (Clip& c) { c.length = bar * (r - 20); });
                    });
                break;
        }
    });
}

void ClipGridView::sceneMenu (int scene)
{
    juce::PopupMenu m;
    m.addItem (1, "Start scene");
    m.addItem (2, "Rename");
    m.addItem (3, "Put the controller pads on this row", session.surfacePadRows() > 0);
    juce::Component::SafePointer<ClipGridView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, scene] (int r) {
        if (safe == nullptr)
            return;
        if (r == 1) safe->session.launchScene (scene);
        if (r == 2) safe->renameScene (scene);
        if (r == 3) safe->session.setSurfaceScene (scene);
    });
}

void ClipGridView::startRename (juce::Rectangle<int> bounds, const juce::String& text, std::function<void (std::string)> done)
{
    nameEditor = std::make_unique<juce::TextEditor>();
    addAndMakeVisible (*nameEditor);
    nameEditor->setFont (currentTheme().font (13.0f));
    nameEditor->setText (text, false);
    nameEditor->setBounds (bounds.reduced (3));
    nameEditor->selectAll();
    nameEditor->grabKeyboardFocus();

    juce::Component::SafePointer<ClipGridView> safe (this);
    const auto close = [safe] {
        juce::MessageManager::callAsync ([safe] {
            if (safe != nullptr)
                safe->nameEditor = nullptr;
        });
    };
    auto commit = [this, close, done = std::move (done)] {
        if (nameEditor == nullptr)
            return;
        const auto name = nameEditor->getText().trim().toStdString();
        nameEditor->onFocusLost = nullptr;
        close();
        done (name);
    };
    nameEditor->onReturnKey = commit;
    nameEditor->onFocusLost = commit;
    nameEditor->onEscapeKey = [this, close] {
        nameEditor->onFocusLost = nullptr;
        close();
    };
}

void ClipGridView::renameScene (int scene)
{
    startRename (cell (-1, scene).withTrimmedLeft (36), sceneName (session.song(), scene), [this, scene] (std::string name) {
        session.edit ("Rename Scene", [&] (Song& song) {
            if (song.scenes.size() <= static_cast<std::size_t> (scene))
                song.scenes.resize (static_cast<std::size_t> (scene) + 1);
            song.scenes[static_cast<std::size_t> (scene)] = name; // empty = back to the number
        });
    });
}

void ClipGridView::renameSlot (TrackId track, int scene)
{
    const auto* t = edit::findTrack (session.song(), track);
    const auto clip = t != nullptr ? slotOf (*t, scene) : nullptr;
    if (clip == nullptr)
        return;
    startRename (cell (edit::trackIndex (session.song(), track), scene).withTrimmedLeft (30), utf8 (clip->name), [this, id = clip->id] (std::string name) {
        if (! name.empty())
            session.edit ("Rename Clip", [&] (Song& song) { edit::modifyClip (song, id, [&] (Clip& c) { c.name = name; }); });
    });
}

} // namespace arranger::ui

#include "ClipGridView.h"
#include "../FileDialogs.h"
#include "../AudioFiles.h"

#include "arranger/core/TimeStretch.h"
#include "StyleRuleEditor.h"

#include "Icons.h"

#include <cmath>

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

    AudioClipPtr audioSlotOf (const Track& t, int scene)
    {
        return scene >= 0 && static_cast<std::size_t> (scene) < t.audioSlots.size() ? t.audioSlots[static_cast<std::size_t> (scene)] : nullptr;
    }

    bool isAudio (const Track& t) { return t.output.kind == TrackOutput::Kind::audio; }

    ClipPtr slotOf (const Track& t, int scene)
    {
        return scene >= 0 && static_cast<std::size_t> (scene) < t.slots.size() ? t.slots[static_cast<std::size_t> (scene)] : nullptr;
    }
}

ClipGridView::ClipGridView (Session& s) : session (s)
{
    session.addChangeListener (this);


    addAndMakeVisible (title);
    title.setText (TRANS ("Loops"), juce::dontSendNotification);
    title.setInterceptsMouseClicks (false, false);

    addAndMakeVisible (launchLabel);
    launchLabel.setText (TRANS ("Loops start:"), juce::dontSendNotification);
    launchLabel.setJustificationType (juce::Justification::centredRight);
    launchLabel.setInterceptsMouseClicks (false, false);

    addAndMakeVisible (launchBox);
    int id = 1;
    for (const auto& c : kLaunch)
        launchBox.addItem (c.text, id++);
    launchBox.setTooltip (TRANS ("When a clip or scene starts after you click it"));
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
    recordButton.setTooltip (TRANS ("Write what the Loops view plays to the Timeline. Click again, or stop, to finish."));
    recordButton.onClick = [this] { session.setJamRecording (recordButton.getToggleState()); };

    addAndMakeVisible (fromTimelineButton);
    fromTimelineButton.setTooltip (TRANS ("Put the Timeline in Loops: each part of the song becomes a scene"));
    fromTimelineButton.onClick = [this] { fillFromTimeline(); };

    addAndMakeVisible (stopAllButton);
    stopAllButton.setTooltip (TRANS ("Stop all clips. The Timeline plays again."));
    stopAllButton.onClick = [this] { session.stopAllSlots(); };

    addChildComponent (chordButton);
    chordButton.setTooltip (TRANS ("The chord that the style follows. Hold keys below the split on your keyboard, or click to select a chord."));
    chordButton.onClick = [this] { chordMenu(); };

    syncControls();
}

void ClipGridView::chordMenu()
{
    static constexpr const char* kRoots[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static constexpr ChordType kCommon[] = { ChordType::major, ChordType::major7, ChordType::major6, ChordType::minor, ChordType::minor7, ChordType::minor6,
                                             ChordType::seventh, ChordType::seventhSus4, ChordType::sus4, ChordType::minor7Flat5, ChordType::diminished,
                                             ChordType::diminished7, ChordType::augmented, ChordType::seventh9, ChordType::minorMajor7 };
    juce::PopupMenu m;
    const auto now = session.chord();
    for (int root = 0; root < 12; ++root)
    {
        juce::PopupMenu types;
        for (const auto type : kCommon)
        {
            const Chord c { root, type, -1 };
            types.addItem (1 + root * 100 + static_cast<int> (type), style_chord::name (c), true, c.root == now.root && c.type == now.type);
        }
        m.addSubMenu (kRoots[root], types, true, nullptr, root == now.root);
    }
    m.addSeparator();
    m.addItem (5000, TRANS ("No chord (rhythm only)"), true, now.type == ChordType::cancel);
    juce::Component::SafePointer<ClipGridView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (chordButton), [safe] (int r) {
        if (safe == nullptr || r == 0)
            return;
        if (r == 5000)
            safe->session.setChord ({ 0, ChordType::cancel, -1 });
        else
            safe->session.setChord ({ (r - 1) / 100, static_cast<ChordType> ((r - 1) % 100), -1 });
    });
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

    const bool isStyle = session.song().style.has_value();
    title.setText (isStyle ? "Style" : "Loops", juce::dontSendNotification);
    chordButton.setButtonText ("Chord: " + juce::String (style_chord::name (session.chord())));
    if (chordButton.isVisible() != isStyle)
    {
        chordButton.setVisible (isStyle);
        resized();
    }

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
    top.removeFromRight (8);
    fromTimelineButton.setBounds (top.removeFromRight (128));
    top.removeFromRight (16);
    launchBox.setBounds (top.removeFromRight (140));
    top.removeFromRight (6);
    launchLabel.setBounds (top.removeFromRight (90));
    if (chordButton.isVisible())
    {
        title.setBounds (top.removeFromLeft (60));
        chordButton.setBounds (top.removeFromLeft (150));
    }
    else
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
    return juce::jmax (40, juce::roundToInt (static_cast<float> (juce::jlimit (118, 200, gridArea().getWidth() / tracks)) * zoomX));
}

int ClipGridView::rowHeight() const
{
    return juce::jmax (20, juce::roundToInt (static_cast<float> (kRow) * zoomY));
}

int ClipGridView::visibleScenes() const
{
    return juce::jmax (1, gridArea().getHeight() / rowHeight());
}

void ClipGridView::clampScroll()
{
    const int tracks = static_cast<int> (session.song().tracks.size());
    scrollX = juce::jlimit (0, juce::jmax (0, tracks * columnWidth() - gridArea().getWidth()), scrollX);
    scrollY = juce::jlimit (0, juce::jmax (0, session.song().sceneCount() * rowHeight() - gridArea().getHeight()), scrollY);
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
        return { x, juce::jmin (area.getBottom(), area.getY() + count * rowHeight()) + 4, width, kStopRow }; // under the last row
    return { x, area.getY() + scene * rowHeight() - scrollY, width, rowHeight() };
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
        const int s = (p.y - area.getY() + scrollY) / rowHeight();
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
    const auto ink = t.isDark ? juce::Colour (0xff111417) : t.colour ("text");
    const auto clip = slotOf (track, scene);
    const auto audio = isAudio (track) ? audioSlotOf (track, scene) : nullptr;
    const bool filled = clip != nullptr || audio != nullptr;
    const ClipId clipId = audio != nullptr ? audio->id : (clip != nullptr ? clip->id : 0);
    const Tick clipLength = audio != nullptr ? audio->length : (clip != nullptr ? clip->length : 0);
    const std::string clipName = audio != nullptr ? audio->name : (clip != nullptr ? clip->name : std::string());
    const bool playing = status.playing == scene;
    const bool waiting = status.pending == scene;
    const bool selected = filled && session.selectedClips().count (clipId) != 0;
    const auto button = box.withWidth (30.0f);

    if (! filled)
    {
        g.setColour (t.colour ("background"));
        g.fillRoundedRectangle (box, 6.0f);
        const bool chosen = track.id == chosenTrack && scene == chosenScene;
        g.setColour (chosen ? colour : t.colour ("line"));
        g.drawRoundedRectangle (box, 6.0f, chosen ? 1.5f : 1.0f);
        // An empty slot stops the track, as on a hardware pad grid.
        if (status.playing >= 0 || status.pending >= 0)
            icons::draw (g, icons::Icon::stop, button.reduced (9.0f), t.colour ("textDim"));
        return;
    }

    g.setColour (clipFill (colour, playing || selected));
    g.fillRoundedRectangle (box, 5.0f);

    // A small picture of the sound.
    if (audio != nullptr)
    {
        const auto& p = slotPeaks (audio->file);
        const auto area = box.withTrimmedLeft (34.0f).withTrimmedRight (6.0f).withTrimmedTop (box.getHeight() * 0.5f).withTrimmedBottom (5.0f);
        if (p.empty())
        {
            g.setColour (t.colour ("warning"));
            g.setFont (t.font (10.0f));
            g.drawText ("File not found", area, juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (ink.withAlpha (0.82f));
            const float w = area.getWidth() / static_cast<float> (p.size());
            for (std::size_t i = 0; i < p.size(); ++i)
            {
                const float h = juce::jmax (1.0f, p[i] * area.getHeight());
                g.fillRect (area.getX() + static_cast<float> (i) * w, area.getCentreY() - h / 2.0f, juce::jmax (1.0f, w - 0.5f), h);
            }
        }
    }
    // A small picture of the notes.
    else if (! clip->notes.empty() && clip->length > 0)
    {
        int low = 127, high = 0;
        for (const auto& n : clip->notes)
        {
            low = juce::jmin (low, static_cast<int> (n.pitch));
            high = juce::jmax (high, static_cast<int> (n.pitch));
        }
        const auto area = box.withTrimmedLeft (34.0f).withTrimmedRight (6.0f).withTrimmedTop (box.getHeight() * 0.55f).withTrimmedBottom (5.0f);
        const float range = static_cast<float> (juce::jmax (1, high - low));
        g.setColour (ink.withAlpha (0.82f));
        for (const auto& n : clip->notes)
        {
            const float x = area.getX() + area.getWidth() * static_cast<float> (n.start) / static_cast<float> (clip->length);
            const float w = juce::jmax (2.0f, area.getWidth() * static_cast<float> (n.length) / static_cast<float> (clip->length));
            const float y = area.getBottom() - 2.0f - (area.getHeight() - 2.0f) * static_cast<float> (n.pitch - low) / range;
            g.fillRect (x, y, juce::jmin (w, area.getRight() - x), 2.0f);
        }
    }

    g.setColour (ink);
    g.setFont (t.font (13.0f, true));
    g.drawText (clipName.empty() ? juce::String ("Clip") : utf8 (clipName), box.withTrimmedLeft (34.0f).withTrimmedRight (6.0f).withHeight (box.getHeight() * 0.58f),
                juce::Justification::centredLeft, true);

    if (playing)
    {
        // How far the loop is.
        const double phase = std::fmod (session.position() - status.launchTick, static_cast<double> (clipLength)) / static_cast<double> (clipLength);
        const auto bar = box.withTrimmedLeft (34.0f).withTrimmedRight (6.0f).removeFromBottom (3.0f).translated (0.0f, -2.0f);
        g.setColour (ink.withAlpha (0.60f));
        g.fillRect (bar.withWidth (bar.getWidth() * static_cast<float> (phase < 0.0 ? phase + 1.0 : phase)));
    }

    const bool lit = playing || (waiting && blinkOn);
    icons::draw (g, icons::Icon::play, button.reduced (8.0f), playing ? ink : (lit ? t.colour ("ok") : ink.withAlpha (0.75f)));

    if (waiting)
    {
        g.setColour (t.colour ("ok").withAlpha (blinkOn ? 1.0f : 0.35f));
        g.drawRoundedRectangle (box, 6.0f, 2.0f);
    }
    else if (selected)
    {
        g.setColour (t.colour ("selection"));
        g.drawRoundedRectangle (box, 5.0f, 1.5f);
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
        g.drawText ("Add a track to use Loops.", getLocalBounds().withTrimmedTop (kHeader), juce::Justification::centred);
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
                    drawSlot (g, box.toFloat().reduced (1.0f), song.tracks[static_cast<std::size_t> (c)], s, status[static_cast<std::size_t> (c)]);
            }
    }

    // Scene names, with a launch button.
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (0, area.getY(), kSceneColumn, area.getHeight());
        for (int s = 0; s < count; ++s)
        {
            const auto box = cell (-1, s).toFloat().reduced (1.0f).withTrimmedLeft (11.0f);
            bool any = false, allPlaying = true;
            for (int c = 0; c < tracks; ++c)
                if (slotOf (song.tracks[static_cast<std::size_t> (c)], s) != nullptr || audioSlotOf (song.tracks[static_cast<std::size_t> (c)], s) != nullptr)
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
            const auto name = cell (c, -1).toFloat().reduced (1.0f, 4.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (name.withHeight (3.0f).translated (0.0f, name.getHeight() - 3.0f), 1.5f);
            g.setColour (t.colour (tr.id == session.selectedTrack() ? "text" : "textMuted"));
            g.setFont (t.font (13.0f, tr.id == session.selectedTrack()));
            g.drawText (utf8 (tr.name), name.withTrimmedLeft (4.0f).withTrimmedBottom (4.0f), juce::Justification::centredLeft, true);

            const auto stop = cell (c, count).toFloat().reduced (1.0f);
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

void ClipGridView::zoomHorizontally (double factor)
{
    zoomX = juce::jlimit (0.5f, 2.5f, zoomX * static_cast<float> (factor));
    clampScroll();
    repaint();
}

void ClipGridView::zoomVertically (double factor)
{
    zoomY = juce::jlimit (0.5f, 2.5f, zoomY * static_cast<float> (factor));
    clampScroll();
    repaint();
}

void ClipGridView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const float delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : wheel.deltaX;
        const float factor = std::pow (1.15f, delta * 8.0f);
        zoomX = juce::jlimit (0.5f, 2.5f, zoomX * factor);
        zoomY = juce::jlimit (0.5f, 2.5f, zoomY * factor);
        clampScroll();
        repaint();
        return;
    }
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
    chosenTrack = track.id;
    chosenScene = h.scene;
    repaint();

    if (isAudio (track))
    {
        // An audio track: its slots hold audio loops.
        const auto audio = audioSlotOf (track, h.scene);
        session.selectTrack (track.id);
        if (audio != nullptr && (e.mods.isPopupMenu() || ! h.onButton))
            session.selectClips ({ audio->id });
        if (e.mods.isPopupMenu())
            audioSlotMenu (track.id, h.scene);
        else if (h.onButton && audio != nullptr)
            session.toggleSlot (track.id, h.scene);
        else if (h.onButton)
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
        session.toggleSlot (track.id, h.scene);
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
    if (isAudio (track))
    {
        if (audioSlotOf (track, h.scene) == nullptr)
            chooseAudioFor (track.id, h.scene);
        return;
    }
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

const std::vector<float>& ClipGridView::slotPeaks (const std::string& file)
{
    auto it = peaks.find (file);
    if (it == peaks.end())
    {
        std::vector<float> p;
        if (const auto sample = session.sampleFor (file); sample != nullptr && sample->frames() > 0)
        {
            constexpr std::size_t kBins = 48;
            p.assign (kBins, 0.0f);
            const auto frames = sample->frames();
            for (const auto& channel : sample->channels)
                for (std::size_t i = 0; i < frames; ++i)
                {
                    auto& bin = p[std::min (kBins - 1, i * kBins / frames)];
                    bin = std::max (bin, std::abs (channel[i]));
                }
            const float top = std::max (0.05f, *std::max_element (p.begin(), p.end()));
            for (auto& v : p)
                v /= top;
        }
        it = peaks.emplace (file, std::move (p)).first;
    }
    return it->second;
}

bool ClipGridView::addAudioToSlot (const juce::File& file, TrackId trackId, int scene)
{
    const auto path = file.getFullPathName().toStdString();
    const auto sample = session.sampleFor (path);
    if (sample == nullptr || sample->frames() == 0 || scene < 0)
        return false;
    const double seconds = sample->seconds();
    ClipId added = 0;
    TrackId target = trackId;
    session.edit ("Add Audio Loop", [&] (Song& s) {
        const auto* t = edit::findTrack (s, target);
        if (t == nullptr || t->output.kind != TrackOutput::Kind::audio)
            target = edit::addAudioTrack (s, file.getFileNameWithoutExtension().toStdString(), edit::trackIndex (s, target)).id;
        AudioClip clip;
        clip.name = file.getFileNameWithoutExtension().toStdString();
        clip.file = path;
        // A loop is a number of whole bars. Its tempo comes from its length, and it then follows the song.
        const double songBpm = s.baseTempo();
        const Tick bar = s.timeSignature.ticksPerBar();
        const double bpm = stretch::tempoFromLength (seconds, songBpm, s.timeSignature.numerator);
        const double beats = seconds * (bpm > 0.0 ? bpm : songBpm) / 60.0;
        const auto ticks = static_cast<Tick> (std::llround (beats * static_cast<double> (kTicksPerQuarter)));
        clip.bpm = bpm;
        clip.length = std::max<Tick> (bar, bpm > 0.0 ? ((ticks + bar / 2) / bar) * bar : ((ticks + bar - 1) / bar) * bar);
        added = edit::setAudioSlot (s, target, scene, std::move (clip));
    });
    session.selectTrack (target);
    if (added != 0)
        session.selectClips ({ added });
    return added != 0;
}

void ClipGridView::chooseAudioFor (TrackId track, int scene)
{
    chooser = std::make_unique<juce::FileChooser> ("Choose an audio loop", file_dialogs::folder ("audio", file_dialogs::defaultFolder()), audio_files::wildcard());
    juce::Component::SafePointer<ClipGridView> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, track, scene] (const juce::FileChooser& fc) {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;
        file_dialogs::remember ("audio", fc.getResult());
        if (! safe->addAudioToSlot (fc.getResult(), track, scene))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Can't import audio", safe->session.sampleProblem (fc.getResult().getFullPathName().toStdString()));
    });
}

void ClipGridView::addClipboardItems (juce::PopupMenu& m, bool filled) const
{
    const auto& c = session.clipboard;
    m.addItem (kCutItem, "Cut", filled);
    m.addItem (kCopyItem, "Copy", filled);
    m.addItem (kPasteItem, "Paste", c.kind == clipboard::Content::Kind::clips || c.kind == clipboard::Content::Kind::notes);
    m.addSeparator();
}

bool ClipGridView::hasChosenSlot() const
{
    return edit::findTrack (session.song(), chosenTrack) != nullptr && chosenScene >= 0 && chosenScene < session.song().sceneCount();
}

void ClipGridView::fillFromTimeline()
{
    const auto& song = session.song();
    if (song.style.has_value())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "From Timeline",
                                                "The Loops view holds a style now. This works only in a project without a style.");
        return;
    }
    if (song.contentEnd() <= 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "From Timeline", "The Timeline has no clips yet.");
        return;
    }
    bool hasSlots = false;
    for (const auto& t : song.tracks)
        for (const auto& c : t.slots)
            hasSlots = hasSlots || c != nullptr;

    fromTimelineWindow = std::make_unique<juce::AlertWindow> (
        "Put the Timeline in Loops",
        juce::String ("The song is cut into parts, and each part becomes a scene. Each MIDI track gets a clip in each scene where it plays.")
            + (hasSlots ? "\n\nClips already in those scenes are replaced. Use Undo to take it back." : ""),
        juce::MessageBoxIconType::NoIcon, this);
    fromTimelineWindow->addComboBox ("bars", { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" }, "Bars in each scene");
    if (auto* box = fromTimelineWindow->getComboBoxComponent ("bars"))
        box->setSelectedItemIndex (2);
    fromTimelineWindow->addButton ("Fill", 1, juce::KeyPress (juce::KeyPress::returnKey));
    fromTimelineWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    fromTimelineWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result) {
        if (result == 1 && fromTimelineWindow != nullptr)
            if (auto* box = fromTimelineWindow->getComboBoxComponent ("bars"))
            {
                const int bars = 1 << juce::jlimit (0, 4, box->getSelectedItemIndex());
                const Tick length = session.song().timeSignature.ticksPerBar() * bars;
                session.edit ("Fill Loops from Timeline", [length] (Song& s) { edit::slotsFromTimeline (s, length); });
            }
        fromTimelineWindow.reset();
    }));
}

void ClipGridView::copySlot (bool cut)
{
    if (! hasChosenSlot())
        return;
    auto content = clipboard::copySlot (session.song(), chosenTrack, chosenScene);
    if (content.empty())
        return;
    session.clipboard = std::move (content);
    if (cut)
        session.edit ("Cut Clip", [track = chosenTrack, scene = chosenScene] (Song& s) { edit::clearSlot (s, track, scene); });
}

void ClipGridView::pasteSlot()
{
    if (! hasChosenSlot())
        return;
    const auto content = session.clipboard;
    ClipId id = 0;
    session.edit ("Paste Clip", [&] (Song& s) { id = clipboard::pasteSlot (s, content, chosenTrack, chosenScene); });
    if (id != 0)
        session.selectClips ({ id });
}

void ClipGridView::audioSlotMenu (TrackId track, int scene)
{
    const auto* t = edit::findTrack (session.song(), track);
    if (t == nullptr)
        return;
    const auto clip = audioSlotOf (*t, scene);

    // A Timeline clip of this track that can go in the slot: the selected one, else the one at the playhead.
    AudioClipPtr source;
    for (const auto& c : t->audio)
        if (session.selectedClips().count (c->id) != 0)
            source = c;
    if (source == nullptr)
        for (const auto& c : t->audio)
            if (session.position() >= static_cast<double> (c->start) && session.position() < static_cast<double> (c->end()))
                source = c;

    juce::PopupMenu m;
    const auto bar = session.song().timeSignature.ticksPerBar();
    addClipboardItems (m, clip != nullptr);
    if (clip != nullptr)
    {
        m.addItem (1, "Start");
        m.addItem (3, "Rename");
        m.addSeparator();
        juce::PopupMenu length;
        for (const int bars : { 1, 2, 4, 8 })
            length.addItem (20 + bars, juce::String (bars) + (bars == 1 ? " bar" : " bars"), true, clip->length == bar * bars);
        m.addSubMenu ("Loop length", length);
        m.addItem (7, "Follow the song tempo", true, clip->bpm > 0.0);
        m.addItem (4, "Copy to the Timeline at the playhead");
        m.addSeparator();
        m.addItem (6, "Delete");
    }
    else
    {
        m.addItem (10, "Add an audio file...");
        m.addItem (11, source != nullptr ? "Copy from the Timeline: " + (source->name.empty() ? juce::String ("clip") : utf8 (source->name)) : juce::String ("Copy from the Timeline"),
                   source != nullptr);
    }

    juce::Component::SafePointer<ClipGridView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, track, scene, clip, source, bar] (int r) {
        if (safe == nullptr || r == 0)
            return;
        if (r == kCutItem || r == kCopyItem)
        {
            safe->copySlot (r == kCutItem);
            return;
        }
        if (r == kPasteItem)
        {
            safe->pasteSlot();
            return;
        }
        auto& session = safe->session;
        switch (r)
        {
            case 1: session.launchSlot (track, scene); break;
            case 3:
                safe->startRename (safe->cell (edit::trackIndex (session.song(), track), scene).withTrimmedLeft (30), utf8 (clip->name), [safe, id = clip->id] (std::string name) {
                    if (safe != nullptr && ! name.empty())
                        safe->session.edit ("Rename Clip", [&] (Song& song) { edit::modifyAudioClip (song, id, [&] (AudioClip& c) { c.name = name; }); });
                });
                break;
            case 4:
            {
                const Tick at = (static_cast<Tick> (session.position()) / bar) * bar;
                session.edit ("Copy Clip to Timeline", [&] (Song& song) {
                    AudioClip c = *clip;
                    c.id = 0;
                    c.start = at;
                    edit::addAudioClip (song, track, std::move (c));
                });
                break;
            }
            case 6: session.edit ("Delete Clip", [&] (Song& song) { edit::clearSlot (song, track, scene); }); break;
            case 7:
            {
                const auto sample = session.sampleFor (clip->file);
                const double found = sample != nullptr ? stretch::tempoFromLength (sample->seconds(), session.song().baseTempo(), session.song().timeSignature.numerator) : 0.0;
                session.edit (clip->bpm > 0.0 ? "Keep File Tempo" : "Follow Song Tempo", [&] (Song& song) {
                    edit::modifyAudioClip (song, clip->id, [&] (AudioClip& c) { c.bpm = c.bpm > 0.0 ? 0.0 : found; });
                });
                break;
            }
            case 10: safe->chooseAudioFor (track, scene); break;
            case 11:
                if (source != nullptr)
                    session.edit ("Copy Clip to Loops", [&] (Song& song) {
                        AudioClip c = *source;
                        c.id = 0;
                        c.fadeIn = c.fadeOut = 0;
                        c.length = std::max<Tick> (bar, ((c.length + bar / 2) / bar) * bar); // a loop is whole bars
                        edit::setAudioSlot (song, track, scene, std::move (c));
                    });
                break;
            default:
                if (r > 20 && r <= 28)
                    session.edit ("Change Loop Length", [&] (Song& song) {
                        edit::modifyAudioClip (song, clip->id, [&] (AudioClip& c) { c.length = bar * (r - 20); });
                    });
                break;
        }
    });
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
    addClipboardItems (m, clip != nullptr);
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

    if (t->styleChannel > 0)
    {
        m.addSeparator();
        m.addItem (30, "Chord rules...");
    }

    juce::Component::SafePointer<ClipGridView> safe (this);
    const auto index = static_cast<int> (t - session.song().tracks.data());
    const auto anchor = localAreaToGlobal (cell (index, scene));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, track, scene, clip, source, anchor] (int r) {
        if (safe == nullptr || r == 0)
            return;
        if (r == kCutItem || r == kCopyItem)
        {
            safe->copySlot (r == kCutItem);
            return;
        }
        if (r == kPasteItem)
        {
            safe->pasteSlot();
            return;
        }
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
            case 30:
                juce::CallOutBox::launchAsynchronously (std::make_unique<StyleRuleEditor> (session, track, scene), anchor, nullptr);
                break;
            case 11:
                if (source != nullptr)
                    session.edit ("Copy Clip to Loops", [&] (Song& song) {
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

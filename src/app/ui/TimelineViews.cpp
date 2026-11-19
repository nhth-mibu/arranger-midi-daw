#include "TimelineViews.h"

#include "AppTooltip.h"
#include "Icons.h"

#include "arranger/core/Notation.h"
#include "../AudioFiles.h"

#include "Panels.h"

#include "arranger/core/AudioToMidi.h"
#include "arranger/core/ChordFinder.h"
#include "arranger/core/ChordInput.h"
#include "arranger/core/TimeStretch.h"

namespace arranger::ui
{

namespace
{
    juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
    const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");

    constexpr int kPeakFrames = 256;

    /** An audio clip's place on the Timeline, as a clip without notes (for bounds and hit tests). */
    Clip shellOf (const AudioClip& a)
    {
        Clip c;
        c.id = a.id;
        c.start = a.start;
        c.length = a.length;
        return c;
    }

    icons::Icon iconForTrack (const Track& t)
    {
        if (t.output.kind == TrackOutput::Kind::audio)
            return icons::Icon::waveform;
        if (t.output.kind == TrackOutput::Kind::sampler || notation::isDrumTrack (t))
            return icons::Icon::drums;

        const auto text = utf8 (t.name + " " + t.sound.name + " " + t.instrument.name).toLowerCase();
        const auto has = [&text] (const char* word) { return text.contains (word); };
        if (has ("drum") || has ("kit") || has ("percussion") || has ("so-cal") || has ("socal"))
            return icons::Icon::drums;
        if (has ("guitar"))
            return icons::Icon::guitar;
        if (has ("bass"))
            return icons::Icon::bass;
        if (has ("brass") || has ("trumpet") || has ("trombone") || has ("sax") || has ("horn"))
            return icons::Icon::brass;
        if (has ("piano") || has ("keys"))
            return icons::Icon::electricPiano;
        if (has ("lead") || has ("synth") || has ("pad") || has ("arp"))
            return icons::Icon::synth;

        const int program = t.sound.program;
        if (program >= 24 && program <= 31) return icons::Icon::guitar;
        if (program >= 32 && program <= 39) return icons::Icon::bass;
        if (program >= 56 && program <= 63) return icons::Icon::brass;
        if (program >= 80 && program <= 87) return icons::Icon::synth;
        if (program >= 0 && program <= 7) return icons::Icon::electricPiano;
        return icons::Icon::instrument;
    }

    juce::String outputSummary (const Track& t)
    {
        switch (t.output.kind)
        {
            case TrackOutput::Kind::none: return "No output";
            case TrackOutput::Kind::soundFont:
                return "SoundFont" + dot + (t.sound.name.empty() ? juce::String ("Ch ") + juce::String (t.output.channel) : utf8 (t.sound.name));
            case TrackOutput::Kind::sampler:
                return "Sampler" + dot + (t.kit.pads.empty() ? juce::String ("no samples") : juce::String (t.kit.pads.size()) + " pads");
            case TrackOutput::Kind::audio:
                return "Audio" + dot + (t.audio.empty() ? juce::String ("no clips") : juce::String (t.audio.size()) + (t.audio.size() == 1 ? " clip" : " clips"));
            case TrackOutput::Kind::plugin:
                return "Plugin" + dot + (t.instrument.empty() ? juce::String ("no instrument") : utf8 (t.instrument.name));
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
    shortcuts::setCommand (addButton, "Add Track");
    addButton.onClick = [this] {
        juce::PopupMenu m;
        m.addItem (1, "MIDI Track");
        m.addItem (2, "Audio Track");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton), [this] (int r) {
            if (r == 1) addTrack();
            else if (r == 2) addAudioTrack();
        });
    };

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

    g.setColour (t.colour ("panel"));
    g.fillRect (0, 0, getWidth(), geo.rulerHeight);
    g.setColour (t.colour ("lineStrong").withAlpha (0.65f));
    g.fillRect (0, geo.rulerHeight - 1, getWidth(), 1);

    // The chord lane's name, across from the lane.
    {
        auto lane = juce::Rectangle<int> (0, geo.chordLaneTop(), getWidth(), geo.chordLaneHeight);
        g.setColour (t.colour ("line").withAlpha (0.7f));
        g.fillRect (lane.removeFromTop (1));
        lane = lane.reduced (20, 0);
        g.setColour (t.colour ("textMuted"));
        g.setFont (t.font (12.0f, true));
        g.drawText ("Chords", lane, juce::Justification::centredLeft);
        if (const auto* ct = edit::findTrack (song, chord_input::chordTrack (song, &session.deviceLibrary())))
        {
            g.setColour (t.colour ("textDim"));
            g.setFont (t.font (11.0f));
            g.drawText ("on " + utf8 (ct->name), lane.withTrimmedLeft (52), juce::Justification::centredLeft, true);
        }
    }

    auto header = getLocalBounds().removeFromTop (geo.chordLaneTop()).reduced (20, 0);
    g.setColour (t.colour ("text"));
    g.setFont (t.font (15.0f, true));
    g.drawText ("Tracks", header, juce::Justification::centredLeft);

    const auto count = juce::Rectangle<float> (static_cast<float> (header.getX() + 62), static_cast<float> (header.getCentreY() - 10), 30.0f, 20.0f);
    g.setColour (t.colour ("panelRaised"));
    g.fillRoundedRectangle (count, 6.0f);
    g.setColour (t.colour ("textMuted"));
    g.setFont (t.font (12.0f, true));
    g.drawText (juce::String (song.tracks.size()), count, juce::Justification::centred);

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

        const bool isSelected = track.id == selected;
        g.setColour (isSelected ? t.colour ("laneSelected") : t.colour (i % 2 == 0 ? "lane" : "laneAlt"));
        g.fillRect (row);
        if (isSelected)
        {
            g.setColour (t.trackColour (utf8 (track.colour)).withAlpha (0.55f));
            g.fillRect (0, row.getY(), 2, row.getHeight());
        }

        const auto c = t.trackColour (utf8 (track.colour));
        g.setColour (c.withAlpha (isSelected ? 1.0f : 0.8f));
        g.fillRoundedRectangle (juce::Rectangle<float> (15.0f, static_cast<float> (row.getY() + 11), 3.0f, static_cast<float> (row.getHeight() - 22)), 1.5f);

        const float iconSize = juce::jmin (22.0f, static_cast<float> (row.getHeight() - 10));
        icons::draw (g, iconForTrack (track),
                     juce::Rectangle<float> (29.0f, static_cast<float> (row.getCentreY()) - iconSize * 0.5f, iconSize, iconSize),
                     c.withAlpha (isSelected ? 1.0f : 0.88f));

        auto text = row.withTrimmedLeft (58).withTrimmedRight (3 * 28 + 20).reduced (0, 12);
        if (row.getHeight() < 56)
        {
            // Compact lanes: one line is all that fits.
            g.setColour (t.colour ("text"));
            g.setFont (t.font (14.0f, true));
            g.drawText (utf8 (track.name), text, juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (t.colour ("text"));
            g.setFont (t.font (15.0f, true));
            g.drawText (utf8 (track.name), text.removeFromTop (text.getHeight() / 2), juce::Justification::bottomLeft, true);
            g.setColour (t.colour ("textMuted"));
            g.setFont (t.font (12.5f));
            g.drawText (outputSummary (track), text.withTrimmedTop (3), juce::Justification::topLeft, true);
        }

        // Mute, solo, record-arm
        const char* labels[] = { "M", "S", "R" };
        const bool states[] = { track.mixer.mute, track.mixer.solo, track.armed || (track.id == recordTarget && session.isRecording()) };
        const juce::Colour onColours[] = { t.colour ("warning"), t.colour ("accent"), t.colour ("recording") };
        for (int b = 0; b < 3; ++b)
        {
            const auto r = buttonBounds (i, b).toFloat();
            g.setColour (states[b] ? onColours[b].withAlpha (0.20f) : t.colour ("panelRaised"));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (states[b] ? onColours[b].withAlpha (0.85f) : t.colour ("lineStrong"));
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
            g.setColour (states[b] ? onColours[b] : t.colour ("textMuted"));
            g.setFont (t.font (12.0f, true));
            g.drawText (labels[b], r, juce::Justification::centred);
        }

        g.setColour (t.colour ("line").withAlpha (0.55f));
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

void TrackList::addAudioTrack()
{
    const int after = edit::trackIndex (session.song(), session.selectedTrack());
    TrackId added = 0;
    session.edit ("Add Audio Track", [&] (Song& s) {
        int count = 1;
        for (const auto& t : s.tracks)
            count += t.output.kind == TrackOutput::Kind::audio ? 1 : 0;
        added = edit::addAudioTrack (s, "Audio " + std::to_string (count), after).id;
    });
    session.selectTrack (added);
}

void TrackList::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int index = geo.trackAt (e.y);
    if (index >= 0 && index < static_cast<int> (session.song().tracks.size()) && e.x < getWidth() - 3 * 28 - 20)
        rename (session.song().tracks[static_cast<std::size_t> (index)].id);
    else if (index >= static_cast<int> (session.song().tracks.size()))
        addTrack();
}

void TrackList::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if ((e.mods.isCommandDown() || e.mods.isCtrlDown()) && e.mods.isShiftDown())
    {
        if (onTrackZoom)
        {
            const float delta = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : w.deltaX;
            onTrackZoom (std::pow (1.15, delta * 8.0));
        }
        return;
    }

    const int viewport = std::max (1, getHeight() - geo.rulerHeight);
    const int maxScroll = geo.maxScrollY (static_cast<int> (session.song().tracks.size()), viewport);
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
    nameEditor->setBounds (rowBounds (index).withTrimmedLeft (56).withTrimmedRight (3 * 28 + 20).withSizeKeepingCentre (getWidth() - 3 * 28 - 76, 30));
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
    juce::PopupMenu lines;
    const int shown = geo.automationOf (id);
    lines.addItem (20, "Show Volume Line", true, shown == 1);
    lines.addItem (21, "Show Pan Line", true, shown == 2);
    // The sends go out as MIDI controllers: only SoundFont and MIDI port tracks have them.
    const auto& menuTrack = session.song().tracks[static_cast<std::size_t> (edit::trackIndex (session.song(), id))];
    const bool sends = menuTrack.output.kind == TrackOutput::Kind::soundFont || menuTrack.output.kind == TrackOutput::Kind::midiPort;
    if (sends)
    {
        lines.addItem (25, "Show Reverb Send Line", true, shown == 3);
        lines.addItem (26, "Show Chorus Send Line", true, shown == 4);
    }
    // The plugin controls that have a line, then the menu to give a line to one more control.
    const auto controlName = [&menuTrack] (int lane) {
        int slot = 0;
        const auto* c = menuTrack.controlLine (lane, &slot);
        const auto& plugin = slot < 0 ? menuTrack.instrument : menuTrack.effects[static_cast<std::size_t> (slot)];
        return utf8 (plugin.name) + ": " + (c->name.empty() ? "Control " + juce::String (c->control + 1) : utf8 (c->name));
    };
    for (int lane = Track::kFirstControlLane; lane < menuTrack.laneCount(); ++lane)
        lines.addItem (100 + lane, "Show " + controlName (lane) + " Line", true, shown == lane + 1);
    juce::PopupMenu controls;
    const auto addPlugin = [&] (const PluginSlot& plugin, int slot) {
        const auto live = plugin.empty() ? nullptr : session.pluginAt (id, slot);
        if (live == nullptr)
            return;
        // Long lists go in pages of 40.
        constexpr int kPage = 40;
        const auto& list = live->plugin().getParameters();
        juce::PopupMenu all, page;
        int inPage = 0, firstOfPage = 0, total = 0;
        const auto closePage = [&] (int last) {
            if (inPage > 0)
                all.addSubMenu (juce::String (firstOfPage + 1) + " to " + juce::String (last + 1), page);
            page = {};
            inPage = 0;
        };
        const int count = std::min (list.size(), 5000);
        for (int i = 0; i < count; ++i)
        {
            if (! list[i]->isAutomatable())
                continue;
            if (inPage == 0)
                firstOfPage = i;
            auto name = list[i]->getName (48);
            page.addItem (10000 + (slot + 1) * 5000 + i, name.isEmpty() ? "Control " + juce::String (i + 1) : name);
            ++total;
            if (++inPage == kPage)
                closePage (i);
        }
        if (total <= kPage)
            controls.addSubMenu (utf8 (plugin.name), page, total > 0);
        else
        {
            closePage (count - 1);
            controls.addSubMenu (utf8 (plugin.name), all);
        }
    };
    if (menuTrack.output.kind == TrackOutput::Kind::plugin)
        addPlugin (menuTrack.instrument, -1);
    if (Session::canHaveEffects (menuTrack))
        for (std::size_t i = 0; i < menuTrack.effects.size(); ++i)
            addPlugin (menuTrack.effects[i], static_cast<int> (i));
    lines.addSubMenu ("Plugin Control", controls, controls.getNumItems() > 0);
    lines.addItem (22, "Hide", shown != 0);
    lines.addSeparator();
    lines.addItem (23, "Remove Volume Automation", ! menuTrack.volumeAutomation.empty());
    lines.addItem (24, "Remove Pan Automation", ! menuTrack.panAutomation.empty());
    if (sends)
    {
        lines.addItem (27, "Remove Reverb Send Automation", ! menuTrack.reverbAutomation.empty());
        lines.addItem (28, "Remove Chorus Send Automation", ! menuTrack.chorusAutomation.empty());
    }
    for (int lane = Track::kFirstControlLane; lane < menuTrack.laneCount(); ++lane)
        lines.addItem (300 + lane, "Remove " + controlName (lane) + " Automation");
    m.addSubMenu ("Automation", lines);
    m.addSeparator();
    m.addItem (5, "Delete Track");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, id] (int r) {
        const int index = edit::trackIndex (session.song(), id);
        if (r >= 20 && r <= 22)
        {
            if (r == 22) geo.automationShown.erase (id);
            else geo.automationShown[id] = r - 19;
            geo.changed();
        }
        else if (r == 25 || r == 26)
        {
            geo.automationShown[id] = r - 22;
            geo.changed();
        }
        else if (r >= 100 && r < 300)
        {
            geo.automationShown[id] = r - 100 + 1;
            geo.changed();
        }
        else if (r >= 300 && r < 10000)
        {
            // The numbers of the plugin lines change: hide the line that shows.
            if (geo.automationOf (id) > Track::kFirstControlLane)
                geo.automationShown.erase (id);
            session.edit ("Remove Automation", [id, lane = r - 300] (Song& s) { edit::clearAutomation (s, id, lane); });
            geo.changed();
        }
        else if (r >= 10000)
        {
            const int slot = (r - 10000) / 5000 - 1, control = (r - 10000) % 5000;
            std::string name;
            if (const auto live = session.pluginAt (id, slot); live != nullptr && control < live->plugin().getParameters().size())
                name = live->plugin().getParameters()[control]->getName (48).toStdString();
            int lane = -1;
            session.edit ("Add Automation Line", [&] (Song& s) { lane = edit::addControlLine (s, id, slot, control, name); });
            if (lane >= 0)
                geo.automationShown[id] = lane + 1;
            geo.changed();
        }
        else if (r == 23 || r == 24 || r == 27 || r == 28)
            session.edit ("Remove Automation", [id, lane = r <= 24 ? r - 23 : r - 25] (Song& s) { edit::clearAutomation (s, id, lane); });
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
    setTooltip ("Double-click a lane to add a clip, double-click a clip to edit its notes. Drag in the ruler's lower half to set the loop. "
                "Cmd/Ctrl+Wheel zooms time; add Shift to zoom track height.");
}

Arrangement::~Arrangement() { session.removeChangeListener (this); }

// ---- the chord lane ------------------------------------------------------------------------
/** The chord box on the chord lane: type chords from a bar, with the next chord to try under it. */
class Arrangement::ChordEntry final : public juce::Component
{
public:
    ChordEntry (Session& s, Tick from) : session (s), start (from)
    {
        const auto& t = currentTheme();
        const Tick bar = std::max<Tick> (1, s.song().timeSignature.ticksPerBar());
        editor.setTextToShowWhenEmpty ("Bar " + juce::String (from / bar + 1) + ":  Am F | C G   or   vi IV I V", t.colour ("textDim"));
        editor.setFont (t.font (13.0f));
        editor.onReturnKey = [this] { write(); };
        editor.onEscapeKey = [this] { if (onClose) onClose(); };
        editor.onFocusLost = [this] {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ChordEntry> (this)] {
                if (safe != nullptr && ! safe->hasKeyboardFocus (true) && safe->onClose)
                    safe->onClose();
            });
        };
        editor.onTextChange = [this] { suggest(); };
        addAndMakeVisible (editor);
        suggest();
    }

    std::function<void()> onClose;

    void focus() { editor.grabKeyboardFocus(); }

    void paint (juce::Graphics& g) override
    {
        const auto& t = currentTheme();
        g.setColour (t.colour ("panelRaised"));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
        g.setColour (t.colour ("accent").withAlpha (0.7f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
        g.setColour (t.colour ("textMuted"));
        g.setFont (t.font (11.0f));
        g.drawText (hint, getLocalBounds().reduced (10, 6).removeFromBottom (16), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        editor.setBounds (r.removeFromTop (26));
        r.removeFromTop (6);
        auto row = r.removeFromTop (24);
        for (auto& b : buttons)
        {
            const int w = std::max (52, juce::GlyphArrangement::getStringWidthInt (currentTheme().font (12.0f), b->getButtonText()) + 16);
            if (w > row.getWidth())
                break;
            b->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (4);
        }
    }

private:
    void suggest()
    {
        const auto& song = session.song();
        const auto key = chord_input::songKey (song);
        auto before = chord_input::chordsIn (editor.getText().toStdString(), key);
        if (before.empty())
            for (const auto& c : chord_input::laneChords (song, &session.deviceLibrary()))
                if (c.at < start && c.chord)
                    before.push_back (*c.chord);
        if (before.size() > 3)
            before.erase (before.begin(), before.end() - 3);
        buttons.clear();
        for (const auto& sug : chord_input::suggestNext (before, key, 5))
        {
            auto b = std::make_unique<juce::TextButton> (utf8 (sug.name));
            b->setTooltip (utf8 (sug.roman) + ": " + utf8 (sug.why));
            b->setWantsKeyboardFocus (false);
            b->onClick = [this, name = utf8 (sug.name)] {
                const auto text = editor.getText().trimEnd();
                editor.setText (text.isEmpty() ? name : text + " " + name, true);
                editor.moveCaretToEnd();
                editor.grabKeyboardFocus();
            };
            addAndMakeVisible (*b);
            buttons.push_back (std::move (b));
        }
        hint = "Enter writes, Esc closes. Names or numerals; | ends a bar.";
        resized();
        repaint();
    }

    void write()
    {
        const auto text = editor.getText().trim();
        if (text.isEmpty() || session.song().tracks.empty())
        {
            if (onClose)
                onClose();
            return;
        }
        const auto& song = session.song();
        const auto typed = chord_input::typeChords (text.toStdString(), chord_input::songKey (song), song.timeSignature, start);
        if (! typed.marks.empty())
            session.edit ("Type Chords", [&] (Song& s) { chord_input::writeChords (s, start, typed.end, typed.marks, &session.deviceLibrary()); });
        if (onClose)
            onClose();
    }

    Session& session;
    Tick start = 0;
    juce::TextEditor editor;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
    juce::String hint;
};

void Arrangement::drawChordLane (juce::Graphics& g, int left, int right)
{
    const auto& t = currentTheme();
    const auto& song = session.song();
    const int top = geo.chordLaneTop(), h = geo.chordLaneHeight;
    g.setColour (t.colour ("timeline").withAlpha (0.6f));
    g.fillRect (left, top, right - left, h);
    g.setColour (t.colour ("line").withAlpha (0.7f));
    g.fillRect (left, top, right - left, 1);
    const auto lane = chord_input::laneChords (song, &session.deviceLibrary());
    if (lane.empty())
    {
        g.setColour (t.colour ("textDim"));
        g.setFont (t.font (11.5f));
        g.drawText ("Double-click to type chords (Am F C G or vi IV I V)", juce::Rectangle<int> (left + 10, top, 420, h), juce::Justification::centredLeft, true);
        return;
    }
    for (const auto& c : lane)
    {
        const float x1 = geo.tickToX (static_cast<double> (c.at)), x2 = geo.tickToX (static_cast<double> (c.end));
        if (x2 < static_cast<float> (left) || x1 > static_cast<float> (right))
            continue;
        const auto box = juce::Rectangle<float> (x1 + 1.0f, static_cast<float> (top) + 3.0f, std::max (4.0f, x2 - x1 - 2.0f), static_cast<float> (h) - 6.0f);
        const bool sel = c.at == selectedChord;
        g.setColour (t.colour ("accent").withAlpha (sel ? 0.34f : (c.chord ? 0.13f : 0.05f)));
        g.fillRoundedRectangle (box, 4.0f);
        if (sel)
        {
            g.setColour (t.colour ("accent"));
            g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);
        }
        g.setColour (t.colour (c.chord ? "text" : "textMuted"));
        g.setFont (t.font (12.0f, true));
        g.drawText (utf8 (c.text), box.reduced (5.0f, 0.0f), juce::Justification::centredLeft, true);
    }
}

void Arrangement::chordLaneDown (const juce::MouseEvent& e)
{
    const auto at = static_cast<Tick> (std::max (0.0, geo.xToTick (static_cast<float> (e.x))));
    const auto lane = chord_input::laneChords (session.song(), &session.deviceLibrary());
    const auto* c = chord_input::chordAt (lane, at);
    selectedChord = c != nullptr ? c->at : -1;
    if (e.mods.isPopupMenu())
    {
        repaint();
        showChordMenu (at);
        return;
    }
    if (c != nullptr)
        session.locate (c->at); // the Smart Chords tool follows the playhead
    repaint();
}

void Arrangement::showChordMenu (Tick at)
{
    const auto& song = session.song();
    const Tick bar = std::max<Tick> (1, song.timeSignature.ticksPerBar());
    const Tick barStart = at / bar * bar;
    const auto key = chord_input::songKey (song);
    const auto lane = chord_input::laneChords (song, &session.deviceLibrary());
    const auto* c = chord_input::chordAt (lane, at);
    const auto here = c != nullptr ? *c : chord_input::LaneChord {};

    std::vector<Chord> before;
    for (const auto& l : lane)
        if (l.at <= at && l.chord)
            before.push_back (*l.chord);
    if (before.size() > 3)
        before.erase (before.begin(), before.end() - 3);
    const auto next = chord_input::suggestNext (before, key, 6);
    const Tick nextAt = c != nullptr ? std::max (barStart + bar, (here.at / bar + 1) * bar) : barStart;

    juce::PopupMenu m;
    m.addItem (1, "Type Chords from Bar " + juce::String (barStart / bar + 1) + "...");
    juce::PopupMenu nextMenu;
    for (std::size_t i = 0; i < next.size(); ++i)
        nextMenu.addItem (100 + static_cast<int> (i), utf8 (next[i].name) + "   " + utf8 (next[i].roman) + "  -  " + utf8 (next[i].why));
    m.addSubMenu ((c != nullptr ? "Next Chord in Bar " : "Chord for Bar ") + juce::String (nextAt / bar + 1), nextMenu, ! next.empty());
    m.addSeparator();
    m.addItem (2, "Reharmonize " + (c != nullptr ? utf8 (here.text) : juce::String ("Chord")) + "...", c != nullptr && here.chord.has_value());
    m.addItem (3, "Delete " + (c != nullptr ? utf8 (here.text) : juce::String ("Chord")), c != nullptr);
    m.addSeparator();
    m.addItem (4, "Find Chords from the Music", ! song.tracks.empty());
    m.addItem (5, "Smart Chords...");
    m.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<Arrangement> (this), at, barStart, bar, here, next, nextAt] (int r) {
        if (safe == nullptr || r == 0)
            return;
        auto& self = *safe;
        auto& s = self.session;
        if (r == 1)
            self.typeChordsAt (barStart);
        else if (r == 2 && self.onReharmonize)
            self.onReharmonize (here.at);
        else if (r == 3)
        {
            const auto lib = &s.deviceLibrary();
            s.edit ("Delete Chord", [&] (Song& song) {
                if (auto* t = edit::findTrack (song, chord_input::chordTrack (song, lib)))
                    std::erase_if (t->marks, [&] (const ScoreMark& mk) { return mk.kind == ScoreMark::Kind::chord && mk.at == here.at; });
            });
        }
        else if (r == 4)
        {
            const auto& song = s.song();
            std::vector<const Track*> tracks;
            for (const auto& t : song.tracks)
                if (! arranging::isDrumTrack (t, &s.deviceLibrary()))
                    tracks.push_back (&t);
            const Tick end = (std::max<Tick> (song.contentEnd(), bar) + bar - 1) / bar * bar;
            const auto marks = chords::find (tracks, song.timeSignature, 0, end, bar / 2, theory::parseKey (song.key));
            const auto lib = &s.deviceLibrary();
            s.edit ("Find Chords", [&] (Song& changed) { chord_input::writeChords (changed, 0, end, marks, lib); });
        }
        else if (r == 5 && self.onChordTools)
            self.onChordTools (static_cast<int> (barStart / bar) + 1);
        else if (r >= 100 && r - 100 < static_cast<int> (next.size()))
        {
            const auto& sug = next[static_cast<std::size_t> (r - 100)];
            const std::vector<ScoreMark> marks { { ScoreMark::Kind::chord, nextAt, sug.name } };
            const auto lib = &s.deviceLibrary();
            s.edit ("Add Chord", [&] (Song& song) { chord_input::writeChords (song, nextAt, nextAt + bar, marks, lib); });
        }
        juce::ignoreUnused (at);
    });
}

void Arrangement::typeChordsAt (Tick at)
{
    const Tick bar = std::max<Tick> (1, session.song().timeSignature.ticksPerBar());
    const Tick start = at / bar * bar;
    closeChordEntry();
    chordEntry = std::make_unique<ChordEntry> (session, start);
    chordEntry->onClose = [this] {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Arrangement> (this)] {
            if (safe != nullptr)
                safe->closeChordEntry();
        });
    };
    const int w = std::min (380, std::max (240, getWidth() - 20));
    const int x = std::clamp (juce::roundToInt (geo.tickToX (static_cast<double> (start))), 4, std::max (4, getWidth() - w - 4));
    chordEntry->setBounds (x, geo.rulerHeight + 2, w, 90);
    addAndMakeVisible (*chordEntry);
    chordEntry->focus();
}

void Arrangement::closeChordEntry()
{
    chordEntry.reset();
}

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
    // Flush with the lane: parts leave half the previous gap between them.
    constexpr float inset = 0.25f;
    return { x1, static_cast<float> (geo.trackY (trackIndex)) + inset, std::max (4.0f, x2 - x1), static_cast<float> (geo.trackHeight) - inset * 2.0f };
}

Arrangement::Hit Arrangement::hitTest (juce::Point<int> p) const
{
    Hit h;
    h.trackIndex = geo.trackAt (p.y);
    const auto& song = session.song();
    if (h.trackIndex < 0 || h.trackIndex >= static_cast<int> (song.tracks.size()))
        return h;
    const auto& track = song.tracks[static_cast<std::size_t> (h.trackIndex)];
    const auto audioOrder = edit::audioDrawOrder (track); // the top clip gets the mouse
    for (auto it = audioOrder.rbegin(); it != audioOrder.rend(); ++it)
    {
        const auto r = clipBounds (shellOf (**it), h.trackIndex);
        if (r.contains (p.toFloat()))
        {
            h.clip = (*it)->id;
            h.rightEdge = p.x > r.getRight() - 8;
            return h;
        }
    }
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
    const float radius = 2.0f;
    const auto body = r.reduced (0.5f);
    const auto ink = t.isDark ? juce::Colour (0xff111417) : t.colour ("text");
    auto paper = clipFill (c, selected);
    if (ghost)
        paper = paper.withAlpha (0.28f);

    if (selected)
    {
        // A flat selection wash across the part, not a bigger rounded card.
        g.setColour (t.colour ("selection").withAlpha (0.14f));
        g.fillRect (body.expanded (0.0f, 2.0f));
    }
    // Modern flat part: a faint, light outline instead of a heavy rounded border.
    g.setColour (paper);
    g.fillRoundedRectangle (body, radius);
    g.setColour (selected ? t.colour ("selection") : c.darker (0.35f).withAlpha (0.32f));
    g.drawRoundedRectangle (body, radius, selected ? 2.0f : 1.0f);
    // A thin left accent carries the part's identity where the track lanes meet.
    g.setColour (c.darker (0.2f).withAlpha (selected ? 1.0f : 0.6f));
    g.fillRect (body.getX(), body.getY() + 1.0f, 2.0f, juce::jmax (1.0f, body.getHeight() - 2.0f));

    g.saveState();
    g.reduceClipRegion (body.toNearestInt());

    g.setColour (ink.withAlpha (ghost ? 0.2f : 0.95f));
    g.setFont (t.font (11.0f, true));
    g.drawText (utf8 (clip.name), r.reduced (8.0f, 3.0f).removeFromTop (15.0f), juce::Justification::topLeft, true);

    if (! clip.notes.empty())
    {
        int lo = 127, hi = 0;
        for (const auto& n : clip.notes)
        {
            lo = std::min<int> (lo, n.pitch);
            hi = std::max<int> (hi, n.pitch);
        }
        const auto noteArea = r.reduced (5.0f, 5.0f).withTrimmedTop (17.0f);
        const float range = static_cast<float> (std::max (12, hi - lo + 1));
        const float rowH = std::max (1.0f, std::min (2.0f, noteArea.getHeight() / range));
        // A pattern clip shows its first part again and again, with a line at each repeat.
        const Tick loop = clip.loopLength > 0 && clip.loopLength < clip.length ? clip.loopLength : clip.length;
        for (Tick base = 0; base < clip.length; base += loop)
        {
            if (base > 0)
            {
                g.setColour (ink.withAlpha (0.20f));
                g.fillRect (geo.tickToX (static_cast<double> (clip.start + base)), r.getY() + 3.0f, 1.0f, r.getHeight() - 6.0f);
            }
            g.setColour (ink.withAlpha (base == 0 ? 0.88f : 0.55f));
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

    g.setColour (t.colour ("timeline"));
    g.fillRect (getLocalBounds());

    // Lanes: alternate tint, selected track highlighted.
    for (int i = 0; i < static_cast<int> (song.tracks.size()); ++i)
    {
        const auto& track = song.tracks[static_cast<std::size_t> (i)];
        const auto y = geo.trackY (i);
        const bool selected = track.id == session.selectedTrack();
        g.setColour (selected ? t.colour ("laneSelected") : t.colour (i % 2 == 0 ? "lane" : "laneAlt"));
        g.fillRect (0, y, getWidth(), geo.trackHeight);

        if (selected)
        {
            g.setColour (t.trackColour (utf8 (track.colour)).withAlpha (0.55f));
            g.fillRect (0, y, 2, geo.trackHeight);
        }
        g.setColour (t.colour ("line").withAlpha (0.55f));
        g.fillRect (0, y + geo.trackHeight - 1, getWidth(), 1);
    }

    // Grid: a quiet wash on every other bar, then the bar and beat lines.
    for (auto bar = firstBar; bar <= lastBar; ++bar)
    {
        const float x = geo.tickToX (static_cast<double> (bar) * barTicks);
        if (bar % 2 == 0)
        {
            g.setColour (t.colour ("text").withAlpha (0.015f));
            g.fillRect (juce::Rectangle<float> (x, static_cast<float> (rulerH), barW, static_cast<float> (getHeight() - rulerH)));
        }
        g.setColour (t.colour ("gridBar").withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (x, static_cast<float> (rulerH), 1.0f, static_cast<float> (getHeight() - rulerH)));
        if (barW / static_cast<float> (sig.numerator) > 10.0f)
        {
            g.setColour (t.colour ("gridBeat").withAlpha (0.9f));
            for (int b = 1; b < sig.numerator; ++b)
                g.fillRect (juce::Rectangle<float> (x + barW * static_cast<float> (b) / static_cast<float> (sig.numerator), static_cast<float> (rulerH), 1.0f,
                                                    static_cast<float> (getHeight() - rulerH)));
        }
    }

    // A quiet line marks where the last clip ends.
    const float endX = geo.tickToX (static_cast<double> (song.contentEnd()));
    if (endX < static_cast<float> (getWidth()))
    {
        g.setColour (t.colour ("lineStrong").withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float> (endX, static_cast<float> (rulerH), 1.0f, static_cast<float> (getHeight() - rulerH)));
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
        for (const auto& clip : edit::audioDrawOrder (track)) // bottom first: the take that plays is on top
        {
            const bool isSel = selected.count (clip->id) > 0;
            if (drag == Drag::move && isSel)
            {
                drawAudioClip (g, *clip, clipBounds (shellOf (*clip), i), colour, false, true); // where it was
                AudioClip moved = *clip;
                moved.start = std::max<Tick> (0, clip->start + dragDeltaTicks);
                int target = std::clamp (i + dragDeltaTracks, 0, static_cast<int> (song.tracks.size()) - 1);
                if (song.tracks[static_cast<std::size_t> (target)].output.kind != TrackOutput::Kind::audio)
                    target = i; // audio clips stay on audio tracks
                drawAudioClip (g, moved, clipBounds (shellOf (moved), target), colour, true, false);
            }
            else if (drag == Drag::resize && clip->id == dragClip)
            {
                AudioClip resized = *clip;
                resized.length = dragNewLength;
                drawAudioClip (g, resized, clipBounds (shellOf (resized), i), colour, true, false);
            }
            else
            {
                const auto r = clipBounds (shellOf (*clip), i);
                if (r.getRight() >= 0 && r.getX() <= static_cast<float> (getWidth()))
                    drawAudioClip (g, *clip, r, colour, isSel, false);
            }
        }
    }

    // Recording in progress: a red clip growing with the playhead.
    for (int i = 0; i < static_cast<int> (song.tracks.size()); ++i)
        if (const int lane = geo.automationOf (song.tracks[static_cast<std::size_t> (i)].id); lane != 0 && song.tracks[static_cast<std::size_t> (i)].findLine (lane - 1) != nullptr)
            drawAutomation (g, song.tracks[static_cast<std::size_t> (i)], i, lane - 1);

    if (session.isRecording())
    {
        const int index = edit::trackIndex (song, session.recordTarget());
        const auto& take = session.takeInProgress();
        if (index >= 0)
        {
            const bool audio = session.isRecordingAudio();
            const double audioStart = session.audioTakeStart() < 0.0 ? session.position() : session.audioTakeStart();
            const double startTick = audio ? audioStart : (take.empty() ? session.position() : take.front().tick);
            const float x1 = geo.tickToX (audio ? startTick : std::floor (startTick / barTicks) * barTicks);
            const float x2 = geo.tickToX (session.position());
            const auto r = juce::Rectangle<float> (x1, static_cast<float> (geo.trackY (index)) + 0.25f, std::max (4.0f, x2 - x1), static_cast<float> (geo.trackHeight) - 0.5f);
            g.setColour (t.colour ("recording").withAlpha (0.16f));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (t.colour ("recording"));
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.5f);
            if (audio)
            {
                // The level of the take so far.
                const auto& levels = session.audioTakePeaks();
                const double startSeconds = song.tempo.secondsAt (startTick);
                const double secondsPerPeak = Session::kTakePeakFrames / std::max (1.0, session.audioTakeSampleRate());
                for (std::size_t i = 0; i < levels.size(); ++i)
                {
                    const float x = geo.tickToX (song.tempo.tickAtSeconds (startSeconds + static_cast<double> (i) * secondsPerPeak));
                    const float h = std::max (1.0f, std::min (1.0f, levels[i]) * (r.getHeight() - 8.0f));
                    g.fillRect (juce::Rectangle<float> (x, r.getCentreY() - h / 2.0f, 1.0f, h));
                }
            }
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
    g.setColour (t.colour ("text").withAlpha (t.isDark ? 0.05f : 0.35f));
    g.fillRect (0, 0, getWidth(), 1);
    if (song.loop.isValid())
    {
        const float x1 = geo.tickToX (static_cast<double> (song.loop.start));
        const float x2 = geo.tickToX (static_cast<double> (song.loop.end));
        const auto band = juce::Rectangle<float> (x1, 37.0f, x2 - x1, 9.0f);
        g.setColour (t.colour ("accent").withAlpha (song.loop.enabled ? 0.16f : 0.06f));
        g.fillRoundedRectangle (band, 4.5f);
        g.setColour (song.loop.enabled ? t.colour ("accent").withAlpha (0.7f) : t.colour ("lineStrong"));
        g.drawRoundedRectangle (band.reduced (0.5f), 4.5f, 1.0f);
        if (song.loop.enabled)
        {
            g.setColour (t.colour ("accent").withAlpha (0.035f));
            g.fillRect (juce::Rectangle<float> (x1, static_cast<float> (rulerH), x2 - x1, static_cast<float> (getHeight() - rulerH)));
        }
    }
    for (auto bar = firstBar; bar <= lastBar; ++bar)
    {
        if (bar < 0 || bar % labelEvery != 0)
            continue;
        const float x = geo.tickToX (static_cast<double> (bar) * barTicks);
        const bool major = bar % 4 == 0;
        g.setColour (t.colour ("gridBar").withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (x, 30.0f, 1.0f, 6.0f));
        g.setColour (major ? t.colour ("text").withAlpha (0.9f) : t.colour ("textMuted").withAlpha (0.75f));
        g.setFont (t.font (major ? 12.5f : 12.0f, major));
        g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 7, 5, 60, 24), juce::Justification::centredLeft);
    }
    // Song sections (Song Forms): a copper mark at each start and the name after the bar number, where there is room.
    for (const auto& sec : song.sections)
    {
        if (sec.scene >= 0)
            continue;
        const float x = geo.tickToX (static_cast<double> (sec.start));
        const float xEnd = geo.tickToX (static_cast<double> (sec.end));
        if (xEnd < 0.0f || x > static_cast<float> (getWidth()))
            continue;
        g.setColour (t.colour ("accent").withAlpha (0.75f));
        g.fillRect (juce::Rectangle<float> (x, 3.0f, 2.0f, 31.0f));
        const auto barNumber = juce::String (sec.start / static_cast<Tick> (barTicks) + 1);
        const float nameX = x + 7.0f + 8.0f * static_cast<float> (barNumber.length()) + 6.0f;
        const float room = std::min (xEnd, x + barW * static_cast<float> (labelEvery)) - nameX - 4.0f;
        if (room >= 24.0f)
        {
            g.setColour (t.colour ("accent"));
            g.setFont (t.font (11.5f, true));
            g.drawText (utf8 (sec.name), juce::Rectangle<float> (nameX, 5.0f, room, 24.0f), juce::Justification::centredLeft, true);
        }
    }
    drawChordLane (g, 0, getWidth());
    g.setColour (t.colour ("lineStrong").withAlpha (0.75f));
    g.fillRect (0, rulerH - 1, getWidth(), 1);

    // Playhead: a soft copper guide with a small head in the ruler.
    const int x = juce::roundToInt (geo.tickToX (session.position()));
    shownPlayheadX = x;
    const auto head = session.isPlaying() ? t.colour ("playhead") : t.colour ("playhead").withAlpha (0.72f);
    g.setColour (head.withAlpha (0.10f));
    g.fillRect (x - 2, 28, 5, getHeight() - 28);
    g.setColour (head);
    g.fillRect (juce::Rectangle<float> (static_cast<float> (x), 26.0f, 1.5f, static_cast<float> (getHeight() - 26)));
    juce::Path cap;
    cap.addTriangle (static_cast<float> (x) - 4.5f, 18.0f, static_cast<float> (x) + 6.0f, 18.0f, static_cast<float> (x) + 0.75f, 26.0f);
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

void Arrangement::zoomVertically (double factor, float aroundY)
{
    const int oldHeight = geo.trackHeight;
    const int newHeight = std::clamp (juce::roundToInt (static_cast<float> (oldHeight) * static_cast<float> (factor)),
                                      TimelineGeometry::minTrackHeight, TimelineGeometry::maxTrackHeight);
    if (newHeight == oldHeight)
        return;

    if (aroundY < static_cast<float> (geo.rulerHeight))
        aroundY = static_cast<float> (geo.rulerHeight + getHeight()) * 0.5f;
    const double anchor = (aroundY - static_cast<float> (geo.rulerHeight) + static_cast<float> (geo.scrollY))
                          / static_cast<double> (oldHeight);
    geo.trackHeight = newHeight;
    const int viewport = std::max (1, getHeight() - geo.rulerHeight);
    const int maxScroll = geo.maxScrollY (static_cast<int> (session.song().tracks.size()), viewport);
    geo.scrollY = std::clamp (juce::roundToInt (anchor * newHeight - (aroundY - static_cast<float> (geo.rulerHeight))), 0, maxScroll);
    geo.changed();
}

void Arrangement::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        if (e.mods.isShiftDown())
        {
            const float delta = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : w.deltaX;
            zoomVertically (std::pow (1.15, delta * 8.0), static_cast<float> (e.y));
        }
        else
            zoom (std::pow (1.15, w.deltaY * 8.0), static_cast<float> (e.x));
        return;
    }
    const float dx = e.mods.isShiftDown() ? w.deltaY : w.deltaX;
    const float dy = e.mods.isShiftDown() ? 0.0f : w.deltaY;
    geo.scrollTicks = std::max (0.0, geo.scrollTicks - dx * 400.0 / geo.pixelsPerTick);
    const int viewport = std::max (1, getHeight() - geo.rulerHeight);
    const int maxScroll = geo.maxScrollY (static_cast<int> (session.song().tracks.size()), viewport);
    geo.scrollY = std::clamp (geo.scrollY - static_cast<int> (dy * 300.0f), 0, maxScroll);
    geo.changed();
}

void Arrangement::mouseMagnify (const juce::MouseEvent& e, float scale)
{
    if (e.mods.isShiftDown())
        zoomVertically (scale, static_cast<float> (e.y));
    else
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

    if (e.y >= geo.chordLaneTop() && e.y < geo.rulerHeight)
    {
        chordLaneDown (e);
        return;
    }
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

    // A track that shows an automation line: the mouse works on the line, not on the clips.
    if (const int row = geo.trackAt (e.y); row >= 0 && row < static_cast<int> (session.song().tracks.size()))
    {
        const auto& track = session.song().tracks[static_cast<std::size_t> (row)];
        if (const int lane = geo.automationOf (track.id); lane != 0 && track.findLine (lane - 1) != nullptr)
        {
            const int which = lane - 1; // the line: 0 volume, 1 pan, 2 reverb, 3 chorus
            const auto id = track.id;
            session.selectTrack (id);
            int index = automationHit (track, row, which, e.getPosition());
            if (pointTrack != id || pointLane != which)
                selectedPoints.clear();
            pointTrack = id;
            pointLane = which;
            const auto& line = track.automationLine (which);
            if (e.mods.isPopupMenu())
            {
                if (index >= 0 && selectedPoints.count (line[static_cast<std::size_t> (index)].at) == 0)
                    selectedPoints = { line[static_cast<std::size_t> (index)].at };
                const auto at = automationPointAt (e.getPosition(), row, e.mods).at;
                juce::PopupMenu m;
                addClipboardItems (m, ! selectedPoints.empty());
                m.addItem (1, "Delete", ! selectedPoints.empty());
                m.addSeparator();
                m.addItem (2, "Select All Points", ! line.empty());
                repaint();
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, id, at] (int r) {
                    if (handleClipboardItem (r, id, at))
                        return;
                    if (r == 1)
                        deletePoints();
                    else if (r == 2)
                        selectAllPoints();
                });
                return;
            }
            if (index < 0)
            {
                const auto point = automationPointAt (e.getPosition(), row, e.mods);
                session.edit ("Add Automation Point", [&] (Song& s) { index = edit::setAutomationPoint (s, id, which, point); });
                selectedPoints = { point.at };
            }
            else
            {
                const auto at = line[static_cast<std::size_t> (index)].at;
                if (e.mods.isShiftDown())
                {
                    if (selectedPoints.count (at) > 0)
                        selectedPoints.erase (at);
                    else
                        selectedPoints.insert (at);
                }
                else if (selectedPoints.count (at) == 0)
                    selectedPoints = { at };
            }
            drag = Drag::automation;
            automationTrack = id;
            automationIndex = index;
            repaint();
            return;
        }
    }

    const auto h = hitTest (e.getPosition());
    const auto& song = session.song();
    if (h.trackIndex >= 0 && h.trackIndex < static_cast<int> (song.tracks.size()))
        session.selectTrack (song.tracks[static_cast<std::size_t> (h.trackIndex)].id);

    selectedPoints.clear();
    if (h.clip == 0)
    {
        if (! e.mods.isShiftDown())
            session.selectClips ({});
        if (e.mods.isPopupMenu() && h.trackIndex >= 0 && h.trackIndex < static_cast<int> (song.tracks.size()))
        {
            const auto id = song.tracks[static_cast<std::size_t> (h.trackIndex)].id;
            const auto at = std::max<Tick> (0, snap (geo.xToTick (static_cast<float> (e.x)), e.mods));
            juce::PopupMenu m;
            addClipboardItems (m, false);
            m.addSeparator();
            m.addItem (1, "Select All Clips");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, id, at] (int r) {
                if (handleClipboardItem (r, id, at))
                    return;
                if (r == 1)
                {
                    std::set<ClipId> all;
                    for (const auto& t : session.song().tracks)
                    {
                        for (const auto& c : t.clips)
                            all.insert (c->id);
                        for (const auto& a : t.audio)
                            all.insert (a->id);
                    }
                    session.selectClips (all);
                }
            });
            return;
        }
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

    if (e.mods.isPopupMenu() && edit::findAudioClip (song, h.clip) != nullptr)
    {
        showAudioMenu (h.clip);
        return;
    }
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Edit Notes");
        m.addSeparator();
        addClipboardItems (m, true);
        m.addSeparator();
        m.addItem (2, "Duplicate");
        m.addItem (3, "Split at Playhead");
        m.addItem (4, "Join");
        m.addItem (5, "Quantise");
        m.addSeparator();
        m.addItem (6, "Delete");
        const auto here = std::max<Tick> (0, snap (geo.xToTick (static_cast<float> (e.x)), e.mods));
        const auto hereTrack = song.tracks[static_cast<std::size_t> (h.trackIndex)].id;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, clip = h.clip, here, hereTrack] (int r) {
            if (handleClipboardItem (r, hereTrack, here)) return;
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
        Tick start = 0;
        clipSpan (h.clip, start, dragNewLength);
    }
    else
        drag = Drag::move;
}

void Arrangement::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::automation)
    {
        const auto& song = session.song();
        const int row = edit::trackIndex (song, automationTrack);
        if (row < 0)
            return;
        const int which = geo.automationOf (automationTrack) - 1; // the line: 0 volume, 1 pan, 2 reverb, 3 chorus
        if (song.tracks[static_cast<std::size_t> (row)].findLine (which) == nullptr)
            return;
        const auto& line = song.tracks[static_cast<std::size_t> (row)].automationLine (which);
        if (automationIndex < 0 || automationIndex >= static_cast<int> (line.size()))
            return;
        // A point stays between the points next to it.
        auto point = automationPointAt (e.getPosition(), row, e.mods);
        const auto i = static_cast<std::size_t> (automationIndex);
        if (i > 0)
            point.at = std::max (point.at, line[i - 1].at + 1);
        if (i + 1 < line.size())
            point.at = std::min (point.at, line[i + 1].at - 1);
        const auto id = automationTrack;
        const int index = automationIndex;
        // The point keeps its selection when it moves.
        if (selectedPoints.erase (line[i].at) > 0)
            selectedPoints.insert (point.at);
        session.edit ("Move Automation Point", [&] (Song& s) {
            edit::removeAutomationPoint (s, id, which, index);
            automationIndex = edit::setAutomationPoint (s, id, which, point);
        }, "automation-point");
        repaint();
        return;
    }
    const auto& song = session.song();
    switch (drag)
    {
        case Drag::move:
        {
            Tick clipStart = 0, clipLength = 0;
            if (! clipSpan (dragClip, clipStart, clipLength))
                return;
            const double delta = e.getDistanceFromDragStartX() / geo.pixelsPerTick;
            dragDeltaTicks = snap (static_cast<double> (clipStart) + delta, e.mods) - clipStart;
            dragDeltaTracks = juce::roundToInt (static_cast<float> (e.getDistanceFromDragStartY()) / static_cast<float> (geo.trackHeight));
            repaint();
            break;
        }
        case Drag::resize:
        {
            Tick clipStart = 0, clipLength = 0;
            if (! clipSpan (dragClip, clipStart, clipLength))
                return;
            const auto end = snap (geo.xToTick (static_cast<float> (e.x)), e.mods);
            dragNewLength = std::max<Tick> (song.timeSignature.ticksPerBeat(), end - clipStart);
            if (const auto audio = edit::findAudioClip (song, dragClip))
            {
                // An audio clip can't be longer than what is left of its file.
                double framesPerPeak = 0.0, rate = 0.0;
                const auto& p = peaksFor (audio->file, framesPerPeak, rate);
                if (! p.empty() && rate > 0.0)
                {
                    const double seconds = (static_cast<double> (p.size()) * framesPerPeak / rate - audio->offset) / edit::audioSpeed (song, *audio);
                    const auto longest = static_cast<Tick> (song.tempo.tickAtSeconds (song.tempo.secondsAt (static_cast<double> (clipStart)) + std::max (0.0, seconds))) - clipStart;
                    dragNewLength = std::clamp<Tick> (dragNewLength, 1, std::max<Tick> (1, longest));
                }
            }
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
            {
                for (const auto& c : song.tracks[static_cast<std::size_t> (i)].clips)
                    if (clipBounds (*c, i).intersects (marquee.toFloat()))
                        sel.insert (c->id);
                for (const auto& c : song.tracks[static_cast<std::size_t> (i)].audio)
                    if (clipBounds (shellOf (*c), i).intersects (marquee.toFloat()))
                        sel.insert (c->id);
            }
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
                const auto audio = edit::findAudioClip (s, id);
                const int from = edit::trackIndex (s, edit::trackOfClip (s, id));
                if ((clip == nullptr && audio == nullptr) || from < 0)
                    continue;
                const int to = std::clamp (from + deltaTr, 0, static_cast<int> (s.tracks.size()) - 1);
                // A MIDI clip does not go onto an audio track.
                const bool allowed = audio != nullptr || s.tracks[static_cast<std::size_t> (to)].output.kind != TrackOutput::Kind::audio;
                edit::moveClip (s, id, (audio != nullptr ? audio->start : clip->start) + deltaT, s.tracks[static_cast<std::size_t> (allowed ? to : from)].id);
            }
        });
    }
    else if (drag == Drag::resize)
    {
        const auto id = dragClip;
        const auto length = dragNewLength;
        if (auto clip = edit::findClipPtr (song, id); clip != nullptr && clip->length != length)
            session.edit ("Resize Clip", [id, length] (Song& s) { edit::modifyClip (s, id, [length] (Clip& c) { c.length = length; }); });
        else if (auto audio = edit::findAudioClip (song, id); audio != nullptr && audio->length != length)
            session.edit ("Resize Clip", [id, length] (Song& s) { edit::modifyAudioClip (s, id, [length] (AudioClip& c) { c.length = length; }); });
    }
    session.endMerge();
    drag = Drag::none;
    dragDeltaTicks = 0;
    dragDeltaTracks = 0;
    repaint();
}

void Arrangement::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int row = geo.trackAt (e.y); row >= 0 && row < static_cast<int> (session.song().tracks.size()) && e.y >= geo.rulerHeight)
        if (geo.automationOf (session.song().tracks[static_cast<std::size_t> (row)].id) != 0)
            return; // the line has the mouse; a click already made or took the point
    if (e.y >= geo.chordLaneTop() && e.y < geo.rulerHeight)
    {
        typeChordsAt (static_cast<Tick> (std::max (0.0, geo.xToTick (static_cast<float> (e.x)))));
        return;
    }
    if (e.y < geo.rulerHeight)
    {
        if (session.song().loop.isValid())
            session.edit ("Loop", [] (Song& s) { s.loop.enabled = ! s.loop.enabled; });
        return;
    }
    const auto h = hitTest (e.getPosition());
    if (h.clip != 0)
    {
        if (edit::findAudioClip (session.song(), h.clip) != nullptr)
            showAudioMenu (h.clip);
        else
        {
            session.openInEditor (h.clip);
            // A drum clip opens in the Beat view.
            const auto track = edit::trackOfClip (session.song(), h.clip);
            if (const auto* t = edit::findTrack (session.song(), track); t != nullptr && notation::isDrumTrack (*t) && session.onOpenBeat)
                session.onOpenBeat();
        }
        return;
    }
    const auto& song = session.song();
    if (h.trackIndex < 0 || h.trackIndex >= static_cast<int> (song.tracks.size()))
        return;
    if (song.tracks[static_cast<std::size_t> (h.trackIndex)].output.kind == TrackOutput::Kind::audio)
    {
        // An audio track: choose a file and put it at the bar under the mouse.
        const auto bar = song.timeSignature.ticksPerBar();
        const Tick at = std::max<Tick> (0, static_cast<Tick> (std::floor (geo.xToTick (static_cast<float> (e.x)) / static_cast<double> (bar))) * bar);
        const auto track = song.tracks[static_cast<std::size_t> (h.trackIndex)].id;
        audioChooser = std::make_unique<juce::FileChooser> ("Add an audio file", juce::File::getSpecialLocation (juce::File::userMusicDirectory), audio_files::wildcard());
        juce::Component::SafePointer<Arrangement> safe (this);
        audioChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, track, at] (const juce::FileChooser& fc) {
            if (safe == nullptr || fc.getResult() == juce::File())
                return;
            if (! safe->addAudioFile (fc.getResult(), track, at))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Can't import audio", safe->session.sampleProblem (fc.getResult().getFullPathName().toStdString()));
        });
        return;
    }

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
        {
            if (auto c = edit::findClipPtr (s, id))
            {
                first = std::min (first, c->start);
                last = std::max (last, c->end());
            }
            if (auto a = edit::findAudioClip (s, id))
            {
                first = std::min (first, a->start);
                last = std::max (last, a->end());
            }
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
            else if (auto a = edit::findAudioClip (s, id))
            {
                AudioClip copy = *a;
                copy.id = 0;
                copy.start += shift;
                added.insert (edit::addAudioClip (s, edit::trackOfClip (s, id), copy));
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
        {
            for (const auto& c : t->clips)
                if (c->start < at && c->end() > at)
                    ids.insert (c->id);
            for (const auto& c : t->audio)
                if (c->start < at && c->end() > at)
                    ids.insert (c->id);
        }
    if (ids.empty())
        return;
    session.edit ("Split", [ids, at] (Song& s) {
        for (auto id : ids)
            edit::splitClip (s, id, at);
    });
}

// ---- clipboard ----------------------------------------------------------------------
void Arrangement::addClipboardItems (juce::PopupMenu& m, bool canCopy) const
{
    m.addItem (kCutItem, "Cut", canCopy);
    m.addItem (kCopyItem, "Copy", canCopy);
    m.addItem (kPasteItem, "Paste Here", ! session.clipboard.empty());
}

bool Arrangement::handleClipboardItem (int r, TrackId track, Tick at)
{
    if (r == kCutItem || r == kCopyItem)
        copySelected (r == kCutItem);
    else if (r == kPasteItem)
        paste (track, at);
    else
        return false;
    return true;
}

void Arrangement::deletePoints()
{
    const auto id = pointTrack;
    const int lane = pointLane;
    const auto times = selectedPoints;
    selectedPoints.clear();
    session.edit (times.size() > 1 ? "Delete Automation Points" : "Delete Automation Point", [&] (Song& s) {
        auto* t = edit::findTrack (s, id);
        if (auto* line = t != nullptr ? t->findLine (lane) : nullptr)
            std::erase_if (*line, [&] (const AutomationPoint& p) { return times.count (p.at) > 0; });
    });
    repaint();
}

void Arrangement::copySelected (bool cut)
{
    const auto& song = session.song();
    const auto* track = edit::findTrack (song, pointTrack);
    const auto* line = track != nullptr && geo.automationOf (pointTrack) == pointLane + 1 ? track->findLine (pointLane) : nullptr;
    if (line != nullptr && ! selectedPoints.empty())
    {
        std::vector<int> indexes;
        for (int i = 0; i < static_cast<int> (line->size()); ++i)
            if (selectedPoints.count ((*line)[static_cast<std::size_t> (i)].at) > 0)
                indexes.push_back (i);
        session.clipboard = clipboard::copyPoints (*line, indexes);
        if (cut)
            deletePoints();
        return;
    }
    const auto ids = session.selectedClips();
    if (ids.empty())
        return;
    session.clipboard = clipboard::copyClips (song, { ids.begin(), ids.end() });
    if (cut)
        session.edit (ids.size() > 1 ? "Cut Clips" : "Cut Clip", [ids] (Song& s) {
            for (auto id : ids)
                edit::removeClip (s, id);
        });
}

void Arrangement::paste (TrackId track, Tick at)
{
    const auto content = session.clipboard;
    switch (content.kind)
    {
        case clipboard::Content::Kind::empty:
            return;
        case clipboard::Content::Kind::points:
        {
            // Points go into the line the track shows. With no line shown, into its volume line.
            const int shown = geo.automationOf (track);
            const int lane = shown > 0 ? shown - 1 : 0;
            std::vector<Tick> times;
            session.edit ("Paste Automation Points", [&] (Song& s) { times = clipboard::pastePoints (s, content, track, lane, at); });
            if (shown == 0 && ! times.empty())
                geo.automationShown[track] = 1;
            pointTrack = track;
            pointLane = lane;
            selectedPoints = { times.begin(), times.end() };
            repaint();
            return;
        }
        case clipboard::Content::Kind::clips:
        {
            std::vector<ClipId> ids;
            session.edit (content.clips.size() > 1 ? "Paste Clips" : "Paste Clip", [&] (Song& s) { ids = clipboard::pasteClips (s, content, track, at); });
            session.selectClips ({ ids.begin(), ids.end() });
            return;
        }
        case clipboard::Content::Kind::notes:
        {
            std::vector<edit::NoteKey> keys;
            session.edit ("Paste Notes", [&] (Song& s) { keys = clipboard::pasteNotes (s, content, track, at); });
            if (! keys.empty())
                session.selectClips ({ keys.front().clip });
            return;
        }
    }
}

bool Arrangement::selectAllPoints()
{
    const auto id = session.selectedTrack();
    const auto* track = edit::findTrack (session.song(), id);
    const int shown = geo.automationOf (id);
    const auto* line = track != nullptr && shown > 0 ? track->findLine (shown - 1) : nullptr;
    if (line == nullptr)
        return false;
    pointTrack = id;
    pointLane = shown - 1;
    selectedPoints.clear();
    for (const auto& p : *line)
        selectedPoints.insert (p.at);
    repaint();
    return true;
}

// ---- automation ---------------------------------------------------------------------
juce::Point<float> Arrangement::automationXY (const AutomationPoint& p, int trackIndex) const
{
    const float top = static_cast<float> (geo.trackY (trackIndex)) + 8.0f, height = static_cast<float> (geo.trackHeight) - 16.0f;
    return { geo.tickToX (static_cast<double> (p.at)), top + (1.0f - p.value / 127.0f) * height };
}

AutomationPoint Arrangement::automationPointAt (juce::Point<int> p, int trackIndex, const juce::ModifierKeys& mods) const
{
    const float top = static_cast<float> (geo.trackY (trackIndex)) + 8.0f, height = static_cast<float> (geo.trackHeight) - 16.0f;
    AutomationPoint point;
    point.at = std::max<Tick> (0, snap (geo.xToTick (static_cast<float> (p.x)), mods));
    point.value = std::clamp ((1.0f - (static_cast<float> (p.y) - top) / height) * 127.0f, 0.0f, 127.0f);
    return point;
}

int Arrangement::automationHit (const Track& track, int trackIndex, int lane, juce::Point<int> p) const
{
    const auto& line = track.automationLine (lane);
    for (std::size_t i = 0; i < line.size(); ++i)
        if (automationXY (line[i], trackIndex).getDistanceFrom (p.toFloat()) <= 8.0f)
            return static_cast<int> (i);
    return -1;
}

void Arrangement::drawAutomation (juce::Graphics& g, const Track& track, int trackIndex, int lane) const
{
    const auto& t = currentTheme();
    const bool pan = lane == 1;
    const auto& line = track.automationLine (lane);
    const auto row = juce::Rectangle<float> (0.0f, static_cast<float> (geo.trackY (trackIndex)), static_cast<float> (getWidth()), static_cast<float> (geo.trackHeight));
    g.setColour (t.colour ("background").withAlpha (0.55f)); // the clips step back while the line is shown
    g.fillRect (row.reduced (0.0f, 1.0f));
    const auto colour = t.colour ("accent");

    // The line: level before the first point and after the last, straight between points.
    float whenEmpty = static_cast<float> (track.mixerValue (lane));
    int controlSlot = 0;
    const auto* control = track.controlLine (lane, &controlSlot);
    if (control != nullptr) // a plugin control: where the control is now
        if (const auto live = session.pluginAt (track.id, controlSlot); live != nullptr && control->control < live->plugin().getParameters().size())
            whenEmpty = live->plugin().getParameters()[control->control]->getValue() * 127.0f;
    juce::Path path;
    if (line.empty())
    {
        const float y = automationXY ({ 0, whenEmpty }, trackIndex).y;
        path.startNewSubPath (0.0f, y);
        path.lineTo (row.getRight(), y);
    }
    else
    {
        path.startNewSubPath (std::min (0.0f, automationXY (line.front(), trackIndex).x), automationXY (line.front(), trackIndex).y);
        for (const auto& p : line)
            path.lineTo (automationXY (p, trackIndex));
        path.lineTo (std::max (row.getRight(), automationXY (line.back(), trackIndex).x), automationXY (line.back(), trackIndex).y);
    }
    g.setColour (line.empty() ? colour.withAlpha (0.45f) : colour);
    g.strokePath (path, juce::PathStrokeType (line.empty() ? 1.2f : 2.0f));
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const auto c = automationXY (line[i], trackIndex);
        const bool held = drag == Drag::automation && automationTrack == track.id && automationIndex == static_cast<int> (i);
        const bool chosen = pointTrack == track.id && pointLane == lane && selectedPoints.count (line[i].at) > 0;
        g.setColour (chosen ? colour : t.colour ("panel"));
        g.fillEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
        g.setColour (held || chosen ? t.colour ("text") : colour);
        g.drawEllipse (c.x - 4.5f, c.y - 4.5f, 9.0f, 9.0f, 2.0f);
    }

    g.setColour (colour);
    g.setFont (t.font (10.0f, true));
    juce::String label = lane == 1 ? "PAN" : (lane == 2 ? "REVERB SEND" : (lane == 3 ? "CHORUS SEND" : "VOLUME"));
    if (control != nullptr)
        label = (control->name.empty() ? "Control " + juce::String (control->control + 1) : utf8 (control->name)).toUpperCase();
    if (drag == Drag::automation && automationTrack == track.id && automationIndex >= 0 && automationIndex < static_cast<int> (line.size()))
    {
        const float v = line[static_cast<std::size_t> (automationIndex)].value;
        label += pan ? (std::abs (v - 64.0f) < 0.5f ? juce::String ("  centre") : (v < 64.0f ? "  L " : "  R ") + juce::String (std::abs (std::lround (v - 64.0f))))
                     : (control != nullptr ? "  " + juce::String (std::lround (v * 100.0f / 127.0f)) + " %" : "  " + juce::String (std::lround (v)));
    }
    else if (line.empty())
        label += "  click to add a point";
    g.drawText (label, row.reduced (8.0f, 3.0f), juce::Justification::topRight, false);
}

// ---- audio clips ------------------------------------------------------------------
bool Arrangement::clipSpan (ClipId id, Tick& start, Tick& length) const
{
    if (const auto clip = edit::findClipPtr (session.song(), id))
    {
        start = clip->start;
        length = clip->length;
        return true;
    }
    if (const auto audio = edit::findAudioClip (session.song(), id))
    {
        start = audio->start;
        length = audio->length;
        return true;
    }
    return false;
}

const std::vector<std::pair<float, float>>& Arrangement::peaksFor (const std::string& file, double& framesPerPeak, double& sampleRate)
{
    framesPerPeak = kPeakFrames;
    auto it = peaks.find (file);
    if (it == peaks.end())
    {
        Peaks p;
        if (const auto sample = session.sampleFor (file))
        {
            p.sampleRate = sample->sampleRate;
            const auto frames = sample->frames();
            p.minMax.reserve (frames / kPeakFrames + 1);
            for (std::size_t f = 0; f < frames; f += kPeakFrames)
            {
                float lo = 0.0f, hi = 0.0f;
                const auto end = std::min (frames, f + kPeakFrames);
                for (const auto& channel : sample->channels)
                    for (std::size_t i = f; i < end; ++i)
                    {
                        lo = std::min (lo, channel[i]);
                        hi = std::max (hi, channel[i]);
                    }
                p.minMax.push_back ({ lo, hi });
            }
            it = peaks.emplace (file, std::move (p)).first;
        }
        else
        {
            static const std::vector<std::pair<float, float>> none;
            sampleRate = 0.0;
            return none; // not cached: the file may turn up later
        }
    }
    sampleRate = it->second.sampleRate;
    return it->second.minMax;
}

void Arrangement::drawAudioClip (juce::Graphics& g, const AudioClip& clip, juce::Rectangle<float> r, juce::Colour c, bool selected, bool ghost)
{
    const auto& t = currentTheme();
    const float radius = 2.0f;
    const auto body = r.reduced (0.5f);
    const auto ink = t.isDark ? juce::Colour (0xff111417) : t.colour ("text");
    auto paper = clipFill (c, selected);
    if (ghost)
        paper = paper.withAlpha (0.28f);

    if (selected)
    {
        // A flat selection wash across the part, not a bigger rounded card.
        g.setColour (t.colour ("selection").withAlpha (0.14f));
        g.fillRect (body.expanded (0.0f, 2.0f));
    }
    // Modern flat part: a faint, light outline instead of a heavy rounded border.
    g.setColour (paper);
    g.fillRoundedRectangle (body, radius);
    g.setColour (selected ? t.colour ("selection") : c.darker (0.35f).withAlpha (0.32f));
    g.drawRoundedRectangle (body, radius, selected ? 2.0f : 1.0f);
    // A thin left accent carries the part's identity where the track lanes meet.
    g.setColour (c.darker (0.2f).withAlpha (selected ? 1.0f : 0.6f));
    g.fillRect (body.getX(), body.getY() + 1.0f, 2.0f, juce::jmax (1.0f, body.getHeight() - 2.0f));
    if (ghost)
        return;

    g.saveState();
    g.reduceClipRegion (body.toNearestInt());

    const auto& song = session.song();
    double framesPerPeak = 0.0, rate = 0.0;
    const auto& p = peaksFor (clip.file, framesPerPeak, rate);
    const auto wave = r.reduced (2.0f, 4.0f).withTrimmedTop (16.0f);
    const float mid = wave.getCentreY(), half = wave.getHeight() / 2.0f;
    const float gain = std::pow (10.0f, clip.gainDb / 20.0f);
    const double startSeconds = song.tempo.secondsAt (static_cast<double> (clip.start));
    const double endSeconds = song.tempo.secondsAt (static_cast<double> (clip.end()));
    const double speed = edit::audioSpeed (song, clip);
    const double fadeIn = song.tempo.secondsAt (static_cast<double> (clip.start + clip.fadeIn)) - startSeconds;
    const double fadeOut = endSeconds - song.tempo.secondsAt (static_cast<double> (clip.end() - clip.fadeOut));

    if (p.empty())
    {
        g.setColour (t.colour ("warning"));
        g.setFont (t.font (11.0f));
        g.drawText ("File not found", wave, juce::Justification::centred, true);
    }
    else
    {
        g.setColour (ink.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (wave.getX(), mid, wave.getWidth(), 1.0f));

        g.setColour (ink.withAlpha (0.86f));
        const int x1 = std::max (0, static_cast<int> (std::floor (r.getX()))), x2 = std::min (getWidth(), static_cast<int> (std::ceil (r.getRight())));
        for (int x = x1; x < x2; ++x)
        {
            // The part of the file under this pixel column.
            const double s1 = song.tempo.secondsAt (geo.xToTick (static_cast<float> (x))) - startSeconds;
            const double s2 = song.tempo.secondsAt (geo.xToTick (static_cast<float> (x + 1))) - startSeconds;
            if (s1 < 0.0)
                continue;
            const auto i1 = static_cast<std::size_t> ((s1 * speed + clip.offset) * rate / framesPerPeak);
            const auto i2 = std::max (i1 + 1, static_cast<std::size_t> ((s2 * speed + clip.offset) * rate / framesPerPeak));
            if (i1 >= p.size())
                break;
            float lo = 0.0f, hi = 0.0f;
            for (std::size_t i = i1; i < std::min (i2, p.size()); ++i)
            {
                lo = std::min (lo, p[i].first);
                hi = std::max (hi, p[i].second);
            }
            float level = gain;
            if (fadeIn > 0.0 && s1 < fadeIn)
                level *= static_cast<float> (s1 / fadeIn);
            if (fadeOut > 0.0 && endSeconds - startSeconds - s1 < fadeOut)
                level *= static_cast<float> (std::max (0.0, (endSeconds - startSeconds - s1) / fadeOut));
            const float top = mid - std::clamp (hi * level, -1.0f, 1.0f) * half, bottom = mid - std::clamp (lo * level, -1.0f, 1.0f) * half;
            g.fillRect (juce::Rectangle<float> (static_cast<float> (x), top, 1.0f, std::max (1.0f, bottom - top)));
        }
    }

    // Fades: a line from the corner.
    g.setColour (ink.withAlpha (0.55f));
    if (clip.fadeIn > 0)
        g.drawLine (r.getX(), r.getBottom() - 2.0f, geo.tickToX (static_cast<double> (clip.start + clip.fadeIn)), r.getY() + 2.0f, 1.0f);
    if (clip.fadeOut > 0)
        g.drawLine (geo.tickToX (static_cast<double> (clip.end() - clip.fadeOut)), r.getY() + 2.0f, r.getRight(), r.getBottom() - 2.0f, 1.0f);

    g.setColour (ink.withAlpha (0.95f));
    g.setFont (t.font (11.0f, true));
    auto label = utf8 (clip.name);
    if (clip.bpm > 0.0)
        label += dot + juce::String (clip.bpm, 1) + " BPM";
    if (std::abs (clip.gainDb) >= 0.05f)
        label += dot + (clip.gainDb > 0 ? "+" : "") + juce::String (clip.gainDb, 1) + " dB";
    g.drawText (label, r.reduced (8.0f, 3.0f).removeFromTop (15.0f), juce::Justification::topLeft, true);
    g.restoreState();
}

void Arrangement::showAudioMenu (ClipId id)
{
    const auto clip = edit::findAudioClip (session.song(), id);
    if (clip == nullptr)
        return;
    const Tick beat = session.song().timeSignature.ticksPerBeat();
    juce::PopupMenu m, gain, fadeIn, fadeOut;
    const float gains[] = { 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -12.0f };
    for (int i = 0; i < 6; ++i)
        gain.addItem (100 + i, (gains[i] > 0 ? "+" : "") + juce::String (gains[i], 0) + " dB", true, std::abs (clip->gainDb - gains[i]) < 0.05f);
    const char* const lengths[] = { "None", "A 16th note", "One beat", "One bar" };
    const Tick ticks[] = { 0, beat / 4, beat, session.song().timeSignature.ticksPerBar() };
    for (int i = 0; i < 4; ++i)
    {
        fadeIn.addItem (200 + i, lengths[i], true, clip->fadeIn == ticks[i]);
        fadeOut.addItem (300 + i, lengths[i], true, clip->fadeOut == ticks[i]);
    }
    m.addSubMenu ("Gain", gain);
    m.addSubMenu ("Fade in", fadeIn);
    m.addSubMenu ("Fade out", fadeOut);
    m.addSeparator();
    m.addItem (10, "Follow Song Tempo", true, clip->bpm > 0.0);
    m.addItem (11, "Set File Tempo...");
    m.addSeparator();
    juce::PopupMenu toMidi;
    toMidi.addItem (20, "Melody (one note at a time)");
    toMidi.addItem (21, "Drums (kick, snare, hi-hat)");
    m.addSubMenu ("Convert to MIDI", toMidi);
    m.addItem (22, "Analyse Audio (Stems, Chords, Lyrics)...", onAnalyse != nullptr);
    m.addSeparator();
    // Takes: the clips at this place, top first. The one on top plays.
    const auto trackIndex = edit::trackIndex (session.song(), edit::trackOfClip (session.song(), id));
    std::vector<ClipId> takeIds;
    if (trackIndex >= 0)
    {
        const auto& track = session.song().tracks[static_cast<std::size_t> (trackIndex)];
        const auto at = static_cast<Tick> (geo.xToTick (static_cast<float> (getMouseXYRelative().x)));
        auto here = edit::audioClipsAt (track, std::clamp (at, clip->start, clip->end() - 1));
        if (here.size() > 1)
        {
            juce::PopupMenu takes;
            for (std::size_t i = 0; i < here.size(); ++i)
            {
                takeIds.push_back (here[i]->id);
                takes.addItem (400 + static_cast<int> (i), "Take " + juce::String (static_cast<int> (here.size() - i)) + ": " + utf8 (here[i]->name), true, i == 0);
            }
            m.addSubMenu ("Takes Here", takes);
            m.addItem (7, "Bring to Front", here.front()->id != id);
            m.addSeparator();
        }
    }
    addClipboardItems (m, true);
    m.addSeparator();
    m.addItem (2, "Duplicate");
    m.addItem (3, "Split at Playhead");
    m.addSeparator();
    m.addItem (6, "Delete");
    const std::vector<float> gainList (std::begin (gains), std::end (gains));
    const std::vector<Tick> tickList (std::begin (ticks), std::end (ticks));
    const auto pasteTrack = edit::trackOfClip (session.song(), id);
    const auto pasteAt = std::max<Tick> (0, snap (geo.xToTick (static_cast<float> (getMouseXYRelative().x)), juce::ModifierKeys::currentModifiers));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, gainList, tickList, takeIds, id, pasteTrack, pasteAt] (int r) {
        if (handleClipboardItem (r, pasteTrack, pasteAt))
            return;
        if (r == 7 || (r >= 400 && r < 400 + static_cast<int> (takeIds.size())))
        {
            const auto front = r == 7 ? id : takeIds[static_cast<std::size_t> (r - 400)];
            session.edit ("Choose Take", [front] (Song& s) { edit::bringToFront (s, front); });
            return;
        }
        const auto ids = session.selectedClips();
        const auto each = [&] (const char* name, const std::function<void (AudioClip&)>& fn) {
            session.edit (name, [&] (Song& s) {
                for (auto clipId : ids)
                    edit::modifyAudioClip (s, clipId, fn);
            });
        };
        if (r == 10 || r == 11)
        {
            const auto first = ids.empty() ? nullptr : edit::findAudioClip (session.song(), *ids.begin());
            if (first == nullptr)
                return;
            if (r == 10)
                for (auto clipId : ids)
                    setFollowTempo (clipId, first->bpm > 0.0 ? 0.0 : -1.0);
            else
                askFileTempo (ids);
            return;
        }
        if (r == 22)
        {
            if (onAnalyse)
                onAnalyse (id);
            return;
        }
        if (r == 20 || r == 21)
        {
            for (auto clipId : ids)
                convertToMidi (clipId, r == 21);
            return;
        }
        if (r == 2) duplicateSelected();
        else if (r == 3) splitAtPlayhead();
        else if (r == 6) deleteSelected();
        else if (r >= 100 && r < 106) each ("Clip Gain", [&] (AudioClip& c) { c.gainDb = gainList[static_cast<std::size_t> (r - 100)]; });
        else if (r >= 200 && r < 204) each ("Fade In", [&] (AudioClip& c) { c.fadeIn = tickList[static_cast<std::size_t> (r - 200)]; });
        else if (r >= 300 && r < 304) each ("Fade Out", [&] (AudioClip& c) { c.fadeOut = tickList[static_cast<std::size_t> (r - 300)]; });
    });
}

void Arrangement::setFollowTempo (ClipId id, double bpm)
{
    const auto clip = edit::findAudioClip (session.song(), id);
    const auto sample = clip != nullptr ? session.sampleFor (clip->file) : nullptr;
    if (sample == nullptr)
        return;
    const auto& song = session.song();
    if (bpm < 0.0) // find it from the length of the file
        bpm = stretch::tempoFromLength (sample->seconds(), song.tempo.tempoAt (static_cast<double> (clip->start)), song.timeSignature.numerator);
    session.edit (bpm > 0.0 ? "Follow Song Tempo" : "Keep File Tempo", [&] (Song& s) {
        // The clip keeps the same part of the file, so its length in the song changes.
        const double before = edit::audioSpeed (s, *clip);
        const double startSeconds = s.tempo.secondsAt (static_cast<double> (clip->start));
        const double fileSeconds = (s.tempo.secondsAt (static_cast<double> (clip->end())) - startSeconds) * before;
        edit::modifyAudioClip (s, id, [&] (AudioClip& c) {
            c.bpm = bpm;
            const double after = edit::audioSpeed (s, c);
            c.length = std::max<Tick> (1, static_cast<Tick> (std::llround (s.tempo.tickAtSeconds (startSeconds + fileSeconds / after))) - c.start);
        });
    });
}

void Arrangement::askFileTempo (std::set<ClipId> ids)
{
    const auto first = ids.empty() ? nullptr : edit::findAudioClip (session.song(), *ids.begin());
    if (first == nullptr)
        return;
    auto* w = new juce::AlertWindow ("File tempo", "The tempo of the audio file, in BPM. The clip then follows the song tempo.", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("bpm", first->bpm > 0.0 ? juce::String (first->bpm, 2) : juce::String(), "BPM");
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, ids] (int result) {
        const double bpm = w->getTextEditorContents ("bpm").getDoubleValue();
        if (result == 1 && bpm >= 20.0 && bpm <= 400.0)
            for (auto id : ids)
                setFollowTempo (id, bpm);
    }), true);
}

bool Arrangement::convertToMidi (ClipId id, bool drums)
{
    const auto audio = edit::findAudioClip (session.song(), id);
    const auto sample = audio != nullptr ? session.sampleFor (audio->file) : nullptr;
    if (sample == nullptr)
        return false;
    const auto found = drums ? audio_to_midi::drums (*sample) : audio_to_midi::melody (*sample);
    auto clip = audio_to_midi::toClip (found, session.song(), *audio);
    if (clip.notes.empty())
    {
        if (session.onError)
            session.onError (drums ? "No drum hits found in this clip." : "No notes found in this clip. Audio to MIDI follows one note at a time, not chords.");
        return false;
    }
    TrackId added = 0;
    ClipId addedClip = 0;
    session.edit ("Convert to MIDI", [&] (Song& s) {
        auto& t = edit::addTrack (s, clip.name + (drums ? " drums" : " MIDI"), edit::trackIndex (s, edit::trackOfClip (s, id)));
        if (drums)
            t.output.channel = 10; // the drum channel of General MIDI
        added = t.id;
        addedClip = edit::addClip (s, added, clip);
    });
    session.selectTrack (added);
    session.selectClips ({ addedClip });
    return true;
}

bool Arrangement::addAudioFile (const juce::File& file, TrackId trackId, Tick at)
{
    const auto path = file.getFullPathName().toStdString();
    const auto sample = session.sampleFor (path);
    if (sample == nullptr || sample->frames() == 0)
        return false;
    const double seconds = sample->seconds();
    ClipId added = 0;
    TrackId target = trackId;
    session.edit ("Add Audio File", [&] (Song& s) {
        const auto* t = edit::findTrack (s, target);
        if (t == nullptr || t->output.kind != TrackOutput::Kind::audio)
            target = edit::addAudioTrack (s, file.getFileNameWithoutExtension().toStdString(), edit::trackIndex (s, target)).id;
        AudioClip clip;
        clip.name = file.getFileNameWithoutExtension().toStdString();
        clip.file = path;
        clip.start = std::max<Tick> (0, at);
        const double startSeconds = s.tempo.secondsAt (static_cast<double> (clip.start));
        clip.length = std::max<Tick> (1, static_cast<Tick> (std::llround (s.tempo.tickAtSeconds (startSeconds + seconds))) - clip.start);
        if (const auto* on = edit::findTrack (s, target))
            clip.layer = edit::layerAbove (*on, clip.start, clip.end()); // a new file goes on top
        added = edit::addAudioClip (s, target, std::move (clip));
    });
    session.selectTrack (target);
    session.selectClips ({ added });
    return added != 0;
}

bool Arrangement::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (audio_files::isAudioFile (juce::File (f)))
            return true;
    return false;
}

void Arrangement::filesDropped (const juce::StringArray& files, int x, int y)
{
    const auto& song = session.song();
    const int index = geo.trackAt (y);
    TrackId track = index >= 0 && index < static_cast<int> (song.tracks.size()) ? song.tracks[static_cast<std::size_t> (index)].id : 0;
    const Tick bar = song.timeSignature.ticksPerBar();
    Tick at = std::max<Tick> (0, static_cast<Tick> (std::floor (geo.xToTick (static_cast<float> (x)) / static_cast<double> (bar))) * bar);
    for (const auto& f : files)
        if (audio_files::isAudioFile (juce::File (f)))
        {
            if (! addAudioFile (juce::File (f), track, at))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Can't import audio", session.sampleProblem (f.toStdString()));
                continue;
            }
            // The next file goes after this one, on the same track.
            track = session.selectedTrack();
            if (const auto* t = edit::findTrack (session.song(), track); t != nullptr && ! t->audio.empty())
                at = ((t->audio.back()->end() + bar - 1) / bar) * bar;
        }
}

} // namespace arranger::ui

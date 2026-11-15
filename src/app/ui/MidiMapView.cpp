#include "MidiMapView.h"

#include "Panels.h"
#include "arranger/core/SongEdit.h"

#include <algorithm>

namespace arranger::ui
{

namespace
{
    constexpr int kHeader = 58;
    constexpr int kTrackWidth = 144;
    constexpr int kCellWidth = 64;
    constexpr int kRowHeight = 34;

    struct Column
    {
        const char* name;
        ControlAction action;
        int row;
    };

    const Column columns[] = {
        { "Volume", ControlAction::volume, 0 },
        { "Pan", ControlAction::pan, 0 },
        { "Send A", ControlAction::reverb, 0 },
        { "Send B", ControlAction::chorus, 0 },
        { "Mute", ControlAction::mute, 0 },
        { "Solo", ControlAction::solo, 0 },
        { "Arm", ControlAction::arm, 0 },
        { "Clip 1", ControlAction::launchClip, 0 },
        { "Clip 2", ControlAction::launchClip, 1 },
        { "Clip 3", ControlAction::launchClip, 2 },
        { "Clip 4", ControlAction::launchClip, 3 },
    };

    const Column globals[] = {
        { "Master", ControlAction::masterVolume, 0 },
        { "Play", ControlAction::play, 0 },
        { "Stop", ControlAction::stop, 0 },
        { "Record", ControlAction::record, 0 },
        { "Loop", ControlAction::loop, 0 },
        { "Scene", ControlAction::launchScene, 0 },
        { "Stop clips", ControlAction::stopClips, 0 },
    };

    juce::String actionName (ControlAction a)
    {
        switch (a)
        {
            case ControlAction::volume: return "Volume";
            case ControlAction::pan: return "Pan";
            case ControlAction::reverb: return "Send A";
            case ControlAction::chorus: return "Send B";
            case ControlAction::mute: return "Mute";
            case ControlAction::solo: return "Solo";
            case ControlAction::arm: return "Arm";
            case ControlAction::select: return "Select";
            case ControlAction::panCentre: return "Centre";
            case ControlAction::masterVolume: return "Master";
            case ControlAction::play: return "Play";
            case ControlAction::stop: return "Stop";
            case ControlAction::record: return "Record";
            case ControlAction::loop: return "Loop";
            case ControlAction::launchClip: return "Clip";
            case ControlAction::launchScene: return "Scene";
            case ControlAction::stopClips: return "Stop clips";
            default: return "Unmapped";
        }
    }

    juce::String actionShort (ControlAction a)
    {
        switch (a)
        {
            case ControlAction::volume: return "VOL";
            case ControlAction::pan: return "PAN";
            case ControlAction::reverb: return "SEND A";
            case ControlAction::chorus: return "SEND B";
            case ControlAction::mute: return "MUTE";
            case ControlAction::solo: return "SOLO";
            case ControlAction::arm: return "ARM";
            case ControlAction::select: return "SEL";
            case ControlAction::panCentre: return "CENTRE";
            case ControlAction::masterVolume: return "MASTER";
            case ControlAction::play: return "PLAY";
            case ControlAction::stop: return "STOP";
            case ControlAction::record: return "REC";
            case ControlAction::loop: return "LOOP";
            case ControlAction::launchClip: return "CLIP";
            case ControlAction::launchScene: return "SCENE";
            case ControlAction::stopClips: return "STOP CLIPS";
            default: return "—";
        }
    }

    bool isValue (ControlAction a)
    {
        return a == ControlAction::volume || a == ControlAction::pan || a == ControlAction::reverb
            || a == ControlAction::chorus || a == ControlAction::masterVolume;
    }
}

void AccentButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = currentTheme();
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    auto fill = t.colour ("accent");
    if (down)
        fill = fill.darker (0.18f);
    else if (highlighted)
        fill = fill.brighter (0.08f);
    g.setColour (fill.withAlpha (0.18f));
    g.fillRoundedRectangle (r.expanded (1.0f), 7.0f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (t.colour ("accentText"));
    g.setFont (t.font (12.5f, true));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
}

// ---------------------------------------------------------------------------
class MidiMapView::MatrixPanel final : public juce::Component
{
public:
    explicit MatrixPanel (MidiMapView& o) : owner (o) {}

    int rowCount() const
    {
        const auto* p = owner.selectedProfile();
        const int strips = p != nullptr ? p->strips : 8;
        const int available = static_cast<int> (owner.session.song().tracks.size()) - owner.session.surfaceBank();
        return juce::jlimit (0, strips, available);
    }

    void rebuild()
    {
        const auto rows = rowCount();
        setSize (kTrackWidth + static_cast<int> (std::size (columns)) * kCellWidth + 12,
                 kHeader + (rows + static_cast<int> (std::size (globals))) * kRowHeight + 14);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = currentTheme();
        const auto& song = owner.session.song();
        const int bank = owner.session.surfaceBank();
        const int rows = rowCount();

        // Column headings.
        auto header = getLocalBounds().removeFromTop (kHeader);
        g.setColour (t.colour ("panelRaised"));
        g.fillRoundedRectangle (header.toFloat().reduced (0.0f, 2.0f), 7.0f);
        g.setColour (t.colour ("textDim"));
        g.setFont (t.font (10.0f, true).withExtraKerningFactor (0.08f));
        g.drawText ("TRACK", header.withWidth (kTrackWidth).reduced (12, 0), juce::Justification::centredLeft);
        for (int c = 0; c < static_cast<int> (std::size (columns)); ++c)
        {
            const auto r = header.withTrimmedLeft (kTrackWidth + c * kCellWidth).withWidth (kCellWidth);
            g.drawText (juce::String (columns[c].name).toUpperCase(), r, juce::Justification::centred);
        }

        int y = kHeader;
        for (int i = 0; i < rows; ++i)
        {
            const int absolute = bank + i;
            const auto& track = song.tracks[static_cast<std::size_t> (absolute)];
            auto row = juce::Rectangle<int> (0, y, getWidth(), kRowHeight);
            if (i % 2 == 1)
            {
                g.setColour (t.colour ("laneAlt"));
                g.fillRect (row);
            }
            if (i == hoverTrack)
            {
                g.setColour (t.colour ("accent").withAlpha (0.05f));
                g.fillRect (row);
            }

            auto name = row.withWidth (kTrackWidth).reduced (12, 0);
            g.setColour (track.colour.empty() ? t.colour ("accent") : t.colour (track.colour));
            g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, static_cast<float> (y) + 9.0f, 3.0f, 16.0f), 1.5f);
            g.setColour (t.colour ("text"));
            g.setFont (t.font (12.5f));
            g.drawText (juce::String::fromUTF8 (track.name.c_str()), name.withTrimmedRight (44), juce::Justification::centredLeft, true);
            g.setColour (t.colour ("textDim"));
            g.setFont (t.font (10.0f));
            g.drawText (juce::String (absolute + 1).paddedLeft ('0', 2), name.removeFromRight (30), juce::Justification::centredRight);

            for (int c = 0; c < static_cast<int> (std::size (columns)); ++c)
                paintCell (g, i, columns[c], row.withTrimmedLeft (kTrackWidth + c * kCellWidth).withWidth (kCellWidth), i == hoverTrack && c == hoverColumn);
            y += kRowHeight;
        }

        // Global controls at the bottom.
        g.setColour (t.colour ("lineStrong"));
        g.fillRect (0, y + 2, getWidth(), 1);
        y += 8;
        g.setColour (t.colour ("textDim"));
        g.setFont (t.font (10.0f, true).withExtraKerningFactor (0.08f));
        g.drawText ("GLOBAL", juce::Rectangle<int> (12, y, kTrackWidth - 12, kRowHeight), juce::Justification::centredLeft);
        for (int c = 0; c < static_cast<int> (std::size (globals)); ++c)
            paintCell (g, -1, globals[c], juce::Rectangle<int> (kTrackWidth + c * kCellWidth, y, kCellWidth, kRowHeight), false);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto hit = hitTest (e.getPosition());
        if (hit.track != hoverTrack || hit.column != hoverColumn)
        {
            hoverTrack = hit.track;
            hoverColumn = hit.column;
            setMouseCursor (hit.valid ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverTrack = -999;
        hoverColumn = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto hit = hitTest (e.getPosition());
        if (hit.valid)
            owner.showControlMenu (hit.track, hit.action, hit.row);
    }

private:
    struct Hit
    {
        bool valid = false;
        int track = -999;
        int column = -1;
        ControlAction action = ControlAction::none;
        int row = 0;
    };

    Hit hitTest (juce::Point<int> p) const
    {
        const auto rows = rowCount();
        if (p.y < kHeader)
            return {};
        int y = kHeader;
        for (int i = 0; i < rows; ++i, y += kRowHeight)
            if (p.y >= y && p.y < y + kRowHeight && p.x >= kTrackWidth)
            {
                const int c = (p.x - kTrackWidth) / kCellWidth;
                if (c >= 0 && c < static_cast<int> (std::size (columns)))
                    return { true, i, c, columns[c].action, columns[c].row };
            }
        y += 8;
        if (p.y >= y && p.y < y + kRowHeight && p.x >= kTrackWidth)
        {
            const int c = (p.x - kTrackWidth) / kCellWidth;
            if (c >= 0 && c < static_cast<int> (std::size (globals)))
                return { true, -1, c, globals[c].action, globals[c].row };
        }
        return {};
    }

    void paintCell (juce::Graphics& g, int track, const Column& col, juce::Rectangle<int> cell, bool hot)
    {
        const auto& t = currentTheme();
        const auto inner = cell.reduced (4, 4).toFloat();
        const auto control = owner.cellControl (track, col.action, col.row);
        g.setColour (t.colour (control ? "accent" : "line").withAlpha (control ? 0.16f : 0.55f));
        g.fillRoundedRectangle (inner, 5.0f);
        g.setColour (t.colour (control ? "accent" : "lineStrong").withAlpha (control ? 0.45f : 0.5f));
        g.drawRoundedRectangle (inner.reduced (0.5f), 5.0f, 1.0f);

        if (control)
        {
            g.setColour (t.colour ("text"));
            g.setFont (t.font (10.5f, true));
            g.drawText (control->shortName, inner.reduced (5.0f, 0.0f), juce::Justification::centred, true);
        }
        else
        {
            g.setColour (t.colour (hot ? "textMuted" : "textDim").withAlpha (hot ? 1.0f : 0.55f));
            g.setFont (t.font (15.0f));
            g.drawText ("+", inner, juce::Justification::centred);
        }
    }

    MidiMapView& owner;
    int hoverTrack = -999, hoverColumn = -1;
};

// ---------------------------------------------------------------------------
MidiMapView::MidiMapView (Session& s) : session (s)
{
    session.addChangeListener (this);
    addAndMakeVisible (controllerBox);
    controllerBox.setTooltip ("Choose the controller page to edit");
    controllerBox.onChange = [this] {
        if (updating)
            return;
        const int i = controllerBox.getSelectedId() - 1;
        if (i >= 0 && i < static_cast<int> (profiles.size()))
            selectedId = profiles[static_cast<std::size_t> (i)].id;
        if (matrix)
            matrix->rebuild();
        repaint();
    };

    addAndMakeVisible (bankLabel);
    bankLabel.setJustificationType (juce::Justification::centred);
    bankLabel.setColour (juce::Label::textColourId, currentTheme().colour ("textMuted"));
    bankLabel.setFont (currentTheme().font (11.5f));
    addAndMakeVisible (bankPrev);
    addAndMakeVisible (bankNext);
    bankPrev.setTooltip ("Previous 8 tracks");
    bankNext.setTooltip ("Next 8 tracks");
    bankPrev.onClick = [this] { session.setSurfaceBank (session.surfaceBank() - 8); };
    bankNext.onClick = [this] { session.setSurfaceBank (session.surfaceBank() + 8); };

    addAndMakeVisible (askButton);
    askButton.setTooltip ("Ask the AI to propose a mapping for this song");
    askButton.onClick = [this] { askAi(); };

    matrix = std::make_unique<MatrixPanel> (*this);
    matrixViewport.setViewedComponent (matrix.get(), false);
    matrixViewport.setScrollBarsShown (true, true);
    matrixViewport.setScrollBarThickness (8);
    addAndMakeVisible (matrixViewport);
    refresh();
}

MidiMapView::~MidiMapView()
{
    session.removeChangeListener (this);
}

const ControllerProfile* MidiMapView::selectedProfile() const
{
    for (const auto& p : profiles)
        if (p.id == selectedId)
            return &p;
    return profiles.empty() ? nullptr : &profiles.front();
}

std::vector<MidiMapView::Control> MidiMapView::controlsFor (const ControllerProfile& p) const
{
    std::vector<Control> out;
    for (const auto& b : p.bindings)
        for (int i = 0; i < b.count; ++i)
        {
            Control c;
            c.isNote = b.isNote;
            c.channel = b.channel;
            c.number = b.number + i;
            c.strip = b.firstStrip + i;
            c.row = b.row;
            c.action = b.action;
            c.mode = juce::String::fromUTF8 (b.mode.c_str());
            if (b.action == ControlAction::launchClip)
                c.shortName = "PAD " + juce::String (b.row + 1) + "." + juce::String (i + 1);
            else if (b.action == ControlAction::masterVolume)
                c.shortName = "MASTER";
            else if (isValue (b.action))
                c.shortName = "KNOB " + juce::String (b.firstStrip + i + 1);
            else if (b.action == ControlAction::play || b.action == ControlAction::stop
                     || b.action == ControlAction::record || b.action == ControlAction::loop
                     || b.action == ControlAction::launchScene || b.action == ControlAction::stopClips)
                c.shortName = (b.isNote ? "NOTE " : "CC ") + juce::String (b.number + i);
            else
                c.shortName = "BTN " + juce::String (b.firstStrip + i + 1);
            c.fullName = actionName (b.action) + " · " + (b.isNote ? "note " : "CC ") + juce::String (b.number + i);
            if (b.firstStrip > 0 || b.count > 1)
                c.fullName << " · strip " << juce::String (b.firstStrip + i + 1);
            if (! c.mode.isEmpty())
                c.fullName << " · " << c.mode;
            out.push_back (std::move (c));
        }
    const auto rank = [] (ControlAction a) {
        switch (a)
        {
            case ControlAction::launchClip: return 0;
            case ControlAction::volume: return 1;
            case ControlAction::pan: return 2;
            case ControlAction::reverb: return 3;
            case ControlAction::chorus: return 4;
            case ControlAction::mute: return 5;
            case ControlAction::solo: return 6;
            case ControlAction::arm: return 7;
            case ControlAction::select: return 8;
            case ControlAction::panCentre: return 9;
            case ControlAction::masterVolume: return 10;
            case ControlAction::play: return 11;
            case ControlAction::stop: return 12;
            case ControlAction::record: return 13;
            case ControlAction::loop: return 14;
            case ControlAction::launchScene: return 15;
            case ControlAction::stopClips: return 16;
            default: return 17;
        }
    };
    std::stable_sort (out.begin(), out.end(), [&] (const Control& a, const Control& b) { return rank (a.action) < rank (b.action); });
    return out;
}

std::optional<MidiMapView::Control> MidiMapView::cellControl (int track, ControlAction action, int row) const
{
    const auto* p = selectedProfile();
    if (p == nullptr)
        return {};
    for (const auto& b : p->bindings)
    {
        if (b.action != action || (action == ControlAction::launchClip && b.row != row))
            continue;
        const int target = track < 0 ? 0 : track;
        if (target < b.firstStrip || target >= b.firstStrip + b.count)
            continue;
        const int i = target - b.firstStrip;
        Control c;
        c.isNote = b.isNote;
        c.channel = b.channel;
        c.number = b.number + i;
        c.strip = b.firstStrip + i;
        c.row = b.row;
        c.action = b.action;
        c.mode = juce::String::fromUTF8 (b.mode.c_str());
        if (b.action == ControlAction::launchClip)
            c.shortName = "PAD " + juce::String (b.row + 1) + "." + juce::String (i + 1);
        else if (b.action == ControlAction::masterVolume)
            c.shortName = "MASTER";
        else if (isValue (b.action))
            c.shortName = "KNOB " + juce::String (b.firstStrip + i + 1);
        else
            c.shortName = "BTN " + juce::String (b.firstStrip + i + 1);
        c.fullName = actionName (b.action) + " · " + (b.isNote ? "note " : "CC ") + juce::String (b.number + i);
        return c;
    }
    return {};
}

void MidiMapView::assign (int track, ControlAction action, int row, const Control& c)
{
    auto* p = [&]() -> ControllerProfile* {
        for (auto& profile : profiles)
            if (profile.id == selectedId)
                return &profile;
        return profiles.empty() ? nullptr : &profiles.front();
    }();
    if (p == nullptr)
        return;

    // Every binding becomes one control, so a cell can be changed alone.
    std::vector<ControllerProfile::Binding> flat;
    for (const auto& b : p->bindings)
        for (int i = 0; i < b.count; ++i)
        {
            auto x = b;
            x.count = 1;
            x.firstStrip = b.firstStrip + i;
            x.number = b.number + i;
            flat.push_back (std::move (x));
        }

    // One physical control has one target. One target has one control.
    std::erase_if (flat, [&] (const auto& b) {
        return b.isNote == c.isNote && b.channel == c.channel && b.number == c.number && b.mode == c.mode.toStdString();
    });
    std::erase_if (flat, [&] (const auto& b) {
        return b.action == action && b.firstStrip == (track < 0 ? 0 : track)
            && (action != ControlAction::launchClip || b.row == row);
    });

    ControllerProfile::Binding b;
    b.isNote = c.isNote;
    b.channel = c.channel;
    b.number = c.number;
    b.count = 1;
    b.firstStrip = track < 0 ? 0 : track;
    b.action = action;
    b.row = action == ControlAction::launchClip ? row : 0;
    b.mode = c.mode.toStdString();
    flat.push_back (std::move (b));
    p->bindings = std::move (flat);

    if (onProfileChanged)
        onProfileChanged (*p);
    if (matrix)
    {
        matrix->rebuild();
        matrix->repaint();
    }
    repaint();
}

void MidiMapView::clearCell (int track, ControlAction action, int row)
{
    auto* p = [&]() -> ControllerProfile* {
        for (auto& profile : profiles)
            if (profile.id == selectedId)
                return &profile;
        return profiles.empty() ? nullptr : &profiles.front();
    }();
    if (p == nullptr)
        return;
    std::vector<ControllerProfile::Binding> flat;
    for (const auto& b : p->bindings)
        for (int i = 0; i < b.count; ++i)
        {
            auto x = b;
            x.count = 1;
            x.firstStrip = b.firstStrip + i;
            x.number = b.number + i;
            flat.push_back (std::move (x));
        }
    std::erase_if (flat, [&] (const auto& b) {
        return b.action == action && b.firstStrip == (track < 0 ? 0 : track)
            && (action != ControlAction::launchClip || b.row == row);
    });
    p->bindings = std::move (flat);
    if (onProfileChanged)
        onProfileChanged (*p);
    if (matrix)
    {
        matrix->rebuild();
        matrix->repaint();
    }
    repaint();
}

void MidiMapView::showControlMenu (int track, ControlAction action, int row)
{
    const auto* p = selectedProfile();
    if (p == nullptr)
        return;
    const auto controls = controlsFor (*p);
    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String (actionName (action))
                           + (track >= 0 ? juce::String (" · track ") + juce::String (session.surfaceBank() + track + 1) : juce::String (" · global")));
    if (controls.empty())
    {
        menu.addItem ("No controls on this page", false, false, [] {});
    }
    else
    {
        const auto current = cellControl (track, action, row);
        for (int i = 0; i < static_cast<int> (controls.size()); ++i)
        {
            const auto& c = controls[static_cast<std::size_t> (i)];
            const bool ticked = current.has_value() && current->isNote == c.isNote && current->channel == c.channel
                && current->number == c.number && current->mode == c.mode;
            menu.addItem (c.shortName + "   " + c.fullName, true, ticked, [this, track, action, row, controls, i] {
                assign (track, action, row, controls[static_cast<std::size_t> (i)]);
            });
        }
        if (current.has_value())
        {
            menu.addSeparator();
            menu.addItem ("Clear this cell", [this, track, action, row] { clearCell (track, action, row); });
        }
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [] (int) {});
}

void MidiMapView::askAi()
{
    if (! onAskAi)
        return;
    const auto* p = selectedProfile();
    juce::String prompt;
    prompt << "Map the MIDI controls to this song. Use the MIDI Map view as the target list: "
           << "each track has Volume, Pan, Send A, Send B, Mute, Solo, Arm and four clip rows. "
           << "Keep one physical control for one target and prefer the knobs for values and the buttons for switches. ";
    prompt << "Tracks: ";
    const auto& song = session.song();
    for (std::size_t i = 0; i < song.tracks.size() && i < 16; ++i)
        prompt << (i == 0 ? "" : ", ") << juce::String::fromUTF8 (song.tracks[i].name.c_str());
    prompt << ". ";
    if (p != nullptr)
    {
        prompt << "Controller: " << juce::String::fromUTF8 (p->name.c_str()) << ". ";
        const auto controls = controlsFor (*p);
        prompt << "Controls: ";
        for (std::size_t i = 0; i < controls.size() && i < 24; ++i)
            prompt << (i == 0 ? "" : ", ") << controls[i].shortName << " (" << controls[i].fullName << ")";
    }
    prompt << ". Use read_device_file and write_device_file to save the controller page when you can; "
           << "the MIDI Map view updates at once. Otherwise give a compact table, one row per track and one column per control.";
    onAskAi (prompt);
}

void MidiMapView::refresh()
{
    profiles = session.controllerPages();
    bool found = false;
    for (const auto& p : profiles)
        if (p.id == selectedId)
            found = true;
    if (! found)
    {
        const ControllerProfile* preferred = profiles.empty() ? nullptr : &profiles.front();
        for (const auto& p : profiles)
            if (p.padRows() > (preferred != nullptr ? preferred->padRows() : 0))
                preferred = &p;
        selectedId = preferred != nullptr ? juce::String::fromUTF8 (preferred->id.c_str()) : juce::String();
    }
    updating = true;
    controllerBox.clear (juce::dontSendNotification);
    int selected = 0;
    for (int i = 0; i < static_cast<int> (profiles.size()); ++i)
    {
        controllerBox.addItem (juce::String::fromUTF8 (profiles[static_cast<std::size_t> (i)].name.c_str()), i + 1);
        if (profiles[static_cast<std::size_t> (i)].id == selectedId)
            selected = i + 1;
    }
    controllerBox.setSelectedId (selected, juce::dontSendNotification);
    const int bank = session.surfaceBank();
    const int tracks = static_cast<int> (session.song().tracks.size());
    const int last = juce::jmin (tracks, bank + 8);
    bankLabel.setText (tracks == 0 ? juce::String ("No tracks")
                                   : "Tracks " + juce::String (bank + 1) + juce::String::fromUTF8 ("\xe2\x80\x93") + juce::String (last),
                      juce::dontSendNotification);
    bankPrev.setEnabled (bank > 0);
    bankNext.setEnabled (bank + 8 < tracks);
    updating = false;
    if (matrix)
        matrix->rebuild();
    resized();
    repaint();
}

void MidiMapView::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    auto r = getLocalBounds();
    auto top = r.removeFromTop (kHeader);
    g.setColour (t.colour ("text"));
    g.setFont (t.font (21.0f, true));
    g.drawText ("MIDI Map", top.withTrimmedLeft (4).removeFromTop (30), juce::Justification::topLeft);
    g.setColour (t.colour ("textMuted"));
    g.setFont (t.font (12.0f));
    g.drawText ("All instruments by all app controls. Click a cell to choose the knob, button or pad.",
                top.withTrimmedLeft (4).withTrimmedTop (28), juce::Justification::topLeft, true);
    g.setColour (t.colour ("accent").withAlpha (0.35f));
    g.fillRoundedRectangle (4.0f, static_cast<float> (kHeader - 10), 62.0f, 2.0f, 1.0f);

    r.removeFromTop (8);
    const int cardWidth = juce::jlimit (260, 360, getWidth() * 30 / 100);
    auto card = r.removeFromLeft (cardWidth);
    r.removeFromLeft (12);
    drawControllerCard (g, card);
    matrixViewport.setBounds (r);
}

void MidiMapView::drawControllerCard (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& t = currentTheme();
    const auto* p = selectedProfile();
    const auto card = area.toFloat();
    juce::ColourGradient shade (t.colour ("panelRaised"), card.getTopLeft(), t.colour ("panel"), card.getBottomRight(), false);
    g.setGradientFill (shade);
    g.fillRoundedRectangle (card, t.metric ("radius", 9.0f) + 3.0f);
    g.setColour (t.colour ("line"));
    g.drawRoundedRectangle (card.reduced (0.5f), t.metric ("radius", 9.0f) + 3.0f, 1.0f);

    auto inner = area.reduced (18, 16);
    g.setColour (t.colour ("accent"));
    g.setFont (t.font (10.0f, true).withExtraKerningFactor (0.16f));
    g.drawText ("CONTROLLER", inner.removeFromTop (16), juce::Justification::centredLeft);
    inner.removeFromTop (4);

    if (p == nullptr)
    {
        g.setColour (t.colour ("textMuted"));
        g.setFont (t.font (13.0f));
        g.drawFittedText ("No controller page loaded. Connect a controller, or add a page in the Devices folder.",
                          inner, juce::Justification::topLeft, 4);
        return;
    }

    g.setColour (t.colour ("text"));
    g.setFont (t.font (19.0f, true));
    g.drawText (juce::String::fromUTF8 (p->name.c_str()), inner.removeFromTop (26), juce::Justification::centredLeft, true);
    g.setColour (t.colour ("textMuted"));
    g.setFont (t.font (11.5f));
    const auto controls = controlsFor (*p);
    g.drawText (juce::String (controls.size()) + " controls · " + juce::String (p->strips) + " strips",
                inner.removeFromTop (18), juce::Justification::centredLeft);
    inner.removeFromTop (10);

    // Group the controls, so the card shows the whole page at a glance.
    struct Group
    {
        ControlAction action = ControlAction::none;
        int count = 0;
        juce::String first, last;
        bool isNote = false;
    };
    std::vector<Group> groups;
    for (const auto& c : controls)
    {
        if (groups.empty() || groups.back().action != c.action)
            groups.push_back ({ c.action, 0, c.shortName, c.shortName, c.isNote });
        auto& group = groups.back();
        ++group.count;
        group.last = c.shortName;
    }

    const int shown = juce::jmin (10, static_cast<int> (groups.size()));
    for (int i = 0; i < shown; ++i)
    {
        const auto& group = groups[static_cast<std::size_t> (i)];
        auto row = inner.removeFromTop (33);
        const bool value = isValue (group.action);
        const bool clip = group.action == ControlAction::launchClip;
        const auto icon = row.removeFromLeft (26).withSizeKeepingCentre (20, 20).toFloat();
        g.setColour (t.colour (value ? "accent" : (clip ? "playing" : "textMuted")).withAlpha (value || clip ? 0.9f : 0.5f));
        if (value)
        {
            g.drawEllipse (icon.reduced (2.0f), 1.5f);
            g.fillEllipse (icon.getCentreX() - 2.5f, icon.getCentreY() - 2.5f, 5.0f, 5.0f);
        }
        else if (clip)
        {
            g.fillRoundedRectangle (icon.reduced (2.0f), 4.0f);
        }
        else
            g.drawRoundedRectangle (icon.reduced (2.0f), 4.0f, 1.5f);
        row.removeFromLeft (6);

        g.setColour (t.colour ("text"));
        g.setFont (t.font (12.0f, true));
        g.drawText (actionName (group.action), row.removeFromLeft (88), juce::Justification::centredLeft);
        g.setColour (t.colour ("textDim"));
        g.setFont (t.font (10.5f));
        const auto kind = group.action == ControlAction::masterVolume ? " faders" : (value ? " knobs" : (clip ? " pads" : " buttons"));
        g.drawText (juce::String (group.count) + kind, row.removeFromLeft (58), juce::Justification::centredLeft);
        const auto range = group.first == group.last ? group.first : group.first + "–" + group.last;
        g.drawText (range, row, juce::Justification::centredRight, true);
    }
    if (static_cast<int> (groups.size()) > shown)
    {
        g.setColour (t.colour ("textDim"));
        g.setFont (t.font (11.0f));
        g.drawText ("+ " + juce::String (static_cast<int> (groups.size()) - shown) + " more groups",
                    inner.removeFromTop (22), juce::Justification::centredLeft);
    }

    // A small legend at the bottom.
    auto legend = area.reduced (18, 14).removeFromBottom (18);
    g.setColour (t.colour ("textDim"));
    g.setFont (t.font (10.5f));
    g.drawText ("Knobs = value · buttons = switch · pads = clip", legend, juce::Justification::centredLeft);
}

void MidiMapView::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (kHeader).reduced (4, 0);
    auto right = top.removeFromRight (juce::jmin (460, getWidth() / 2));
    askButton.setBounds (right.removeFromRight (92).withSizeKeepingCentre (92, 30));
    right.removeFromRight (8);
    bankNext.setBounds (right.removeFromRight (24).withSizeKeepingCentre (24, 30));
    bankPrev.setBounds (right.removeFromRight (24).withSizeKeepingCentre (24, 30));
    bankLabel.setBounds (right.removeFromRight (112).withSizeKeepingCentre (112, 30));
    right.removeFromRight (6);
    const int boxWidth = juce::jmax (100, right.getWidth());
    controllerBox.setBounds (right.withSizeKeepingCentre (boxWidth, 30));

    auto body = getLocalBounds().withTrimmedTop (kHeader + 8);
    const int cardWidth = juce::jlimit (260, 360, getWidth() * 30 / 100);
    matrixViewport.setBounds (body.withTrimmedLeft (cardWidth + 12));

    if (matrix)
    {
        matrix->rebuild();
        matrix->setSize (juce::jmax (matrixViewport.getWidth() - 8, kTrackWidth + static_cast<int> (std::size (columns)) * kCellWidth + 12),
                         matrix->getHeight());
    }
}

} // namespace arranger::ui

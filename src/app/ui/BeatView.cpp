#include "BeatView.h"

#include "arranger/core/GeneralMidi.h"

namespace arranger::ui
{

namespace
{
    juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    constexpr int kSoft = 60, kNormal = 100, kAccent = 127;
    constexpr int kDefaultDrums[] = { 49, 51, 46, 42, 45, 39, 38, 36 }; // crash, ride, hats, tom, clap, snare, kick
}

BeatView::BeatView (Session& s) : session (s)
{
    session.addChangeListener (this);

    addAndMakeVisible (title);
    title.setInterceptsMouseClicks (false, false);

    addAndMakeVisible (stepBox);
    stepBox.addItem ("1/8 steps", 2);
    stepBox.addItem ("1/16 steps", 4);
    stepBox.addItem ("1/32 steps", 8);
    stepBox.addItem ("1/8 triplets", 3);
    stepBox.addItem ("1/16 triplets", 6);
    stepBox.setSelectedId (stepsPerBeat, juce::dontSendNotification);
    stepBox.setTooltip ("The length of one step");
    stepBox.onChange = [this] {
        stepsPerBeat = juce::jmax (1, stepBox.getSelectedId());
        repaint();
    };

    addAndMakeVisible (barsBox);
    for (const int bars : { 1, 2, 4, 8 })
        barsBox.addItem (juce::String (bars) + (bars == 1 ? " bar" : " bars"), bars);
    barsBox.setTooltip ("The length of the pattern. The pattern repeats to the end of its clip.");
    barsBox.onChange = [this] { setBars (barsBox.getSelectedId()); };

    addAndMakeVisible (clearButton);
    clearButton.setTooltip ("Remove all notes from the pattern");
    clearButton.onClick = [this] {
        if (const auto id = patternClip())
            session.edit ("Clear Pattern", [id] (Song& song) { edit::modifyClip (song, id, [] (Clip& c) { c.notes.clear(); }); });
    };

    for (auto* b : { &earlierButton, &laterButton })
        addChildComponent (*b);
    earlierButton.setTooltip ("Show the bars before");
    laterButton.setTooltip ("Show the bars after");
    earlierButton.onClick = [this] { moveWindow (-1); };
    laterButton.onClick = [this] { moveWindow (1); };

    addAndMakeVisible (placeButton);
    placeButton.setTooltip ("Play the pattern from one bar to another, on this track or on a new drum track");
    placeButton.onClick = [this] { placePattern(); };

    addAndMakeVisible (newButton);
    newButton.setTooltip ("Make a one-bar pattern at the playhead on the selected track");
    newButton.onClick = [this] { newPattern(); };

    syncControls();
}

BeatView::~BeatView()
{
    session.removeChangeListener (this);
}

void BeatView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    syncControls();
    repaint();
}

ClipPtr BeatView::clip() const
{
    const auto& song = session.song();
    const auto* track = edit::findTrack (song, session.selectedTrack());
    if (track == nullptr)
        return nullptr;

    // The clip open in the editor (also a Clip grid slot), else the one under the playhead, else the first.
    if (const auto edited = session.editedClip(); edited != 0)
    {
        for (const auto& c : track->clips)
            if (c->id == edited)
                return c;
        for (const auto& c : track->slots)
            if (c != nullptr && c->id == edited)
                return c;
    }
    if (track->clips.empty())
        return nullptr;
    const auto at = static_cast<Tick> (session.position());
    for (const auto& c : track->clips)
        if (at >= c->start && at < c->end())
            return c;
    return track->clips.front();
}

ClipId BeatView::patternClip() const
{
    const auto c = clip();
    return c != nullptr ? c->id : 0;
}

Tick BeatView::patternLength (const Clip& c) const
{
    return c.loopLength > 0 && c.loopLength < c.length ? c.loopLength : c.length;
}

Tick BeatView::stepTicks() const
{
    return kTicksPerQuarter / stepsPerBeat;
}

Tick BeatView::windowLength (const Clip& c) const
{
    // A pattern of up to 8 bars shows whole; a longer clip (an imported drum part) shows 4 bars at a time.
    const Tick bar = juce::jmax<Tick> (1, session.song().timeSignature.ticksPerBar());
    const Tick length = patternLength (c);
    return length > bar * 8 ? bar * 4 : length;
}

Tick BeatView::windowStart() const
{
    const auto c = clip();
    if (c == nullptr)
        return 0;
    const Tick window = windowLength (*c), length = patternLength (*c);
    if (window >= length)
        return 0;
    const Tick at = juce::jmax<Tick> (0, static_cast<Tick> (session.position()) - c->start) % length;
    return (at / window) * window;
}

void BeatView::moveWindow (int direction)
{
    const auto c = clip();
    if (c == nullptr)
        return;
    const Tick window = windowLength (*c), length = patternLength (*c);
    const Tick next = juce::jlimit<Tick> (0, ((length - 1) / window) * window, windowStart() + direction * window);
    session.locate (c->start + next);
    syncControls();
    repaint();
}

int BeatView::stepCount() const
{
    const auto c = clip();
    if (c == nullptr)
        return 0;
    const Tick shown = juce::jmin (windowLength (*c), patternLength (*c) - windowStart());
    return static_cast<int> (juce::jlimit<Tick> (1, 256, (shown + stepTicks() - 1) / stepTicks()));
}

std::vector<BeatView::Row> BeatView::rows() const
{
    std::vector<Row> out;
    const auto* track = edit::findTrack (session.song(), session.selectedTrack());
    if (track == nullptr)
        return out;

    std::vector<int> notes;
    if (track->output.kind == TrackOutput::Kind::sampler && ! track->kit.pads.empty())
        for (const auto& p : track->kit.pads)
            notes.push_back (p.note);
    else
        notes.assign (std::begin (kDefaultDrums), std::end (kDefaultDrums));

    // Any other pitch the clip already uses gets a row too, so nothing is hidden.
    if (const auto c = clip())
        for (const auto& n : c->notes)
            if (std::find (notes.begin(), notes.end(), n.pitch) == notes.end())
                notes.push_back (n.pitch);
    std::sort (notes.begin(), notes.end(), std::greater<>());
    notes.erase (std::unique (notes.begin(), notes.end()), notes.end());

    for (const int note : notes)
    {
        juce::String name;
        if (const auto* pad = track->kit.padForNote (note); pad != nullptr && track->output.kind == TrackOutput::Kind::sampler)
            name = utf8 (pad->name);
        if (name.isEmpty())
            name = gmDrumName (note);
        if (name.isEmpty())
            name = juce::MidiMessage::getMidiNoteName (note, true, true, 3);
        out.push_back ({ note, name });
    }
    return out;
}

void BeatView::syncControls()
{
    const auto c = clip();
    const auto* track = edit::findTrack (session.song(), session.selectedTrack());
    const bool has = c != nullptr;
    for (auto* comp : std::initializer_list<juce::Component*> { &stepBox, &barsBox, &clearButton })
        comp->setVisible (has);
    newButton.setVisible (track != nullptr);
    newButton.setButtonText (has ? "New pattern" : "New pattern on this track");

    juce::String text;
    if (track == nullptr)
        text = "Add a track to make a beat.";
    else if (! has)
        text = utf8 (track->name);
    else
    {
        text = utf8 (track->name) + "  /  " + (c->name.empty() ? juce::String ("Pattern") : utf8 (c->name));
        const auto barTicks = juce::jmax<Tick> (1, session.song().timeSignature.ticksPerBar());
        if (windowLength (*c) < patternLength (*c))
            text << "  /  bars " << (windowStart() / barTicks + 1) << "-" << ((windowStart() + windowLength (*c)) / barTicks)
                 << " of " << (patternLength (*c) / barTicks);
    }
    title.setText (text, juce::dontSendNotification);
    title.setFont (currentTheme().font (15.0f, true));
    title.setColour (juce::Label::textColourId, currentTheme().colour ("text"));

    if (has)
    {
        const auto bar = session.song().timeSignature.ticksPerBar();
        const int bars = static_cast<int> (juce::jmax<Tick> (1, patternLength (*c) / juce::jmax<Tick> (1, bar)));
        barsBox.setSelectedId (bars, juce::dontSendNotification);
        if (barsBox.getSelectedId() == 0)
            barsBox.setText (juce::String (bars) + " bars", juce::dontSendNotification);
    }
    resized();
}

void BeatView::resized()
{
    auto top = getLocalBounds().removeFromTop (kHeader).reduced (14, 10);
    clearButton.setBounds (top.removeFromRight (70));
    top.removeFromRight (8);
    placeButton.setVisible (clip() != nullptr);
    if (placeButton.isVisible())
    {
        placeButton.setBounds (top.removeFromRight (116));
        top.removeFromRight (8);
    }
    newButton.setBounds (top.removeFromRight (clip() != nullptr ? 110 : 190));
    top.removeFromRight (8);
    barsBox.setBounds (top.removeFromRight (90));
    top.removeFromRight (8);
    stepBox.setBounds (top.removeFromRight (120));
    top.removeFromRight (12);
    const auto c = clip();
    const bool paged = c != nullptr && windowLength (*c) < patternLength (*c);
    earlierButton.setVisible (paged);
    laterButton.setVisible (paged);
    if (paged)
    {
        laterButton.setBounds (top.removeFromRight (30));
        top.removeFromRight (4);
        earlierButton.setBounds (top.removeFromRight (30));
        top.removeFromRight (12);
    }
    title.setBounds (top);
}

int BeatView::rowHeight() const
{
    // Rows grow to use the space, within sensible limits.
    const int count = juce::jmax (1, static_cast<int> (rows().size()));
    return juce::jmax (24, (getHeight() - kHeader - 40) / count);
}

juce::Rectangle<int> BeatView::gridArea() const
{
    return getLocalBounds().withTrimmedTop (kHeader).withTrimmedLeft (kNames).reduced (0, 4).withTrimmedRight (14);
}

juce::Rectangle<float> BeatView::cell (int row, int step) const
{
    const auto area = gridArea().toFloat();
    const int steps = juce::jmax (1, stepCount());
    const float w = area.getWidth() / static_cast<float> (steps);
    const auto h = static_cast<float> (rowHeight());
    return { area.getX() + static_cast<float> (step) * w, area.getY() + static_cast<float> (row) * h, w, h };
}

bool BeatView::hit (juce::Point<int> p, int& row, int& step) const
{
    const int rowCount = static_cast<int> (rows().size());
    const int steps = stepCount();
    if (steps == 0 || rowCount == 0)
        return false;
    const auto first = cell (0, 0);
    row = static_cast<int> (std::floor ((static_cast<float> (p.y) - first.getY()) / first.getHeight()));
    step = static_cast<int> (std::floor ((static_cast<float> (p.x) - first.getX()) / first.getWidth()));
    return row >= 0 && row < rowCount && step >= 0 && step < steps;
}

int BeatView::velocityAt (const Clip& c, int note, int step) const
{
    const Tick from = windowStart() + step * stepTicks(), to = from + stepTicks();
    for (const auto& n : c.notes)
        if (n.pitch == note && n.start >= from && n.start < to)
            return n.velocity;
    return 0;
}

void BeatView::setStep (int row, int step, int velocity)
{
    const auto c = clip();
    const auto list = rows();
    if (c == nullptr || row < 0 || row >= static_cast<int> (list.size()))
        return;
    const int note = list[static_cast<std::size_t> (row)].note;
    if (velocityAt (*c, note, step) == velocity)
        return;

    const Tick from = windowStart() + step * stepTicks(), to = from + stepTicks();
    const Tick length = juce::jmax<Tick> (1, stepTicks() / 2);
    session.edit (velocity > 0 ? "Add Step" : "Remove Step", [id = c->id, note, from, to, length, velocity] (Song& song) {
        edit::modifyClip (song, id, [&] (Clip& clip) {
            std::erase_if (clip.notes, [&] (const Note& n) { return n.pitch == note && n.start >= from && n.start < to; });
            if (velocity > 0)
                clip.notes.push_back ({ from, length, static_cast<std::uint8_t> (note), static_cast<std::uint8_t> (velocity) });
            edit::normalise (clip);
        });
    }, "beat-steps");

    if (velocity > 0 && ! session.isPlaying())
        session.previewNote (session.selectedTrack(), note, velocity, 120);
}

void BeatView::copyPattern (bool cut, int row)
{
    const auto c = clip();
    const auto list = rows();
    if (c == nullptr || row >= static_cast<int> (list.size()))
        return;
    const int pitch = row >= 0 ? list[static_cast<std::size_t> (row)].note : -1;
    const Tick length = patternLength (*c);
    const auto inside = [&] (const Note& n) { return n.start < length && (pitch < 0 || n.pitch == pitch); };
    std::vector<edit::NoteKey> keys;
    for (const auto& n : c->notes)
        if (inside (n))
            keys.push_back ({ c->id, n });
    if (keys.empty())
        return;
    session.clipboard = clipboard::copyNotes (session.song(), keys);
    // The pattern copies from its start, not from its first note: a paste keeps the steps in place.
    const Tick first = keys.front().note.start;
    for (auto& n : session.clipboard.notes)
        n.start += first;
    session.clipboard.span += first;
    if (cut)
        session.edit (row >= 0 ? "Cut Row" : "Cut Pattern", [id = c->id, length, pitch] (Song& song) {
            edit::modifyClip (song, id, [&] (Clip& clip) {
                std::erase_if (clip.notes, [&] (const Note& n) { return n.start < length && (pitch < 0 || n.pitch == pitch); });
            });
        });
}

void BeatView::pastePattern (int row)
{
    const auto c = clip();
    const auto list = rows();
    const auto content = session.clipboard;
    if (c == nullptr || content.kind != clipboard::Content::Kind::notes || row >= static_cast<int> (list.size()))
        return;
    const int pitch = row >= 0 ? list[static_cast<std::size_t> (row)].note : -1;
    const Tick length = patternLength (*c);
    session.edit (row >= 0 ? "Paste Row" : "Paste Pattern", [&] (Song& song) {
        edit::modifyClip (song, c->id, [&] (Clip& clip) {
            std::erase_if (clip.notes, [&] (const Note& n) { return n.start < length && (pitch < 0 || n.pitch == pitch); });
            for (auto n : content.notes)
            {
                if (n.start >= length)
                    continue;
                if (pitch >= 0)
                    n.pitch = static_cast<std::uint8_t> (pitch);
                std::erase_if (clip.notes, [&] (const Note& o) { return o.start == n.start && o.pitch == n.pitch; });
                clip.notes.push_back (n);
            }
            edit::normalise (clip);
        });
    });
}

void BeatView::showRowMenu (int row)
{
    const auto c = clip();
    const auto list = rows();
    if (c == nullptr || row < 0 || row >= static_cast<int> (list.size()))
        return;
    const bool canPaste = session.clipboard.kind == clipboard::Content::Kind::notes;
    juce::PopupMenu m;
    m.addSectionHeader (list[static_cast<std::size_t> (row)].name);
    m.addItem (1, "Cut Row");
    m.addItem (2, "Copy Row");
    m.addItem (3, "Paste to Row", canPaste);
    m.addSeparator();
    m.addItem (4, "Cut Pattern");
    m.addItem (5, "Copy Pattern");
    m.addItem (6, "Paste Pattern", canPaste);
    juce::Component::SafePointer<BeatView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, row] (int r) {
        if (safe == nullptr) return;
        if (r == 1 || r == 2) safe->copyPattern (r == 1, row);
        else if (r == 3) safe->pastePattern (row);
        else if (r == 4 || r == 5) safe->copyPattern (r == 4);
        else if (r == 6) safe->pastePattern();
    });
}

void BeatView::newPattern()
{
    const auto track = session.selectedTrack();
    if (edit::findTrack (session.song(), track) == nullptr)
        return;
    const auto bar = session.song().timeSignature.ticksPerBar();
    Tick start = (static_cast<Tick> (session.position()) / bar) * bar;

    // Start after any clip that is in the way.
    if (const auto* t = edit::findTrack (session.song(), track))
        for (const auto& c : t->clips)
            if (start < c->end() && start + bar > c->start)
                start = ((c->end() + bar - 1) / bar) * bar;

    ClipId made = 0;
    session.edit ("New Pattern", [&] (Song& song) {
        Clip c;
        c.name = "Pattern";
        c.start = start;
        c.length = bar * 4;
        c.loopLength = bar;
        made = edit::addClip (song, track, c);
    });
    session.selectClips ({ made });
    session.openInEditor (made);
}

void BeatView::placePattern()
{
    const auto c = clip();
    const auto* track = edit::findTrack (session.song(), session.selectedTrack());
    if (c == nullptr || track == nullptr)
        return;
    const Tick bar = session.song().timeSignature.ticksPerBar();
    const int firstBar = static_cast<int> (static_cast<Tick> (session.position()) / bar) + 1;
    const int patternBars = static_cast<int> (juce::jmax<Tick> (1, patternLength (*c) / bar));

    placeWindow = std::make_unique<juce::AlertWindow> ("Put the pattern in bars",
                                                       "The pattern repeats from the first bar to the last bar. What is there now on that track is replaced.",
                                                       juce::MessageBoxIconType::NoIcon, this);
    placeWindow->addTextEditor ("from", juce::String (firstBar), "From bar");
    placeWindow->addTextEditor ("to", juce::String (firstBar + juce::jmax (4, patternBars) - 1), "To bar");
    placeWindow->addComboBox ("where", { "On this track: " + utf8 (track->name), "On a new drum track" }, "Where");
    if (auto* box = placeWindow->getComboBoxComponent ("where"))
        box->setSelectedItemIndex (0);
    placeWindow->addButton ("Put", 1, juce::KeyPress (juce::KeyPress::returnKey));
    placeWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    placeWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this, bar] (int result) {
        if (result != 1 || placeWindow == nullptr)
        {
            placeWindow.reset();
            return;
        }
        const int from = placeWindow->getTextEditorContents ("from").getIntValue();
        const int to = placeWindow->getTextEditorContents ("to").getIntValue();
        const bool newTrack = placeWindow->getComboBoxComponent ("where")->getSelectedItemIndex() == 1;
        placeWindow.reset();
        const auto c = clip();
        const auto* track = edit::findTrack (session.song(), session.selectedTrack());
        if (c == nullptr || track == nullptr || from < 1 || to < from)
            return;

        // One looping clip: the pattern plays over and over from `from` to the end of `to`.
        Clip placed;
        placed.name = c->name.empty() ? "Pattern" : c->name;
        placed.start = (from - 1) * bar;
        placed.length = (to - from + 1) * bar;
        placed.loopLength = std::min (patternLength (*c), placed.length);
        for (const auto& n : c->notes)
            if (n.start < placed.loopLength)
                placed.notes.push_back (n);
        for (const auto& e : c->controls)
            if (e.at < placed.loopLength)
                placed.controls.push_back (e);

        ClipId made = 0;
        TrackId target = track->id;
        session.edit ("Put Pattern in Bars", [&] (Song& song) {
            if (newTrack)
            {
                const auto* source = edit::findTrack (song, target);
                const auto output = source->output;
                const auto sound = source->sound;
                const auto kit = source->kit;
                const auto mixer = source->mixer;
                const auto name = source->name + " " + juce::String (from).toStdString() + "-" + juce::String (to).toStdString();
                auto& t = edit::addTrack (song, name, edit::trackIndex (song, target));
                t.output = output;
                t.sound = sound;
                t.kit = kit;
                t.mixer = mixer;
                t.mixer.solo = false;
                target = t.id;
            }
            else
                edit::clearRange (song, target, placed.start, placed.start + placed.length);
            made = edit::addClip (song, target, placed);
        });
        session.selectTrack (target);
        session.selectClips ({ made });
        session.openInEditor (made);
    }));
}

void BeatView::setBars (int bars)
{
    const auto c = clip();
    if (c == nullptr || bars <= 0)
        return;
    const Tick length = session.song().timeSignature.ticksPerBar() * bars;
    session.edit ("Change Pattern Length", [id = c->id, length] (Song& song) {
        edit::modifyClip (song, id, [length] (Clip& clip) {
            clip.loopLength = length;
            clip.length = std::max (clip.length, length);
        });
    });
}

void BeatView::mouseDown (const juce::MouseEvent& e)
{
    session.endMerge();
    const auto list = rows();

    // A click on a name plays the pad.
    if (e.x < kNames && e.y > kHeader)
    {
        const int row = (e.y - gridArea().getY()) / rowHeight();
        if (e.mods.isPopupMenu())
            showRowMenu (row);
        else if (row >= 0 && row < static_cast<int> (list.size()))
            session.previewNote (session.selectedTrack(), list[static_cast<std::size_t> (row)].note, kNormal, 200);
        return;
    }

    int row = 0, step = 0;
    const auto c = clip();
    if (c == nullptr || ! hit (e.getPosition(), row, step))
        return;
    const int now = velocityAt (*c, list[static_cast<std::size_t> (row)].note, step);

    if (e.mods.isPopupMenu())
        paintVelocity = now == 0 ? kSoft : (now < kNormal ? kNormal : (now < kAccent ? kAccent : kSoft)); // right-click: soft, normal, accent
    else if (now > 0)
        paintVelocity = 0;
    else
        paintVelocity = e.mods.isShiftDown() ? kAccent : (e.mods.isAltDown() ? kSoft : kNormal);
    painting = true;
    setStep (row, step, paintVelocity);
}

void BeatView::mouseDrag (const juce::MouseEvent& e)
{
    int row = 0, step = 0;
    if (painting && hit (e.getPosition(), row, step))
        setStep (row, step, paintVelocity);
}

void BeatView::mouseUp (const juce::MouseEvent&)
{
    painting = false;
    session.endMerge();
}

void BeatView::refresh()
{
    int step = -1;
    if (const auto c = clip(); c != nullptr && session.isPlaying())
    {
        const auto at = static_cast<Tick> (session.position());
        if (at >= c->start && at < c->end())
        {
            const Tick inWindow = (at - c->start) % patternLength (*c) - windowStart();
            step = inWindow >= 0 && inWindow < windowLength (*c) ? static_cast<int> (inWindow / stepTicks()) : -1;
        }
    }
    // A long clip: the window follows the playhead.
    if (windowStart() != shownWindow)
    {
        shownWindow = windowStart();
        syncControls();
        repaint();
    }
    if (step != shownStep)
    {
        shownStep = step;
        repaint (gridArea().expanded (2, 6));
    }
}

void BeatView::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    const auto c = clip();
    const auto* track = edit::findTrack (session.song(), session.selectedTrack());

    g.setColour (t.colour ("line"));
    g.fillRect (0, kHeader - 1, getWidth(), 1);

    if (c == nullptr)
    {
        g.setColour (t.colour ("textDim"));
        g.setFont (t.font (15.0f));
        g.drawFittedText (track == nullptr ? "Add a track first." : "This track has no pattern yet.\nClick \"New pattern on this track\".",
                          getLocalBounds().withTrimmedTop (kHeader), juce::Justification::centred, 3);
        return;
    }

    const auto list = rows();
    const int steps = stepCount();
    const auto colour = t.trackColour (utf8 (track->colour));
    const int stepsPerBar = juce::jmax (1, static_cast<int> (session.song().timeSignature.ticksPerBar() / stepTicks()));

    for (int r = 0; r < static_cast<int> (list.size()); ++r)
    {
        const auto first = cell (r, 0);
        g.setColour (t.colour ("textMuted"));
        g.setFont (t.font (13.0f));
        g.drawText (list[static_cast<std::size_t> (r)].name, 16, static_cast<int> (first.getY()), kNames - 24, rowHeight(), juce::Justification::centredLeft);

        for (int s = 0; s < steps; ++s)
        {
            const auto box = cell (r, s).reduced (2.5f);
            const int velocity = velocityAt (*c, list[static_cast<std::size_t> (r)].note, s);
            const bool onBeat = s % stepsPerBeat == 0;
            const bool otherBar = (s / stepsPerBar) % 2 == 1;

            if (velocity > 0)
                g.setColour (colour.withAlpha (velocity >= kAccent ? 1.0f : (velocity >= kNormal ? 0.78f : 0.45f)));
            else
                g.setColour (t.colour (onBeat ? "panelRaised" : "background").withAlpha (otherBar ? 0.7f : 1.0f));
            g.fillRoundedRectangle (box, 5.0f);
            if (velocity == 0)
            {
                g.setColour (t.colour ("line"));
                g.drawRoundedRectangle (box, 5.0f, 1.0f);
            }
            if (s == shownStep)
            {
                g.setColour (t.colour ("playhead").withAlpha (velocity > 0 ? 0.9f : 0.35f));
                g.drawRoundedRectangle (box.expanded (1.0f), 6.0f, 1.5f);
            }
        }
    }

    // Beat numbers under the grid.
    g.setFont (t.font (11.0f));
    g.setColour (t.colour ("textDim"));
    const float y = cell (static_cast<int> (list.size()), 0).getY() + 2.0f;
    for (int s = 0; s < steps; s += stepsPerBeat)
    {
        const int bar = static_cast<int> (windowStart() / juce::jmax<Tick> (1, session.song().timeSignature.ticksPerBar())) + s / stepsPerBar + 1, beat = (s % stepsPerBar) / stepsPerBeat + 1;
        g.drawText (juce::String (bar) + "." + juce::String (beat), cell (0, s).withY (y).withHeight (16.0f).withWidth (60.0f), juce::Justification::centredLeft);
    }
}

} // namespace arranger::ui

#include "ChordsView.h"
#include "ProposalCard.h"

namespace arranger::ui
{

namespace
{
    using card::utf8;

    juce::String barText (const Song& s, Tick t)
    {
        const Tick bar = std::max<Tick> (1, s.timeSignature.ticksPerBar());
        return "bar " + juce::String (t / bar + 1);
    }
}

// ---- the pads ---------------------------------------------------------------------------
/** Sixteen chord pads in two rows: press to play, release to stop. */
class ChordsView::Pads final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Pads (Session& s) : session (s) {}

    void paint (juce::Graphics& g) override
    {
        const auto& t = currentTheme();
        const auto pal = session.chordPalette();
        for (int i = 0; i < 16 && i < static_cast<int> (pal.size()); ++i)
        {
            const auto& p = pal[static_cast<std::size_t> (i)];
            const auto r = cell (i).toFloat().reduced (2.0f);
            const auto tint = t.trackColour (p.group == "diatonic" ? "teal" : (p.group == "borrowed" ? "amber" : "violet"));
            const bool held = session.heldPad() == i;
            g.setColour (held ? tint : tint.withAlpha (t.isDark ? 0.18f : 0.16f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (tint.withAlpha (held ? 1.0f : 0.55f));
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
            g.setColour (held ? t.colour ("accentText") : t.colour ("text"));
            g.setFont (t.font (13.0f, true));
            auto text = r.reduced (4.0f, 3.0f);
            g.drawFittedText (utf8 (p.name), text.removeFromTop (text.getHeight() * 0.55f).toNearestInt(), juce::Justification::centredBottom, 1, 0.6f);
            g.setColour (held ? t.colour ("accentText") : t.colour ("textMuted"));
            g.setFont (t.font (10.5f));
            g.drawFittedText (utf8 (p.roman), text.toNearestInt(), juce::Justification::centredTop, 1);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < 16; ++i)
            if (cell (i).contains (e.getPosition()))
            {
                session.padChord (i, true);
                pressed = i;
                return;
            }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (pressed >= 0)
            session.padChord (pressed, false);
        pressed = -1;
    }

private:
    juce::Rectangle<int> cell (int i) const
    {
        const int w = getWidth() / 8, h = getHeight() / 2;
        return { (i % 8) * w, (i / 8) * h, w, h };
    }

    Session& session;
    int pressed = -1;
};

// ---- the view ----------------------------------------------------------------------------
ChordsView::ChordsView (Session& s) : session (s)
{
    const auto& theme = currentTheme();
    for (auto* l : { &title, &keyLabel, &nowLabel, &typeHeading, &fromLabel, &nextLabel, &padHeading, &invLabel, &voicingLabel, &octaveLabel,
                     &keysHeading, &gridLabel, &zoneLabel, &keysHint, &reharmHeading, &reharmLabel, &status })
    {
        const bool heading = l == &title || l == &typeHeading || l == &padHeading || l == &keysHeading || l == &reharmHeading;
        l->setColour (juce::Label::textColourId, theme.colour (heading || l == &nowLabel ? "text" : "textMuted"));
        l->setFont (heading ? theme.font (l == &title ? 15.0f : 13.0f, true) : theme.font (12.0f));
        list.addAndMakeVisible (*l);
    }
    title.setText ("Smart Chords", juce::dontSendNotification);
    nowLabel.setFont (theme.font (13.0f, true));
    typeHeading.setText ("Type chords", juce::dontSendNotification);
    fromLabel.setText ("From bar", juce::dontSendNotification);
    nextLabel.setText ("Next:", juce::dontSendNotification);
    padHeading.setText ("Chord pads", juce::dontSendNotification);
    invLabel.setText ("Inversion", juce::dontSendNotification);
    voicingLabel.setText ("Voicing", juce::dontSendNotification);
    octaveLabel.setText ("Octave", juce::dontSendNotification);
    keysHeading.setText ("From the keyboard", juce::dontSendNotification);
    gridLabel.setText ("Grid", juce::dontSendNotification);
    zoneLabel.setText ("Keys", juce::dontSendNotification);
    keysHint.setText ("While recording, each chord goes to the chord lane at the grid. When stopped, a chord is written at the playhead as you let go, "
                      "and the playhead moves on.", juce::dontSendNotification);
    keysHint.setJustificationType (juce::Justification::topLeft);
    reharmHeading.setText ("Reharmonize", juce::dontSendNotification);

    typed.setTextToShowWhenEmpty ("Am F | C G   or   vi IV I V", theme.colour ("textDim"));
    typed.setTooltip ("Chord names or roman numerals. Without | each chord is one bar; | ends a bar and the chords in it share it. "
                      "% repeats the bar before, / holds a chord.");
    typed.onTextChange = [this] { refreshSuggestions(); };
    typed.onReturnKey = [this] { writeTyped(); };
    fromBar.setInputRestrictions (4, "0123456789");
    fromBar.setJustification (juce::Justification::centred);
    fromBar.setText ("1", false);
    fromBar.onTextChange = [this] { if (typed.isEmpty()) refreshSuggestions(); };
    writeButton.setTooltip ("Write the chords to the chord lane (one undo step)");
    writeButton.onClick = [this] { writeTyped(); };
    for (auto* c : std::initializer_list<juce::Component*> { &typed, &fromBar, &writeButton })
        list.addAndMakeVisible (*c);

    pads = std::make_unique<Pads> (session);
    pads->setTooltip ("Press a pad to play its chord on the selected track. Teal: the key's own chords. Amber: borrowed. Violet: secondary dominants.");
    list.addAndMakeVisible (*pads);

    for (int i = 0; i < 4; ++i)
        inversion.addItem (i == 0 ? "Root" : juce::String (i) + (i == 1 ? "st" : (i == 2 ? "nd" : "rd")), i + 1);
    for (int i = 0; i < chord_input::kVoicingCount; ++i)
        voicing.addItem (chord_input::voicingName (static_cast<chord_input::Voicing> (i)), i + 1);
    octave.addItem ("Low (C3)", 1);
    octave.addItem ("Middle (C4)", 2);
    octave.addItem ("High (C5)", 3);
    voicing.setTooltip ("Smooth picks the inversion nearest to the chord before, so the notes move least");
    for (auto* c : { &inversion, &voicing, &octave })
    {
        c->onChange = [this] { readVoicing(); };
        list.addAndMakeVisible (*c);
    }
    padsOnController.setTooltip ("The Launchkey's 16 pads play the chord pads in place of the Clip grid. Knobs 1, 2 and 3 set inversion, voicing and octave.");
    padsOnController.onClick = [this] { readVoicing(); };
    list.addAndMakeVisible (padsOnController);

    grid.addItem ("Bar", 1);
    grid.addItem ("Half bar", 2);
    grid.addItem ("Beat", 3);
    zone.addItem ("Whole keyboard", 1);
    zone.addItem ("Left hand (below the split)", 2);
    zone.setTooltip ("Left hand: only the keys below the chord split point (F#3) name the chord, as on an arranger keyboard");
    fromKeys.setTooltip ("Chords you play on a MIDI keyboard (and on the pads) go to the chord lane");
    for (auto* c : std::initializer_list<juce::Component*> { &grid, &zone, &fromKeys })
        list.addAndMakeVisible (*c);
    grid.onChange = zone.onChange = [this] { readVoicing(); };
    fromKeys.onClick = [this] { readVoicing(); };

    reharmButton.setTooltip ("Other chords for the chord at the playhead, to compare by ear");
    reharmButton.onClick = [this] { reharmonizeAt (static_cast<Tick> (session.position())); };
    stopButton.setTooltip ("Stop the comparison: the song plays as it is");
    stopButton.onClick = [this] { stopPlaying(); };
    list.addAndMakeVisible (reharmButton);
    list.addAndMakeVisible (stopButton);

    // The settings as the session has them.
    const auto& c = session.chordInput();
    inversion.setSelectedId (c.voicing.inversion + 1, juce::dontSendNotification);
    voicing.setSelectedId (static_cast<int> (c.voicing.voicing) + 1, juce::dontSendNotification);
    octave.setSelectedId (std::clamp ((c.voicing.centre - 48) / 12 + 1, 1, 3), juce::dontSendNotification);
    padsOnController.setToggleState (c.padsPlayChords, juce::dontSendNotification);
    fromKeys.setToggleState (c.fromKeys, juce::dontSendNotification);
    zone.setSelectedId (c.belowSplitOnly ? 2 : 1, juce::dontSendNotification);
    const Tick bar = std::max<Tick> (1, session.song().timeSignature.ticksPerBar());
    grid.setSelectedId (c.grid == 0 || c.grid >= bar ? 1 : (c.grid >= bar / 2 ? 2 : 3), juce::dontSendNotification);

    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    session.addChangeListener (this);
    refreshLabels();
    refreshSuggestions();
}

ChordsView::~ChordsView()
{
    stopPlaying();
    session.removeChangeListener (this);
}

void ChordsView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshLabels();
    if (pads)
        pads->repaint();
    // Settings changed from the controller knobs.
    const auto& c = session.chordInput();
    inversion.setSelectedId (c.voicing.inversion + 1, juce::dontSendNotification);
    voicing.setSelectedId (static_cast<int> (c.voicing.voicing) + 1, juce::dontSendNotification);
    octave.setSelectedId (std::clamp ((c.voicing.centre - 48) / 12 + 1, 1, 3), juce::dontSendNotification);
    if (card != nullptr && ! arranging::isCurrent (session.song(), proposal) && playingOption == -2)
    {
        list.removeChildComponent (card.get());
        juce::MessageManager::callAsync ([retired = std::shared_ptr<ProposalCard> (std::move (card))] {});
        layoutList();
    }
}

void ChordsView::refreshLabels()
{
    const auto& song = session.song();
    const auto key = session.songKey();
    const auto* track = edit::findTrack (song, chord_input::chordTrack (song, &session.deviceLibrary()));
    const auto keyText = "Key: " + utf8 (theory::name (key)) + (track != nullptr ? "   |   Chord lane on " + utf8 (track->name) : juce::String());
    keyLabel.setText (keyText, juce::dontSendNotification);

    juce::String now;
    if (const auto h = session.heardChord())
        now = "Heard: " + utf8 (theory::chordName (*h, key)) + "  (" + utf8 (theory::roman (*h, key)) + ")";
    else
    {
        const auto lane = chord_input::laneChords (song, &session.deviceLibrary());
        if (const auto* c = chord_input::chordAt (lane, static_cast<Tick> (session.position())))
            now = "At the playhead: " + utf8 (c->text) + (c->chord ? "  (" + utf8 (theory::roman (*c->chord, key)) + ")" : juce::String());
        else
            now = "No chord at the playhead";
    }
    nowLabel.setText (now, juce::dontSendNotification);

    const auto lane = chord_input::laneChords (song, &session.deviceLibrary());
    const auto* at = chord_input::chordAt (lane, static_cast<Tick> (session.position()));
    reharmLabel.setText (at != nullptr ? "Chord at the playhead: " + utf8 (at->text) + " (" + barText (song, at->at) + ")"
                                       : juce::String ("Put the playhead on a chord of the chord lane."),
                         juce::dontSendNotification);
    // Suggestions follow the lane when nothing is typed.
    const auto labels = keyText + now;
    if (labels != lastLabels && typed.isEmpty())
        refreshSuggestions();
    lastLabels = labels;
}

void ChordsView::refreshSuggestions()
{
    const auto& song = session.song();
    const auto key = session.songKey();
    std::vector<Chord> before;
    if (! typed.isEmpty())
        before = chord_input::chordsIn (typed.getText().toStdString(), key);
    else
    {
        // The lane chords before the bar to write from.
        const Tick bar = std::max<Tick> (1, song.timeSignature.ticksPerBar());
        const Tick from = std::max (0, fromBar.getText().getIntValue() - 1) * bar;
        for (const auto& c : chord_input::laneChords (song, &session.deviceLibrary()))
            if (c.at < from && c.chord)
                before.push_back (*c.chord);
    }
    if (before.size() > 3)
        before.erase (before.begin(), before.end() - 3);
    suggestions = chord_input::suggestNext (before, key, 6);

    for (auto& b : suggestionButtons)
        list.removeChildComponent (b.get());
    if (! suggestionButtons.empty())
    {
        auto retired = std::make_shared<std::vector<std::unique_ptr<juce::TextButton>>> (std::move (suggestionButtons));
        suggestionButtons.clear();
        juce::MessageManager::callAsync ([retired] {});
    }
    for (std::size_t i = 0; i < suggestions.size(); ++i)
    {
        const auto& sug = suggestions[i];
        auto b = std::make_unique<juce::TextButton> (utf8 (sug.name) + "  " + utf8 (sug.roman));
        b->setTooltip (utf8 (sug.why) + ". Click to add it.");
        b->onClick = [safe = juce::Component::SafePointer<ChordsView> (this), name = utf8 (sug.name)] {
            if (safe == nullptr)
                return;
            const auto chosen = name;
            auto text = safe->typed.getText().trimEnd();
            safe->typed.setText (text.isEmpty() ? chosen : text + " " + chosen, true);
            safe->typed.moveCaretToEnd();
        };
        list.addAndMakeVisible (*b);
        suggestionButtons.push_back (std::move (b));
    }
    nextLabel.setText (before.empty() ? "Start with:" : "Next:", juce::dontSendNotification);
    if (! getLocalBounds().isEmpty())
        layoutList();
}

void ChordsView::writeTyped()
{
    const auto text = typed.getText().trim();
    if (text.isEmpty())
        return;
    const auto& song = session.song();
    if (song.tracks.empty())
    {
        setStatus ("Add a track first: the chord lane is kept on a track.");
        return;
    }
    const Tick bar = std::max<Tick> (1, song.timeSignature.ticksPerBar());
    const int first = std::max (1, fromBar.getText().getIntValue());
    const auto t = chord_input::typeChords (text.toStdString(), session.songKey(), song.timeSignature, (first - 1) * bar);
    if (t.marks.empty())
    {
        setStatus ("No chords in that text.");
        return;
    }
    session.edit ("Type Chords", [&] (Song& s) { chord_input::writeChords (s, (first - 1) * bar, t.end, t.marks, &session.deviceLibrary()); });
    juce::String msg = "Wrote " + juce::String (t.bars) + (t.bars == 1 ? " bar" : " bars") + " from bar " + juce::String (first) + ".";
    if (! t.unknown.empty())
    {
        msg << " Not chords:";
        for (const auto& w : t.unknown)
            msg << " " << utf8 (w);
    }
    setStatus (msg);
    fromBar.setText (juce::String (first + t.bars), false);
    typed.clear();
    refreshSuggestions();
}

void ChordsView::typeAt (int bar, const juce::String& text)
{
    fromBar.setText (juce::String (std::max (1, bar)), false);
    typed.setText (text, false);
    refreshSuggestions();
    typed.grabKeyboardFocus();
}

void ChordsView::readVoicing()
{
    auto c = session.chordInput();
    c.voicing.inversion = std::max (0, inversion.getSelectedId() - 1);
    c.voicing.voicing = static_cast<chord_input::Voicing> (std::max (0, voicing.getSelectedId() - 1));
    c.voicing.centre = 48 + std::max (0, octave.getSelectedId() - 1) * 12;
    c.padsPlayChords = padsOnController.getToggleState();
    c.fromKeys = fromKeys.getToggleState();
    c.belowSplitOnly = zone.getSelectedId() == 2;
    const Tick bar = std::max<Tick> (1, session.song().timeSignature.ticksPerBar());
    const int g = grid.getSelectedId();
    c.grid = g == 3 ? session.song().timeSignature.ticksPerBeat() : (g == 2 ? bar / 2 : 0);
    session.setChordInput (c);
}

void ChordsView::reharmonizeAt (Tick at)
{
    stopPlaying();
    reharmTick = at;
    proposal = chord_input::reharmonize (session.song(), at, &session.deviceLibrary());
    if (card != nullptr)
    {
        list.removeChildComponent (card.get());
        juce::MessageManager::callAsync ([retired = std::shared_ptr<ProposalCard> (std::move (card))] {});
    }
    if (proposal.options.empty())
        setStatus (utf8 (proposal.because));
    else
    {
        card = std::make_unique<ProposalCard> (session.song(), proposal);
        card->onPlay = [safe = juce::Component::SafePointer<ChordsView> (this)] (int option) { if (safe != nullptr) safe->play (option); };
        card->onUse = [safe = juce::Component::SafePointer<ChordsView> (this)] (int option) { if (safe != nullptr) safe->use (option); };
        card->onDismiss = [safe = juce::Component::SafePointer<ChordsView> (this)] {
            if (safe == nullptr)
                return;
            safe->stopPlaying();
            if (safe->card != nullptr)
            {
                safe->list.removeChildComponent (safe->card.get());
                juce::MessageManager::callAsync ([retired = std::shared_ptr<ProposalCard> (std::move (safe->card))] {});
            }
            safe->layoutList();
        };
        list.addAndMakeVisible (*card);
        setStatus ({});
    }
    layoutList();
    if (card != nullptr)
        viewport.setViewPosition (0, std::max (0, card->getY() - 60));
}

void ChordsView::play (int option)
{
    if (card == nullptr)
        return;
    const auto& song = session.song();
    if (! arranging::isCurrent (song, proposal))
    {
        setStatus ("The song changed. Reharmonize again.");
        return;
    }
    const Tick bar = std::max<Tick> (1, song.timeSignature.ticksPerBar());
    const Tick from = proposal.from / bar * bar;
    const Tick to = std::min (std::max ((proposal.to + bar - 1) / bar * bar, from + bar), from + bar * 8);
    session.audition (option < 0 ? song : arranging::withOption (song, proposal.options[static_cast<std::size_t> (option)]), from, to);
    playingOption = option;
    card->setPlaying (option);
}

void ChordsView::stopPlaying()
{
    if (playingOption != -2 && session.isAuditioning())
        session.audition (std::nullopt, proposal.from);
    playingOption = -2;
    if (card != nullptr)
        card->setPlaying (-2);
}

void ChordsView::use (int option)
{
    stopPlaying();
    const auto p = proposal;
    const auto& o = p.options[static_cast<std::size_t> (option)];
    try
    {
        session.edit (p.title + ": " + o.label, [&] (Song& s) { arranging::apply (s, p, option); });
    }
    catch (const std::exception& e)
    {
        setStatus (utf8 (e.what()));
        return;
    }
    if (card != nullptr)
    {
        list.removeChildComponent (card.get());
        juce::MessageManager::callAsync ([retired = std::shared_ptr<ProposalCard> (std::move (card))] {});
    }
    layoutList();
    setStatus ("Done: " + utf8 (o.label) + ". Undo takes it back.");
}

void ChordsView::setStatus (const juce::String& text)
{
    status.setText (text, juce::dontSendNotification);
    if (! getLocalBounds().isEmpty())
        layoutList();
}

void ChordsView::paint (juce::Graphics& g)
{
    g.fillAll (currentTheme().colour ("panel"));
}

void ChordsView::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutList();
}

void ChordsView::layoutList()
{
    const int width = std::max (260, viewport.getMaximumVisibleWidth());
    const int w = width - kPad * 2;
    int y = kPad;
    const auto row = [&] (int h) {
        juce::Rectangle<int> r (kPad, y, w, h);
        y += h + 6;
        return r;
    };
    const auto heading = [&] (juce::Label& l) {
        y += 6;
        l.setBounds (row (20));
    };

    title.setBounds (row (22));
    keyLabel.setBounds (row (18));
    nowLabel.setBounds (row (20));

    heading (typeHeading);
    typed.setBounds (row (26));
    {
        auto r = row (26);
        fromLabel.setBounds (r.removeFromLeft (60));
        fromBar.setBounds (r.removeFromLeft (44));
        r.removeFromLeft (6);
        writeButton.setBounds (r.removeFromLeft (64));
    }
    {
        // The suggestions wrap onto more rows.
        constexpr int labelW = 74;
        int x = kPad + labelW;
        int rowY = y;
        nextLabel.setBounds (kPad, rowY, labelW, 26);
        for (auto& b : suggestionButtons)
        {
            const int bw = std::max (64, juce::GlyphArrangement::getStringWidthInt (currentTheme().font (12.0f), b->getButtonText()) + 20);
            if (x + bw > kPad + w && x > kPad + labelW)
            {
                x = kPad + labelW;
                rowY += 30;
            }
            b->setBounds (x, rowY, bw, 26);
            x += bw + 6;
        }
        y = rowY + 32;
    }

    heading (padHeading);
    pads->setBounds (row (juce::jlimit (90, 130, w / 4)));
    {
        auto r = row (24);
        const int half = (r.getWidth() - 12) / 2;
        auto a = r.removeFromLeft (half);
        invLabel.setBounds (a.removeFromLeft (64));
        inversion.setBounds (a);
        r.removeFromLeft (12);
        voicingLabel.setBounds (r.removeFromLeft (56));
        voicing.setBounds (r);
    }
    {
        auto r = row (24);
        octaveLabel.setBounds (r.removeFromLeft (64));
        octave.setBounds (r.removeFromLeft ((r.getWidth() - 12) / 2 + 0));
    }
    padsOnController.setBounds (row (24));

    heading (keysHeading);
    fromKeys.setBounds (row (24));
    {
        auto r = row (24);
        gridLabel.setBounds (r.removeFromLeft (34));
        grid.setBounds (r.removeFromLeft (100));
        r.removeFromLeft (12);
        zoneLabel.setBounds (r.removeFromLeft (34));
        zone.setBounds (r);
    }
    keysHint.setBounds (row (static_cast<int> (textHeight (keysHint.getText(), keysHint.getFont(), w)) + 6));

    heading (reharmHeading);
    reharmLabel.setBounds (row (18));
    {
        auto r = row (26);
        reharmButton.setBounds (r.removeFromLeft (110));
        r.removeFromLeft (6);
        stopButton.setBounds (r.removeFromLeft (60));
    }
    if (status.getText().isNotEmpty())
        status.setBounds (row (static_cast<int> (textHeight (status.getText(), status.getFont(), w)) + 6));
    else
        status.setBounds ({});
    if (card != nullptr)
    {
        const int h = card->heightFor (w);
        card->setBounds (kPad, y, w, h);
        y += h + 6;
    }
    list.setSize (width, y + kPad);
}

} // namespace arranger::ui

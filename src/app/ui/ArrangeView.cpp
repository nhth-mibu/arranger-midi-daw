#include "ArrangeView.h"

#include "arranger/core/HardwareFit.h"
#include "arranger/core/SongEdit.h"

namespace arranger::ui
{

namespace
{
    juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    constexpr int kPad = 10;

    /** Wrapped text: its height follows its width. */
    float textHeight (const juce::String& text, const juce::Font& font, int width)
    {
        if (text.isEmpty() || width <= 0)
            return 0.0f;
        juce::AttributedString s;
        s.setWordWrap (juce::AttributedString::byWord);
        s.append (text, font);
        juce::TextLayout layout;
        layout.createLayout (s, static_cast<float> (width));
        return std::ceil (layout.getHeight());
    }

    void drawText (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour, juce::Rectangle<float> area)
    {
        juce::AttributedString s;
        s.setWordWrap (juce::AttributedString::byWord);
        s.append (text, font, colour);
        juce::TextLayout layout;
        layout.createLayout (s, area.getWidth());
        layout.draw (g, area);
    }

    void styleNumber (juce::TextEditor& e)
    {
        e.setInputRestrictions (4, "0123456789");
        e.setJustification (juce::Justification::centred);
    }
}

/** Wrapped text that sets its own height. */
class TextBlock final : public juce::Component
{
public:
    juce::String text;
    float size = 12.5f;

    int heightFor (int width) const { return static_cast<int> (textHeight (text, currentTheme().font (size), width)) + 4; }
    void paint (juce::Graphics& g) override
    {
        drawText (g, text, currentTheme().font (size), currentTheme().colour ("textMuted"), getLocalBounds().toFloat());
    }
};

/** One proposal: what was seen, and its options to play and use. */
class ProposalCard final : public juce::Component
{
public:
    ProposalCard (const Song& song, const arranging::Proposal& p) : proposal (p)
    {
        juce::StringArray names;
        for (auto id : p.tracks)
            if (const auto* t = edit::findTrack (song, id))
                names.add (utf8 (t->name));
        meta = utf8 (arranging::barsText (song, p.from, p.to)) + "  ·  " + names.joinIntoString (", ");
        for (std::size_t i = 0; i < p.options.size(); ++i)
        {
            auto row = std::make_unique<Row>();
            row->label = utf8 (p.options[i].label);
            row->summary = utf8 (p.options[i].summary);
            row->play.setButtonText (juce::String ("Play ") + (i == 0 ? "A" : "B"));
            row->play.setTooltip ("Play this version of the bars, looping. Switch between the original and A or B while it plays.");
            row->use.setButtonText ("Use");
            row->use.setTooltip ("Make this change (one undo step)");
            const int option = static_cast<int> (i);
            row->play.onClick = [this, option] { if (onPlay) onPlay (option); };
            row->use.onClick = [this, option] { if (onUse) onUse (option); };
            addAndMakeVisible (row->play);
            addAndMakeVisible (row->use);
            rows.push_back (std::move (row));
        }
        for (auto* b : { &original, &dismiss })
            addAndMakeVisible (*b);
        original.setTooltip ("Play the bars as they are now");
        dismiss.setTooltip ("Hide this proposal. The app remembers it, so this kind comes later next time.");
        original.onClick = [this] { if (onPlay) onPlay (-1); };
        dismiss.onClick = [this] { if (onDismiss) onDismiss(); };
    }

    std::function<void (int)> onPlay, onUse;
    std::function<void()> onDismiss;

    void setPlaying (int option)
    {
        playing = option;
        repaint();
    }

    int heightFor (int width) const
    {
        const auto& t = currentTheme();
        const int w = width - kPad * 2;
        float h = kPad + textHeight (utf8 (proposal.title), t.font (13.5f, true), w) + 4 + textHeight (utf8 (proposal.because), t.font (12.0f), w) + 4 + 18;
        for (const auto& r : rows)
            h += 8 + textHeight (r->label, t.font (12.5f, true), w) + textHeight (r->summary, t.font (11.5f), w) + 4 + 26;
        h += 10 + 26 + kPad;
        return static_cast<int> (h);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = currentTheme();
        const auto box = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (t.colour ("panelRaised"));
        g.fillRoundedRectangle (box, 8.0f);
        g.setColour (playing != -2 ? t.colour ("accent") : t.colour ("line"));
        g.drawRoundedRectangle (box, 8.0f, playing != -2 ? 1.6f : 1.0f);

        const int w = getWidth() - kPad * 2;
        float y = kPad;
        const auto block = [&] (const juce::String& text, const juce::Font& font, juce::Colour colour) {
            const float h = textHeight (text, font, w);
            drawText (g, text, font, colour, { static_cast<float> (kPad), y, static_cast<float> (w), h });
            y += h;
        };
        block (utf8 (proposal.title), t.font (13.5f, true), t.colour ("text"));
        y += 4;
        block (utf8 (proposal.because), t.font (12.0f), t.colour ("textMuted"));
        y += 4;
        g.setColour (t.colour ("textDim"));
        g.setFont (t.font (11.0f));
        g.drawText (meta, kPad, static_cast<int> (y), w, 16, juce::Justification::centredLeft, true);
        y += 18;
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            y += 8;
            const auto mark = juce::String (i == 0 ? "A  " : "B  ");
            block (mark + rows[i]->label, t.font (12.5f, true), static_cast<int> (i) == playing ? t.colour ("accent") : t.colour ("text"));
            block (rows[i]->summary, t.font (11.5f), t.colour ("textMuted"));
            y += 4 + 26;
        }
        if (playing == -1)
        {
            g.setColour (t.colour ("accent"));
            g.setFont (t.font (11.0f, true));
            g.drawText ("Playing the original", kPad, getHeight() - kPad - 26, w - 200, 26, juce::Justification::centredRight);
        }
    }

    void resized() override
    {
        const auto& t = currentTheme();
        const int w = getWidth() - kPad * 2;
        float y = kPad + textHeight (utf8 (proposal.title), t.font (13.5f, true), w) + 4 + textHeight (utf8 (proposal.because), t.font (12.0f), w) + 4 + 18;
        for (auto& r : rows)
        {
            y += 8 + textHeight (r->label, t.font (12.5f, true), w) + textHeight (r->summary, t.font (11.5f), w) + 4;
            juce::Rectangle<int> line (kPad, static_cast<int> (y), w, 24);
            r->play.setBounds (line.removeFromLeft (72));
            line.removeFromLeft (6);
            r->use.setBounds (line.removeFromLeft (64));
            y += 26;
        }
        juce::Rectangle<int> bottom (kPad, getHeight() - kPad - 24, w, 24);
        original.setBounds (bottom.removeFromLeft (90));
        bottom.removeFromLeft (6);
        dismiss.setBounds (bottom.removeFromLeft (76));
    }

private:
    struct Row
    {
        juce::String label, summary;
        juce::TextButton play, use;
    };
    arranging::Proposal proposal;
    juce::String meta;
    std::vector<std::unique_ptr<Row>> rows;
    juce::TextButton original { "Original" }, dismiss { "Dismiss" };
    int playing = -2;
};

// ---- the view ----------------------------------------------------------------------
juce::String ArrangeView::titleFor (const juce::String& tool)
{
    return tool == "hardware_fit" ? "Fit to My Synths" : "Arrangement Doctor";
}

ArrangeView::ArrangeView (Session& s, const juce::String& t) : session (s), tool (t)
{
    const auto& theme = currentTheme();
    for (auto* l : { &title, &about, &goalLabel, &barsLabel, &toLabel, &melodyLabel, &status })
    {
        l->setColour (juce::Label::textColourId, theme.colour (l == &title ? "text" : "textMuted"));
        addAndMakeVisible (*l);
    }
    title.setText (titleFor (tool), juce::dontSendNotification);
    title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    about.setJustificationType (juce::Justification::topLeft);
    about.setMinimumHorizontalScale (1.0f);
    about.setText (doctor() ? "Choose a goal. Play each proposal against the original (A/B), then use the ones you like. The melody never changes."
                            : "Does the song fit your synths: notes at once, parts, note range, drum kits? Bass, melody and drums are protected.",
                   juce::dontSendNotification);
    status.setJustificationType (juce::Justification::topLeft);

    goalLabel.setText ("Goal", juce::dontSendNotification);
    barsLabel.setText ("Bars", juce::dontSendNotification);
    toLabel.setText ("to", juce::dontSendNotification);
    toLabel.setJustificationType (juce::Justification::centred);
    melodyLabel.setText ("Melody", juce::dontSendNotification);
    int id = 1;
    for (auto g : { arranging::Goal::standOut, arranging::Goal::clearMelody, arranging::Goal::lessClutter, arranging::Goal::moreVariation })
        goal.addItem (utf8 (arranging::goalName (g)), id++);
    goal.setSelectedId (1, juce::dontSendNotification);
    goal.onChange = [this] { check(); };

    const auto& song = session.song();
    melody.addItem ("Guess", 1);
    melodyIds.push_back (0);
    for (const auto& tr : song.tracks)
        if (tr.output.kind != TrackOutput::Kind::audio)
        {
            melodyIds.push_back (tr.id);
            melody.addItem (utf8 (tr.name), static_cast<int> (melodyIds.size()));
        }
    melody.setSelectedId (1, juce::dontSendNotification);
    melody.onChange = [this] { check(); };

    // Bars: the loop, else the selected clips; empty = the whole song.
    const Tick bar = std::max<Tick> (1, song.timeSignature.ticksPerBar());
    Tick from = 0, to = 0;
    if (song.loop.enabled && song.loop.isValid())
    {
        from = song.loop.start;
        to = song.loop.end;
    }
    else if (! session.selectedClips().empty())
    {
        from = std::numeric_limits<Tick>::max();
        for (const auto clip : session.selectedClips())
            if (const auto c = edit::findClipPtr (song, clip))
            {
                from = std::min (from, c->start);
                to = std::max (to, c->end());
            }
    }
    for (auto* e : { &fromBar, &toBar })
    {
        styleNumber (*e);
        e->setTextToShowWhenEmpty ("all", theme.colour ("textDim"));
        e->onReturnKey = [this] { check(); };
    }
    if (to > from)
    {
        fromBar.setText (juce::String (from / bar + 1), false);
        toBar.setText (juce::String ((to - 1) / bar + 1), false);
    }
    // With bars chosen, lift them; else look at the busy bars of the whole song.
    goal.setSelectedId (to > from ? 1 : 3, juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &goal, &melody, &fromBar, &toBar })
        addAndMakeVisible (*c);
    for (auto* c : std::initializer_list<juce::Component*> { &goalLabel, &goal, &barsLabel, &toLabel, &fromBar, &toBar })
        c->setVisible (doctor());

    checkButton.setButtonText (doctor() ? "Check" : "Check Again");
    checkButton.onClick = [this] { check(); };
    stopButton.onClick = [this] { stopPlaying(); };
    stopButton.setTooltip ("Stop the comparison: the song plays as it is");
    saveButton.onClick = [this] { saveCapabilities(); };
    saveButton.setTooltip ("Keep these numbers for your synths (they win over the built-in profile)");
    addAndMakeVisible (checkButton);
    addAndMakeVisible (stopButton);

    findings = std::make_unique<TextBlock>();
    list.addAndMakeVisible (*findings);
    list.addChildComponent (saveButton);
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    check();
}

ArrangeView::~ArrangeView()
{
    stopPlaying();
}

void ArrangeView::setStatus (const juce::String& text)
{
    status.setText (text, juce::dontSendNotification);
    if (! getLocalBounds().isEmpty())
        resized();
}

void ArrangeView::check()
{
    stopPlaying();
    const auto& song = session.song();
    const auto melodyId = melodyIds[static_cast<std::size_t> (std::max (0, melody.getSelectedId() - 1))];
    juce::String found;
    proposals.clear();
    setStatus ({});
    try
    {
        if (doctor())
        {
            arranging::DoctorRequest r;
            r.goal = static_cast<arranging::Goal> (goal.getSelectedId() - 1);
            const Tick bar = std::max<Tick> (1, song.timeSignature.ticksPerBar());
            const int a = fromBar.getText().getIntValue(), b = toBar.getText().getIntValue();
            if (a >= 1 && b >= a)
            {
                r.from = (a - 1) * bar;
                r.to = b * bar;
            }
            r.melody = melodyId;
            r.devices = &session.deviceLibrary();
            r.preferences = &session.arrangingPreferences();
            const auto report = arranging::diagnose (song, r);
            for (const auto& f : report.findings)
                found << "• " << utf8 (f.text) << "\n";
            if (report.melody == 0)
                found << "• No melody track found: choose it above to protect it and to check room for it.\n";
            proposals = report.proposals;
        }
        else
        {
            const auto report = hardware::check (song, session.deviceLibrary(), &session.arrangingPreferences(), melodyId);
            devices.clear();
            for (const auto& d : report.devices)
            {
                found << "• " << utf8 (d.name) << ": up to " << d.peakVoices << " notes at once on " << (d.channels.size() == 1 ? "channel " : "channels ");
                for (std::size_t i = 0; i < d.channels.size(); ++i)
                    found << (i == 0 ? "" : ", ") << d.channels[i];
                found << ".\n";
                for (const auto& p : d.problems)
                    found << "   " << utf8 (p) << "\n";
                if (d.key == "soundfont")
                    continue;
                // The numbers of the synth, to correct.
                auto row = std::make_unique<DeviceRow>();
                row->profileId = d.profileId;
                row->name.setText (utf8 (d.name) + (d.capabilities.source.empty() ? "" : "  (" + utf8 (d.capabilities.source) + ")"), juce::dontSendNotification);
                row->voicesLabel.setText ("Voices", juce::dontSendNotification);
                row->partsLabel.setText ("Parts", juce::dontSendNotification);
                row->rangeLabel.setText ("Notes", juce::dontSendNotification);
                row->voices.setText (juce::String (d.capabilities.polyphony), false);
                row->parts.setText (juce::String (d.capabilities.parts), false);
                row->lowest.setText (juce::String (d.capabilities.lowestNote), false);
                row->highest.setText (juce::String (d.capabilities.highestNote), false);
                for (auto* l : { &row->name, &row->voicesLabel, &row->partsLabel, &row->rangeLabel })
                {
                    l->setColour (juce::Label::textColourId, currentTheme().colour (l == &row->name ? "text" : "textMuted"));
                    list.addAndMakeVisible (*l);
                }
                for (auto* e : { &row->voices, &row->parts, &row->lowest, &row->highest })
                {
                    styleNumber (*e);
                    list.addAndMakeVisible (*e);
                }
                devices.push_back (std::move (row));
            }
            if (report.devices.empty())
                found << "• No track plays on a synth or the SoundFont yet.\n";
            saveButton.setVisible (! devices.empty());
            proposals = report.proposals;
        }
    }
    catch (const std::exception& e)
    {
        setStatus (utf8 (e.what()));
    }
    if (proposals.empty() && status.getText().isEmpty())
        setStatus (doctor() ? "Nothing to propose for this goal. Try another goal or other bars." : "The song fits your synths.");
    findings->text = found.trimEnd();

    cards.clear();
    for (std::size_t i = 0; i < proposals.size(); ++i)
    {
        auto card = std::make_unique<ProposalCard> (song, proposals[i]);
        card->onPlay = [this, i] (int option) { play (i, option); };
        card->onUse = [this, i] (int option) { use (i, option); };
        card->onDismiss = [this, i] { dismiss (i); };
        list.addAndMakeVisible (*card);
        cards.push_back (std::move (card));
    }
    layoutList();
}

void ArrangeView::choose (const juce::String& id, int first, int last)
{
    if (const auto g = arranging::parseGoal (id.toStdString()))
        goal.setSelectedId (static_cast<int> (*g) + 1, juce::dontSendNotification);
    fromBar.setText (first > 0 ? juce::String (first) : juce::String(), false);
    toBar.setText (last > 0 ? juce::String (last) : juce::String(), false);
    check();
}

void ArrangeView::play (std::size_t index, int option)
{
    if (index >= proposals.size())
        return;
    const auto& p = proposals[index];
    const auto& song = session.song();
    if (! arranging::isCurrent (song, p))
    {
        setStatus ("The song changed. Check again.");
        return;
    }
    // Loop the proposal's bars (at most 8).
    const Tick bar = std::max<Tick> (1, song.timeSignature.ticksPerBar());
    const Tick from = p.from / bar * bar;
    const Tick to = std::min (std::max (p.to, from + bar), from + bar * 8);
    session.audition (option < 0 ? song : arranging::withOption (song, p.options[static_cast<std::size_t> (option)]), from, to);
    if (playingProposal >= 0 && playingProposal < static_cast<int> (cards.size()))
        cards[static_cast<std::size_t> (playingProposal)]->setPlaying (-2);
    playingProposal = static_cast<int> (index);
    playingOption = option;
    cards[index]->setPlaying (option);
}

void ArrangeView::stopPlaying()
{
    if (session.isAuditioning())
        session.audition (std::nullopt, playingProposal >= 0 && playingProposal < static_cast<int> (proposals.size()) ? proposals[static_cast<std::size_t> (playingProposal)].from : 0);
    if (playingProposal >= 0 && playingProposal < static_cast<int> (cards.size()))
        cards[static_cast<std::size_t> (playingProposal)]->setPlaying (-2);
    playingProposal = -1;
    playingOption = -2;
}

void ArrangeView::use (std::size_t index, int option)
{
    if (index >= proposals.size())
        return;
    stopPlaying();
    const auto p = proposals[index];
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
    session.rememberArrangingChoice (p, option);
    check();
    setStatus ("Done: " + utf8 (o.label) + ". Undo takes it back.");
}

void ArrangeView::dismiss (std::size_t index)
{
    if (index >= proposals.size())
        return;
    stopPlaying();
    session.rememberArrangingChoice (proposals[index], -1);
    proposals.erase (proposals.begin() + static_cast<std::ptrdiff_t> (index));
    cards.erase (cards.begin() + static_cast<std::ptrdiff_t> (index));
    // The cards after it moved up: their callbacks must use the new numbers.
    for (std::size_t i = 0; i < cards.size(); ++i)
    {
        cards[i]->onPlay = [this, i] (int option) { play (i, option); };
        cards[i]->onUse = [this, i] (int option) { use (i, option); };
        cards[i]->onDismiss = [this, i] { dismiss (i); };
    }
    layoutList();
}

void ArrangeView::saveCapabilities()
{
    for (const auto& row : devices)
    {
        const auto* profile = session.deviceLibrary().byId (row->profileId);
        DeviceCapabilities c = profile != nullptr ? profile->capabilities : DeviceCapabilities {};
        c.polyphony = std::clamp (row->voices.getText().getIntValue(), 0, 4096);
        c.parts = std::clamp (row->parts.getText().getIntValue(), 0, 64);
        c.lowestNote = std::clamp (row->lowest.getText().getIntValue(), 0, 127);
        c.highestNote = std::clamp (row->highest.getText().getIntValue(), 0, 127);
        if (c.lowestNote > c.highestNote)
            std::swap (c.lowestNote, c.highestNote);
        c.source = "your numbers";
        if (profile == nullptr || ! (c.polyphony == profile->capabilities.polyphony && c.parts == profile->capabilities.parts
                                     && c.lowestNote == profile->capabilities.lowestNote && c.highestNote == profile->capabilities.highestNote))
            session.setDeviceCapabilities (row->profileId, c);
    }
    check();
    setStatus ("Saved. The check uses your numbers now.");
}

void ArrangeView::paint (juce::Graphics& g)
{
    g.fillAll (currentTheme().colour ("panel"));
}

void ArrangeView::layoutList()
{
    const int width = std::max (200, viewport.getWidth() - viewport.getScrollBarThickness() - 2);
    int y = 0;
    findings->setBounds (0, y, width, findings->heightFor (width));
    y = findings->getBottom() + 10;
    for (auto& row : devices)
    {
        row->name.setBounds (0, y, width, 22);
        y += 24;
        juce::Rectangle<int> line (0, y, width, 24);
        row->voicesLabel.setBounds (line.removeFromLeft (48));
        row->voices.setBounds (line.removeFromLeft (44));
        line.removeFromLeft (8);
        row->partsLabel.setBounds (line.removeFromLeft (42));
        row->parts.setBounds (line.removeFromLeft (36));
        line.removeFromLeft (8);
        row->rangeLabel.setBounds (line.removeFromLeft (46));
        row->lowest.setBounds (line.removeFromLeft (38));
        line.removeFromLeft (4);
        row->highest.setBounds (line.removeFromLeft (38));
        y += 30;
    }
    if (saveButton.isVisible())
    {
        saveButton.setBounds (0, y, 120, 26);
        y += 38;
    }
    for (auto& card : cards)
    {
        const int h = card->heightFor (width);
        card->setBounds (0, y, width, h);
        y += h + 10;
    }
    list.setSize (width, std::max (y, 10));
}

void ArrangeView::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    title.setBounds (r.removeFromTop (24));
    const int aboutHeight = static_cast<int> (textHeight (about.getText(), about.getFont(), r.getWidth())) + 6;
    about.setBounds (r.removeFromTop (aboutHeight));
    r.removeFromTop (6);
    auto row = [&] (juce::Label& label) {
        auto line = r.removeFromTop (26);
        r.removeFromTop (6);
        label.setBounds (line.removeFromLeft (60));
        return line;
    };
    if (doctor())
    {
        goal.setBounds (row (goalLabel));
        auto line = row (barsLabel);
        fromBar.setBounds (line.removeFromLeft (50));
        toLabel.setBounds (line.removeFromLeft (30));
        toBar.setBounds (line.removeFromLeft (50));
    }
    melody.setBounds (row (melodyLabel));
    auto buttons = r.removeFromTop (28);
    checkButton.setBounds (buttons.removeFromLeft (110));
    buttons.removeFromLeft (8);
    stopButton.setBounds (buttons.removeFromLeft (70));
    r.removeFromTop (6);
    status.setBounds (r.removeFromTop (status.getText().isEmpty() ? 0 : 36));
    viewport.setBounds (r);
    layoutList();
}

} // namespace arranger::ui

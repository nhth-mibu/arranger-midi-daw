#include "SongFormView.h"
#include "ProposalCard.h"

namespace arranger::ui
{

namespace
{
    juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    juce::String timeText (double seconds)
    {
        const int s = static_cast<int> (std::lround (seconds));
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }

    constexpr Transition kChoices[] = { Transition::automatic, Transition::fill, Transition::pickup, Transition::hold, Transition::stop, Transition::none };
}

// ---- one form ------------------------------------------------------------------------
class SongFormView::FormCard final : public juce::Component
{
public:
    std::function<void (int join)> onPlay; ///< join 0: the whole form
    std::function<void (int join, Transition)> onTransition;
    std::function<void (Transition)> onEnding;
    std::function<void()> onUse, onRemove;

    FormCard (const Song& song, const SongForm& f, const std::vector<SongSection>& sections, const DeviceLibrary& devices) : form (f)
    {
        const Tick bar = std::max<Tick> (1, song.timeSignature.ticksPerBar());
        order = utf8 (form::describe (f));
        try
        {
            const auto r = form::render (song, f, sections, &devices);
            meta = juce::String (static_cast<int> (r.length / bar)) + " bars  ·  " + timeText (r.song.tempo.secondsAt (static_cast<double> (r.length)))
                   + "  ·  the song: " + juce::String (static_cast<int> (song.contentEnd() / bar)) + " bars";
            for (const auto& n : r.notes)
                notes << utf8 (n) << "\n";
            notes = notes.trimEnd();
            for (std::size_t i = 0; i < r.boundaries.size(); ++i)
            {
                const auto& b = r.boundaries[i];
                auto row = std::make_unique<JoinRow>();
                row->text = "Bar " + juce::String (static_cast<int> (b.at / bar) + 1) + "  " + utf8 (b.from) + juce::String::fromUTF8 (" \xE2\x86\x92 ") + utf8 (b.to);
                row->why = utf8 (b.natural ? "As in the original." : "Recommended: " + form::transitionName (b.recommended) + ". " + b.why);
                row->natural = b.natural;
                const auto chosen = f.parts[static_cast<std::size_t> (b.part)].into;
                int id = 1;
                for (auto k : kChoices)
                {
                    if (k != Transition::automatic && k != Transition::none && std::find (b.options.begin(), b.options.end(), k) == b.options.end())
                    {
                        ++id;
                        continue;
                    }
                    const auto name = k == Transition::automatic ? "Automatic (" + form::transitionName (b.recommended) + ")" : form::transitionName (k);
                    row->choice.addItem (utf8 (name), id);
                    if (k == chosen)
                        row->choice.setSelectedId (id, juce::dontSendNotification);
                    ++id;
                }
                const int join = static_cast<int> (i) + 1;
                row->choice.onChange = [this, join, r = row.get()] {
                    if (onTransition)
                        onTransition (join, kChoices[r->choice.getSelectedId() - 1]);
                };
                row->choice.setTooltip ("How this part begins after the part before it");
                row->play.setButtonText ("Play");
                row->play.setTooltip ("Play two bars on each side of this join, looping. Change the transition while it plays to compare.");
                row->play.onClick = [this, join] { if (onPlay) onPlay (join); };
                addAndMakeVisible (row->choice);
                addAndMakeVisible (row->play);
                joins.push_back (std::move (row));
            }
            ending.addItem ("Ending: automatic (" + utf8 (form::transitionName (r.ending == Transition::hold ? Transition::hold : Transition::none)) + ")", 1);
            ending.addItem ("Ending: final chord", 2);
            ending.addItem ("Ending: stop with the last part", 3);
            ending.setSelectedId (f.ending == Transition::hold ? 2 : (f.ending == Transition::none ? 3 : 1), juce::dontSendNotification);
            ending.onChange = [this] {
                if (onEnding)
                    onEnding (ending.getSelectedId() == 2 ? Transition::hold : (ending.getSelectedId() == 3 ? Transition::none : Transition::automatic));
            };
            addAndMakeVisible (ending);
            playable = true;
        }
        catch (const std::exception& e)
        {
            notes = utf8 (e.what());
        }
        playForm.setTooltip ("Play the whole form from its start. The song does not change.");
        useForm.setTooltip ("Write this form onto the Timeline (one undo step)");
        removeForm.setTooltip ("Remove this form from the song");
        playForm.onClick = [this] { if (onPlay) onPlay (0); };
        useForm.onClick = [this] { if (onUse) onUse(); };
        removeForm.onClick = [this] { if (onRemove) onRemove(); };
        playForm.setEnabled (playable);
        useForm.setEnabled (playable);
        for (auto* b : { &playForm, &useForm, &removeForm })
            addAndMakeVisible (*b);
    }

    void setPlaying (int join)
    {
        playing = join;
        repaint();
    }

    int heightFor (int width) const
    {
        const auto& t = currentTheme();
        const int w = width - kPad * 2;
        float h = kPad + 20 + textHeight (order, t.font (12.0f), w) + 4 + 16;
        if (notes.isNotEmpty())
            h += 4 + textHeight (notes, t.font (11.5f), w);
        for (const auto& j : joins)
            h += 10 + 18 + 26 + (j->natural ? 0 : textHeight (j->why, t.font (11.5f), w));
        if (playable)
            h += 10 + 26;
        h += 10 + 26 + kPad;
        return static_cast<int> (h);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = currentTheme();
        const auto box = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (t.colour ("panelRaised"));
        g.fillRoundedRectangle (box, 8.0f);
        g.setColour (playing >= 0 ? t.colour ("accent") : t.colour ("line"));
        g.drawRoundedRectangle (box, 8.0f, playing >= 0 ? 1.6f : 1.0f);

        const int w = getWidth() - kPad * 2;
        float y = kPad;
        g.setColour (t.colour ("text"));
        g.setFont (t.font (13.5f, true));
        g.drawText (utf8 (form.name), kPad, static_cast<int> (y), w, 20, juce::Justification::centredLeft, true);
        y += 20;
        const auto block = [&] (const juce::String& text, const juce::Font& font, juce::Colour colour) {
            const float h = textHeight (text, font, w);
            drawText (g, text, font, colour, { static_cast<float> (kPad), y, static_cast<float> (w), h });
            y += h;
        };
        block (order, t.font (12.0f), t.colour ("text"));
        y += 4;
        g.setColour (t.colour ("textDim"));
        g.setFont (t.font (11.0f));
        g.drawText (meta, kPad, static_cast<int> (y), w, 16, juce::Justification::centredLeft, true);
        y += 16;
        if (notes.isNotEmpty())
        {
            y += 4;
            block (notes, t.font (11.5f), t.colour ("textMuted"));
        }
        for (std::size_t i = 0; i < joins.size(); ++i)
        {
            const auto& j = *joins[i];
            y += 10;
            g.setColour (static_cast<int> (i) + 1 == playing ? t.colour ("accent") : t.colour (j.natural ? "textMuted" : "text"));
            g.setFont (t.font (12.0f, ! j.natural));
            g.drawText (j.text, kPad, static_cast<int> (y), w, 18, juce::Justification::centredLeft, true);
            y += 18 + 26;
            if (! j.natural)
                block (j.why, t.font (11.5f), t.colour ("textMuted"));
        }
        if (playing == 0)
        {
            g.setColour (t.colour ("accent"));
            g.setFont (t.font (11.0f, true));
            g.drawText ("Playing the form", kPad, getHeight() - kPad - 26, w, 26, juce::Justification::centredRight);
        }
    }

    void resized() override
    {
        const auto& t = currentTheme();
        const int w = getWidth() - kPad * 2;
        float y = kPad + 20 + textHeight (order, t.font (12.0f), w) + 4 + 16;
        if (notes.isNotEmpty())
            y += 4 + textHeight (notes, t.font (11.5f), w);
        for (auto& j : joins)
        {
            y += 10 + 18;
            juce::Rectangle<int> line (kPad, static_cast<int> (y), w, 24);
            j->play.setBounds (line.removeFromRight (56));
            line.removeFromRight (6);
            j->choice.setBounds (line);
            y += 26 + (j->natural ? 0 : textHeight (j->why, t.font (11.5f), w));
        }
        if (playable)
        {
            y += 10;
            ending.setBounds (kPad, static_cast<int> (y), w, 24);
            y += 26;
        }
        juce::Rectangle<int> bottom (kPad, getHeight() - kPad - 24, w, 24);
        playForm.setBounds (bottom.removeFromLeft (96));
        bottom.removeFromLeft (6);
        useForm.setBounds (bottom.removeFromLeft (60));
        bottom.removeFromLeft (6);
        removeForm.setBounds (bottom.removeFromLeft (76));
    }

private:
    struct JoinRow
    {
        juce::String text, why;
        bool natural = false;
        juce::ComboBox choice;
        juce::TextButton play;
    };
    SongForm form;
    juce::String order, meta, notes;
    std::vector<std::unique_ptr<JoinRow>> joins;
    juce::ComboBox ending;
    juce::TextButton playForm { "Play Form" }, useForm { "Use" }, removeForm { "Remove" };
    bool playable = false;
    int playing = -1;
};

// ---- the view ----------------------------------------------------------------------
SongFormView::SongFormView (Session& s) : session (s)
{
    const auto& theme = currentTheme();
    for (auto* l : { &title, &about, &sectionsLabel, &makeLabel, &status })
    {
        l->setColour (juce::Label::textColourId, theme.colour (l == &title ? "text" : "textMuted"));
        l->setJustificationType (juce::Justification::topLeft);
        l->setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (*l);
    }
    title.setText ("Song Forms", juce::dontSendNotification);
    title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    about.setText ("Other versions of the song from its sections. Play them first: the Timeline changes only when you use one.",
                   juce::dontSendNotification);
    sectionsLabel.setText ("Sections (name and bars)", juce::dontSendNotification);
    makeLabel.setText ("New form", juce::dontSendNotification);

    sectionText.setMultiLine (true, false);
    sectionText.setReturnKeyStartsNewLine (true);
    sectionText.setTooltip ("One section in each line: a name and its bars, like \"Chorus 1: 13-20\". Names must differ.");
    orderText.setTextToShowWhenEmpty ("Intro, Verse 1, Chorus 1 x2, Verse 2 (no melody), Outro", theme.colour ("textDim"));
    orderText.setTooltip ("Section names in order. x2 repeats a section; (no melody) rests a track or a role in it.");
    orderText.onReturnKey = [this] { makeFromBox(); };
    int id = 1;
    for (auto p : form::kPresets)
        preset.addItem (utf8 (form::presetName (p)), id++);
    preset.addItem ("My order (type it below)", id);
    preset.setSelectedId (1, juce::dontSendNotification);
    preset.onChange = [this] { resized(); };

    saveButton.setTooltip ("Keep these sections in the song");
    findButton.setTooltip ("Find the sections from the music again (the saved sections are removed)");
    makeButton.setTooltip ("Make the form. It is kept in the song; the Timeline does not change.");
    stopButton.setTooltip ("Stop the preview: the song plays as it is");
    saveButton.onClick = [this] { saveSections(); };
    findButton.onClick = [this] { findAgain(); };
    makeButton.onClick = [this] { makeFromBox(); };
    stopButton.onClick = [this] { stopPlaying(); };
    for (auto* c : std::initializer_list<juce::Component*> { &sectionText, &orderText, &preset, &saveButton, &findButton, &makeButton, &stopButton })
        addAndMakeVisible (*c);

    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    refresh();
    startTimerHz (4);
}

SongFormView::~SongFormView()
{
    stopTimer();
    stopPlaying();
}

std::vector<SongSection> SongFormView::sections() const
{
    // Recordings count by how loud they play.
    return form::sectionsOf (session.song(), &session.deviceLibrary(), [this] (const std::string& file) { return session.sampleFor (file); });
}

void SongFormView::setStatus (const juce::String& text)
{
    status.setText (text, juce::dontSendNotification);
    if (! getLocalBounds().isEmpty())
        resized();
}

void SongFormView::timerCallback()
{
    // Another edit (Undo, the AI chat) changed the song: show it again.
    if (session.doc().revision() != shownRevision)
        refresh();
}

void SongFormView::refresh()
{
    shownRevision = session.doc().revision();
    const auto& song = session.song();
    const auto secs = sections();
    if (! sectionText.hasKeyboardFocus (true))
        sectionText.setText (utf8 (form::sectionsText (song, secs)), false);
    sectionsLabel.setText (song.sections.empty() ? (secs.empty() ? "Sections: none found" : "Sections (found from the music; correct and save)")
                                                 : "Sections (name and bars)",
                           juce::dontSendNotification);

    cards.clear();
    for (std::size_t i = 0; i < song.forms.size(); ++i)
    {
        auto card = std::make_unique<FormCard> (song, song.forms[i], secs, session.deviceLibrary());
        const int index = static_cast<int> (i);
        card->onPlay = [this, index] (int join) { play (index, join); };
        card->onTransition = [this, index] (int join, Transition t) { setTransition (index, join, t); };
        card->onEnding = [this, index] (Transition t) { setEnding (index, t); };
        card->onUse = [this, index] { use (index); };
        card->onRemove = [this, index] { remove (index); };
        list.addAndMakeVisible (*card);
        cards.push_back (std::move (card));
    }
    if (playingForm >= 0 && playingForm < static_cast<int> (cards.size()) && session.isAuditioning())
        cards[static_cast<std::size_t> (playingForm)]->setPlaying (playingJoin);
    layoutList();
}

void SongFormView::saveSections()
{
    try
    {
        const auto secs = form::parseSections (session.song(), sectionText.getText().toStdString());
        stopPlaying();
        session.edit ("Set Song Sections", [&] (Song& s) { s.sections = secs; });
        refresh();
        setStatus ("Saved " + juce::String (static_cast<int> (secs.size())) + " sections.");
    }
    catch (const std::exception& e)
    {
        setStatus (utf8 (e.what()));
    }
}

void SongFormView::findAgain()
{
    stopPlaying();
    if (! session.song().sections.empty())
        session.edit ("Find Song Sections", [] (Song& s) { s.sections.clear(); });
    sectionText.setText ({}, false);
    refresh();
    setStatus ("Found from the music. Correct the names or bars, then save.");
}

void SongFormView::makePreset (const juce::String& presetId)
{
    if (const auto p = form::parsePreset (presetId.toStdString()))
    {
        preset.setSelectedId (static_cast<int> (*p) + 1, juce::dontSendNotification);
        makeFromBox();
    }
}

void SongFormView::makeFromBox()
{
    const auto& song = session.song();
    const auto secs = sections();
    SongForm f;
    try
    {
        const int chosen = preset.getSelectedId() - 1;
        if (chosen >= 0 && chosen < static_cast<int> (std::size (form::kPresets)))
            f = form::makePreset (song, secs, form::kPresets[chosen], &session.deviceLibrary());
        else
            f = form::parseForm (song, secs, "My Form " + std::to_string (song.forms.size() + 1), orderText.getText().toStdString());
    }
    catch (const std::exception& e)
    {
        setStatus (utf8 (e.what()));
        return;
    }
    stopPlaying();
    const bool keepSections = song.sections.empty();
    session.edit ("New Song Form: " + f.name, [&] (Song& s) {
        if (keepSections)
            s.sections = secs;
        form::putForm (s, f);
    });
    refresh();
    setStatus ("Made \"" + utf8 (f.name) + "\". Play it, change the joins you want, then use it.");
}

void SongFormView::play (int index, int join)
{
    const auto& song = session.song();
    if (index < 0 || index >= static_cast<int> (song.forms.size()))
        return;
    try
    {
        const auto r = form::render (song, song.forms[static_cast<std::size_t> (index)], sections(), &session.deviceLibrary());
        const Tick bar = std::max<Tick> (1, song.timeSignature.ticksPerBar());
        Tick from = 0, to = r.length;
        if (join >= 1 && join <= static_cast<int> (r.boundaries.size()))
        {
            const Tick at = r.boundaries[static_cast<std::size_t> (join - 1)].at;
            from = std::max<Tick> (0, at - bar * 2);
            to = std::min (r.length, at + bar * 2);
        }
        session.audition (r.song, from, to);
    }
    catch (const std::exception& e)
    {
        setStatus (utf8 (e.what()));
        return;
    }
    if (playingForm >= 0 && playingForm < static_cast<int> (cards.size()))
        cards[static_cast<std::size_t> (playingForm)]->setPlaying (-1);
    playingForm = index;
    playingJoin = join;
    if (index < static_cast<int> (cards.size()))
        cards[static_cast<std::size_t> (index)]->setPlaying (join);
}

void SongFormView::stopPlaying()
{
    if (session.isAuditioning())
        session.audition (std::nullopt, 0);
    if (playingForm >= 0 && playingForm < static_cast<int> (cards.size()))
        cards[static_cast<std::size_t> (playingForm)]->setPlaying (-1);
    playingForm = playingJoin = -1;
}

void SongFormView::setTransition (int index, int join, Transition t)
{
    const auto& song = session.song();
    if (index < 0 || index >= static_cast<int> (song.forms.size()))
        return;
    auto f = song.forms[static_cast<std::size_t> (index)];
    if (join < 1 || join >= static_cast<int> (f.parts.size()))
        return;
    f.parts[static_cast<std::size_t> (join)].into = t;
    const bool wasPlaying = session.isAuditioning() && playingForm == index;
    const int playedJoin = playingJoin;
    // A song edit ends a preview; play again at once, so the change can be heard in place.
    session.edit ("Song Form Transition", [&] (Song& s) { form::putForm (s, f); });
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SongFormView> (this), index, wasPlaying, playedJoin] {
        if (safe == nullptr)
            return;
        safe->refresh();
        if (wasPlaying)
            safe->play (index, playedJoin);
    });
}

void SongFormView::setEnding (int index, Transition t)
{
    const auto& song = session.song();
    if (index < 0 || index >= static_cast<int> (song.forms.size()))
        return;
    auto f = song.forms[static_cast<std::size_t> (index)];
    f.ending = t;
    session.edit ("Song Form Ending", [&] (Song& s) { form::putForm (s, f); });
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SongFormView> (this)] {
        if (safe != nullptr)
            safe->refresh();
    });
}

void SongFormView::use (int index)
{
    const auto& song = session.song();
    if (index < 0 || index >= static_cast<int> (song.forms.size()))
        return;
    stopPlaying();
    const auto f = song.forms[static_cast<std::size_t> (index)];
    try
    {
        const auto next = form::use (song, f, sections(), &session.deviceLibrary());
        session.edit ("Use Song Form: " + f.name, [&] (Song& s) { s = next; });
    }
    catch (const std::exception& e)
    {
        setStatus (utf8 (e.what()));
        return;
    }
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SongFormView> (this), name = utf8 (f.name)] {
        if (safe == nullptr)
            return;
        safe->refresh();
        safe->setStatus ("The Timeline is now \"" + name + "\". Undo brings the original and its forms back.");
    });
}

void SongFormView::remove (int index)
{
    const auto& song = session.song();
    if (index < 0 || index >= static_cast<int> (song.forms.size()))
        return;
    stopPlaying();
    const auto name = song.forms[static_cast<std::size_t> (index)].name;
    session.edit ("Remove Song Form", [&] (Song& s) {
        s.forms.erase (std::remove_if (s.forms.begin(), s.forms.end(), [&] (const SongForm& g) { return g.name == name; }), s.forms.end());
    });
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SongFormView> (this)] {
        if (safe != nullptr)
            safe->refresh();
    });
}

void SongFormView::paint (juce::Graphics& g)
{
    g.fillAll (currentTheme().colour ("panel"));
}

void SongFormView::layoutList()
{
    const int width = std::max (200, viewport.getWidth() - viewport.getScrollBarThickness() - 2);
    int y = 0;
    for (auto& card : cards)
    {
        const int h = card->heightFor (width);
        card->setBounds (0, y, width, h);
        y += h + 10;
    }
    list.setSize (width, std::max (y, 10));
}

void SongFormView::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    title.setBounds (r.removeFromTop (24));
    const int aboutHeight = static_cast<int> (textHeight (about.getText(), about.getFont(), r.getWidth())) + 6;
    about.setBounds (r.removeFromTop (aboutHeight));
    r.removeFromTop (6);
    sectionsLabel.setBounds (r.removeFromTop (20));
    const int lines = std::clamp (sectionText.getText().isEmpty() ? 3 : juce::StringArray::fromLines (sectionText.getText().trimEnd()).size(), 3, 5);
    sectionText.setBounds (r.removeFromTop (lines * 17 + 10));
    r.removeFromTop (6);
    {
        auto line = r.removeFromTop (26);
        saveButton.setBounds (line.removeFromLeft (110));
        line.removeFromLeft (8);
        findButton.setBounds (line.removeFromLeft (96));
    }
    r.removeFromTop (12);
    makeLabel.setBounds (r.removeFromTop (20));
    {
        auto line = r.removeFromTop (26);
        stopButton.setBounds (line.removeFromRight (60));
        line.removeFromRight (6);
        makeButton.setBounds (line.removeFromRight (64));
        line.removeFromRight (6);
        preset.setBounds (line);
    }
    const bool custom = preset.getSelectedId() == static_cast<int> (std::size (form::kPresets)) + 1;
    orderText.setVisible (custom);
    if (custom)
    {
        r.removeFromTop (6);
        orderText.setBounds (r.removeFromTop (26));
    }
    r.removeFromTop (6);
    status.setBounds (r.removeFromTop (status.getText().isEmpty() ? 0 : 36));
    viewport.setBounds (r);
    layoutList();
}

} // namespace arranger::ui

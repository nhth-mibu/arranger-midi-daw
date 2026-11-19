#include "SongFormsView.h"
#include "ProposalCard.h"

namespace arranger::ui
{

namespace
{
    juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
}

SongFormsView::SongFormsView (Session& s) : session (s), tools (std::make_unique<SessionTools> (s))
{
    const auto& theme = currentTheme();
    for (auto* l : { &title, &about, &sectionsLabel, &formLabel, &nameLabel, &partsLabel, &status })
    {
        l->setColour (juce::Label::textColourId, theme.colour (l == &title ? "text" : "textMuted"));
        l->setJustificationType (juce::Justification::topLeft);
        l->setMinimumHorizontalScale (1.0f);
        l->setFont (theme.font (12.5f));
        l->setBorderSize ({ 0, 0, 0, 0 });
        addAndMakeVisible (*l);
    }
    title.setText ("Song Forms", juce::dontSendNotification);
    title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    about.setText ("Other arrangements of the song from its sections: a short version, a long intro. New joins get a transition that fits both sides. "
                   "Play a form without changing the Timeline; Use This Form changes it.",
                   juce::dontSendNotification);
    sectionsLabel.setText ("Sections (name: bars)", juce::dontSendNotification);
    formLabel.setText ("Form", juce::dontSendNotification);
    nameLabel.setText ("Name", juce::dontSendNotification);
    partsLabel.setText ("Parts", juce::dontSendNotification);
    for (auto* l : { &formLabel, &nameLabel, &partsLabel })
        l->setJustificationType (juce::Justification::centredLeft);

    sections.setMultiLine (true, false);
    sections.setReturnKeyStartsNewLine (true);
    sections.setTextToShowWhenEmpty ("Intro: 1-4\nVerse 1: 5-12\nChorus 1: 13-20", theme.colour ("textDim"));
    name.setTextToShowWhenEmpty ("Short version", theme.colour ("textDim"));
    parts.setTextToShowWhenEmpty ("Intro, Verse 1, Chorus 1 x2, Ending", theme.colour ("textDim"));
    parts.onReturnKey = [this] { saveForm(); };

    findButton.setTooltip ("Find the sections from the music (phrases that play alike) and name them");
    setButton.setTooltip ("Use the sections as written above (one undo step)");
    newButton.setTooltip ("Start a new form");
    saveButton.setTooltip ("Keep the form. The Timeline does not change. New joins get the suggested transition.");
    playButton.setTooltip ("Play the form from the start, looping. The Timeline does not change.");
    useButton.setTooltip ("Make the Timeline this form, with its transitions (one undo step)");
    findButton.onClick = [this] { findSections(); };
    setButton.onClick = [this] { saveSections(); };
    newButton.onClick = [this] {
        formName.clear();
        forms.setSelectedId (0, juce::dontSendNotification);
        name.setText ({}, false);
        parts.setText ({}, false);
        refreshJoins();
    };
    saveButton.onClick = [this] { saveForm(); };
    playButton.onClick = [this] { playForm (0); };
    stopButton.onClick = [this] { stopPlaying(); };
    useButton.onClick = [this] { useForm(); };
    forms.onChange = [this] { if (! updating) selectForm(); };
    forms.setTextWhenNothingSelected ("(new form)");

    for (auto* c : std::initializer_list<juce::Component*> { &sections, &name, &parts, &forms, &findButton, &setButton, &newButton, &saveButton, &playButton, &stopButton, &useButton })
        addAndMakeVisible (*c);
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    session.addChangeListener (this);
    refresh();
}

SongFormsView::~SongFormsView()
{
    session.removeChangeListener (this);
    stopPlaying();
}

void SongFormsView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (session.doc().revision() != seenRevision)
        refresh();
}

const SongForm* SongFormsView::form() const
{
    for (const auto& f : session.song().forms)
        if (f.name == formName)
            return &f;
    return nullptr;
}

void SongFormsView::setStatus (const juce::String& text)
{
    status.setText (text, juce::dontSendNotification);
    resized();
}

juce::String SongFormsView::call (const std::string& tool, const nlohmann::json& args)
{
    try
    {
        tools->server().call (tool, args.dump());
        return {};
    }
    catch (const std::exception& e)
    {
        return utf8 (e.what());
    }
}

void SongFormsView::refresh()
{
    seenRevision = session.doc().revision();
    const auto& song = session.song();
    const Tick bar = std::max<Tick> (1, song.timeSignature.ticksPerBar());
    if (! sections.hasKeyboardFocus (true))
    {
        juce::String text;
        for (const auto& sec : song.sections)
            text << utf8 (sec.name) << ": " << (sec.start / bar + 1) << "-" << ((sec.end + bar - 1) / bar) << "\n";
        sections.setText (text.trimEnd(), false);
    }
    updating = true;
    forms.clear (juce::dontSendNotification);
    int pick = 0;
    for (std::size_t i = 0; i < song.forms.size(); ++i)
    {
        forms.addItem (utf8 (song.forms[i].name), static_cast<int> (i) + 1);
        if (song.forms[i].name == formName)
            pick = static_cast<int> (i) + 1;
    }
    if (pick == 0 && ! song.forms.empty() && formName.empty() && name.getText().isEmpty())
    {
        pick = 1;
        formName = song.forms.front().name;
    }
    forms.setSelectedId (pick, juce::dontSendNotification);
    updating = false;
    if (const auto* f = form(); f != nullptr && ! parts.hasKeyboardFocus (true))
    {
        name.setText (utf8 (f->name), false);
        parts.setText (utf8 (song_form::partsText (song, *f)), false);
    }
    const bool hasSections = ! song.sections.empty();
    for (auto* c : std::initializer_list<juce::Component*> { &saveButton, &parts, &name })
        c->setEnabled (hasSections);
    if (! hasSections && status.getText().isEmpty())
        status.setText ("No sections yet. Press Find Sections, or write them above and press Set Sections.", juce::dontSendNotification);
    refreshJoins();
}

void SongFormsView::refreshJoins()
{
    joins.clear();
    const auto& song = session.song();
    const auto* f = form();
    const bool ok = f != nullptr && song_form::problemWith (song, *f).empty();
    playButton.setEnabled (ok);
    useButton.setEnabled (ok);
    if (ok)
    {
        const Tick bar = std::max<Tick> (1, song.timeSignature.ticksPerBar());
        for (const auto& b : song_form::boundaries (song, *f, &session.deviceLibrary()))
        {
            auto row = std::make_unique<JoinRow>();
            const auto* a = song_form::findSection (song, b.from);
            const auto* c = song_form::findSection (song, b.to);
            row->heading.setText ("Bar " + juce::String (b.at / bar + 1) + ":  " + utf8 (a->name) + "  to  " + utf8 (c->name) + (b.isNew ? "  (new join)" : ""),
                                  juce::dontSendNotification);
            row->heading.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            row->heading.setColour (juce::Label::textColourId, currentTheme().colour (b.isNew ? "text" : "textMuted"));
            row->because.setText (utf8 (b.because), juce::dontSendNotification);
            row->because.setColour (juce::Label::textColourId, currentTheme().colour ("textMuted"));
            row->because.setJustificationType (juce::Justification::topLeft);
            row->because.setMinimumHorizontalScale (1.0f);
            row->because.setFont (currentTheme().font (12.0f));
            row->because.setBorderSize ({ 0, 0, 0, 0 });
            const auto now = f->transitions[static_cast<std::size_t> (b.index)].kind;
            int id = 1;
            for (auto k : b.kinds)
            {
                row->kind.addItem (utf8 (song_form::kindName (k)) + (k == b.suggested && k != Transition::Kind::none ? "  (suggested)" : ""), static_cast<int> (k) + 1);
                if (k == now)
                    id = static_cast<int> (k) + 1;
            }
            row->kind.setSelectedId (id, juce::dontSendNotification);
            const int join = b.index + 1;
            row->kind.onChange = [this, join, box = &row->kind] { setTransition (join, static_cast<Transition::Kind> (box->getSelectedId() - 1)); };
            row->play.setTooltip ("Play two bars before and after this join, looping");
            row->play.onClick = [this, join] { playForm (join); };
            for (auto* comp : std::initializer_list<juce::Component*> { &row->heading, &row->because, &row->kind, &row->play })
                list.addAndMakeVisible (*comp);
            joins.push_back (std::move (row));
        }
    }
    layoutList();
}

void SongFormsView::layoutList()
{
    const int w = std::max (100, viewport.getWidth() - viewport.getScrollBarThickness() - 2);
    int y = 0;
    for (auto& row : joins)
    {
        row->heading.setBounds (0, y, w, 20);
        y += 22;
        const int h = static_cast<int> (textHeight (row->because.getText(), row->because.getFont(), w)) + 8;
        row->because.setBounds (0, y, w, h);
        y += h + 2;
        row->kind.setBounds (0, y, std::min (220, w - 70), 24);
        row->play.setBounds (std::min (220, w - 70) + 6, y, 60, 24);
        y += 34;
    }
    list.setBounds (0, 0, w, std::max (y, 1));
}

void SongFormsView::findSections()
{
    const auto error = call ("find_sections", nlohmann::json::object());
    setStatus (error.isNotEmpty() ? error : (session.song().sections.empty() ? juce::String ("The song is too short for sections.") : juce::String ("Found the sections. Rename them above if you like.")));
}

void SongFormsView::saveSections()
{
    nlohmann::json rows = nlohmann::json::array();
    for (auto line : juce::StringArray::fromLines (sections.getText()))
    {
        line = line.trim();
        if (line.isEmpty())
            continue;
        // "Name: 5-12" (or "Name 5-12")
        auto rangeText = line.fromLastOccurrenceOf (":", false, false).trim();
        auto label = line.upToLastOccurrenceOf (":", false, false).trim();
        if (! line.contains (":"))
        {
            rangeText = line.fromLastOccurrenceOf (" ", false, false);
            label = line.upToLastOccurrenceOf (" ", false, false).trim();
        }
        const int from = rangeText.upToFirstOccurrenceOf ("-", false, false).trim().getIntValue();
        const int to = rangeText.contains ("-") ? rangeText.fromFirstOccurrenceOf ("-", false, false).trim().getIntValue() : from;
        if (label.isEmpty() || from < 1 || to < from)
        {
            setStatus ("Write each section as \"Name: first bar-last bar\", for example \"Chorus 1: 13-20\". Not clear: " + line);
            return;
        }
        rows.push_back ({ { "name", label.toStdString() }, { "from_bar", from }, { "to_bar", to } });
    }
    const auto error = call ("set_sections", { { "sections", rows } });
    setStatus (error.isNotEmpty() ? error : juce::String ("Sections set."));
}

void SongFormsView::selectForm()
{
    const int i = forms.getSelectedId() - 1;
    const auto& song = session.song();
    if (i < 0 || i >= static_cast<int> (song.forms.size()))
        return;
    stopPlaying();
    formName = song.forms[static_cast<std::size_t> (i)].name;
    name.setText (utf8 (formName), false);
    parts.setText (utf8 (song_form::partsText (song, song.forms[static_cast<std::size_t> (i)])), false);
    setStatus ({});
    refreshJoins();
}

void SongFormsView::makeForm (const juce::String& formTitle, const juce::String& partNames)
{
    name.setText (formTitle, false);
    parts.setText (partNames, false);
    saveForm();
}

void SongFormsView::saveForm()
{
    auto title = name.getText().trim();
    if (title.isEmpty())
        title = "Form " + juce::String (session.song().forms.size() + 1);
    try
    {
        const auto result = nlohmann::json::parse (tools->server().call ("set_song_form", nlohmann::json ({ { "name", title.toStdString() }, { "parts", parts.getText().toStdString() } }).dump()));
        formName = result.value ("name", title.toStdString());
        juce::String text = "Saved: " + juce::String (result.value ("bars", 0)) + " bars.";
        if (result.contains ("not_sections"))
        {
            text << " Not sections:";
            for (const auto& w : result["not_sections"])
                text << " " << utf8 (w.get<std::string>());
        }
        parts.giveAwayKeyboardFocus();
        refresh();
        setStatus (text);
    }
    catch (const std::exception& e)
    {
        setStatus (utf8 (e.what()));
    }
}

void SongFormsView::setTransition (int join, Transition::Kind kind)
{
    const auto error = call ("set_transition", { { "form", formName }, { "join", join }, { "kind", song_form::kindId (kind) } });
    if (error.isNotEmpty())
        setStatus (error);
    else if (playing)
        playForm (join);
}

void SongFormsView::playForm (int join)
{
    nlohmann::json a = { { "form", formName } };
    if (join > 0)
        a["join"] = join;
    const auto error = call ("play_song_form", a);
    playing = error.isEmpty();
    setStatus (error.isNotEmpty() ? error : (join > 0 ? "Playing the bars around join " + juce::String (join) + "." : juce::String ("Playing the form. The Timeline does not change.")));
}

void SongFormsView::stopPlaying()
{
    if (! playing)
        return;
    playing = false;
    session.audition (std::nullopt);
    if (! getLocalBounds().isEmpty())
        setStatus ({});
}

void SongFormsView::useForm()
{
    stopPlaying();
    const auto error = call ("use_song_form", { { "form", formName } });
    formName.clear();
    refresh();
    setStatus (error.isNotEmpty() ? error : juce::String ("The Timeline is the form now. Undo takes it back."));
}

void SongFormsView::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    title.setBounds (r.removeFromTop (24));
    const auto block = [&] (juce::Label& l, int gap) {
        const int h = l.getText().isEmpty() ? 0 : static_cast<int> (textHeight (l.getText(), l.getFont(), r.getWidth())) + 8;
        l.setBounds (r.removeFromTop (h));
        r.removeFromTop (h > 0 ? gap : 0);
    };
    block (about, 8);
    sectionsLabel.setBounds (r.removeFromTop (18));
    const int lines = std::clamp (sections.getText().isEmpty() ? 3 : juce::StringArray::fromLines (sections.getText()).size(), 3, 5);
    sections.setBounds (r.removeFromTop (lines * 18 + 8));
    r.removeFromTop (6);
    {
        auto line = r.removeFromTop (26);
        findButton.setBounds (line.removeFromLeft (110));
        line.removeFromLeft (6);
        setButton.setBounds (line.removeFromLeft (110));
    }
    r.removeFromTop (12);
    const auto row = [&] (juce::Label& label) {
        auto line = r.removeFromTop (26);
        r.removeFromTop (6);
        label.setBounds (line.removeFromLeft (50));
        return line;
    };
    {
        auto line = row (formLabel);
        newButton.setBounds (line.removeFromRight (60));
        line.removeFromRight (6);
        forms.setBounds (line);
    }
    name.setBounds (row (nameLabel));
    {
        auto line = row (partsLabel);
        saveButton.setBounds (line.removeFromRight (90));
        line.removeFromRight (6);
        parts.setBounds (line);
    }
    {
        auto line = r.removeFromTop (28);
        playButton.setBounds (line.removeFromLeft (90));
        line.removeFromLeft (6);
        stopButton.setBounds (line.removeFromLeft (60));
        line.removeFromLeft (6);
        useButton.setBounds (line.removeFromLeft (120));
    }
    r.removeFromTop (8);
    block (status, 6);
    viewport.setBounds (r);
    layoutList();
}

} // namespace arranger::ui

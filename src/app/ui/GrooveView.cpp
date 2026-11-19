#include "GrooveView.h"
#include "ProposalCard.h"
#include "../AudioFiles.h"
#include "../Grooves.h"

#include "arranger/core/Arranging.h"
#include "arranger/core/SongEdit.h"

namespace arranger::ui
{

namespace
{
    juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    constexpr int kFileItem = 1000, kWordsItem = 1001;

    /** The bars the tool works on: the loop when it is on, else the whole song. */
    std::pair<Tick, Tick> range (const Song& s)
    {
        if (s.loop.enabled && s.loop.isValid())
            return { s.loop.start, s.loop.end };
        return { 0, std::numeric_limits<Tick>::max() };
    }
}

GrooveView::GrooveView (Session& s) : session (s), tools (std::make_unique<SessionTools> (s))
{
    const auto& theme = currentTheme();
    for (auto* l : { &title, &about, &fromLabel, &grooveLabel, &onLabel, &feel, &match, &status })
    {
        l->setColour (juce::Label::textColourId, theme.colour (l == &title ? "text" : (l == &feel ? "text" : "textMuted")));
        l->setJustificationType (l == &fromLabel || l == &grooveLabel || l == &onLabel ? juce::Justification::centredLeft : juce::Justification::topLeft);
        l->setMinimumHorizontalScale (1.0f);
        l->setFont (theme.font (12.5f));
        l->setBorderSize ({ 0, 0, 0, 0 });
        addAndMakeVisible (*l);
    }
    title.setText ("Groove", juce::dontSendNotification);
    title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    about.setText ("Take the feel of a part, a file or a few words, and put it on stiff parts. Hear before and after, then apply (one undo step). "
                   "With the loop on, only the loop changes.",
                   juce::dontSendNotification);
    fromLabel.setText ("From", juce::dontSendNotification);
    grooveLabel.setText ("Groove", juce::dontSendNotification);
    onLabel.setText ("Put on", juce::dontSendNotification);
    match.setColour (juce::Label::textColourId, theme.colour ("accent"));

    source.onChange = [this] {
        words.setVisible (source.getSelectedId() == kWordsItem);
        takeButton.setButtonText (source.getSelectedId() == kFileItem ? "Choose..." : "Take");
        resized();
    };
    words.setTextToShowWhenEmpty ("lazy swing, behind the beat", theme.colour ("textDim"));
    words.onReturnKey = [this] { take(); };
    addChildComponent (words);
    takeButton.setTooltip ("Take the groove from the part, the file or the words, and keep it in your groove library");
    takeButton.onClick = [this] { take(); };

    grooveBox.onChange = [this] {
        if (const auto* g = current())
            feel.setText (utf8 (groove::describe (*g)) + (g->about.empty() ? juce::String() : "\n" + utf8 (g->about)), juce::dontSendNotification);
        updateMatch();
    };
    target.onChange = [this] { updateMatch(); };

    const auto setup = [this] (Amount& a, const char* name, double value, const char* tip) {
        a.label.setText (name, juce::dontSendNotification);
        a.label.setColour (juce::Label::textColourId, currentTheme().colour ("textMuted"));
        a.slider.setSliderStyle (juce::Slider::LinearHorizontal);
        a.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        a.slider.setRange (0.0, 150.0, 5.0);
        a.slider.setTextValueSuffix (" %");
        a.slider.setValue (value, juce::dontSendNotification);
        a.slider.setTooltip (tip);
        a.slider.onDragEnd = [this] { updateMatch(); };
        a.slider.onValueChange = [this] { if (! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()) updateMatch(); };
        addAndMakeVisible (a.label);
        addAndMakeVisible (a.slider);
    };
    setup (timing, "Timing", 100.0, "How far the notes move to the groove's timing");
    setup (accents, "Accents", 80.0, "How much of the groove's loud and soft steps the notes take (the part keeps its own level)");
    setup (kick, "Kick", 100.0, "Drum parts: the amount for the kick (0 keeps the kick on the grid)");
    setup (snare, "Snare", 100.0, "Drum parts: the amount for the snare");
    setup (hats, "Hi-hats", 100.0, "Drum parts: the amount for the hi-hats and cymbals");

    beforeButton.setTooltip ("Play the bars as they are, looping");
    afterButton.setTooltip ("Play the bars with the groove, looping. The song does not change.");
    stopButton.setTooltip ("Stop the comparison");
    applyButton.setTooltip ("Put the groove on the parts (one undo step)");
    beforeButton.onClick = [this] { play (false); };
    afterButton.onClick = [this] { play (true); };
    stopButton.onClick = [this] { stopPlaying(); };
    applyButton.onClick = [this] { apply(); };
    for (auto* c : std::initializer_list<juce::Component*> { &source, &grooveBox, &target, &takeButton, &beforeButton, &afterButton, &stopButton, &applyButton })
        addAndMakeVisible (*c);

    refreshSources();
    refreshLibrary();
}

GrooveView::~GrooveView()
{
    stopPlaying();
}

void GrooveView::refreshSources()
{
    const auto& song = session.song();
    source.clear (juce::dontSendNotification);
    target.clear (juce::dontSendNotification);
    sourceTracks.clear();
    targetTracks = { 0 };
    target.addItem ("All other tracks", 1);
    int firstDrums = 0;
    for (const auto& t : song.tracks)
    {
        const bool audio = t.output.kind == TrackOutput::Kind::audio;
        const bool hasContent = audio ? ! t.audio.empty() : ! groove::notesOf (t).empty();
        if (hasContent)
        {
            sourceTracks.push_back (t.id);
            source.addItem (utf8 (t.name) + (audio ? "  (audio)" : ""), static_cast<int> (sourceTracks.size()));
            if (firstDrums == 0 && ! audio && arranging::isDrumTrack (t, &session.deviceLibrary()))
                firstDrums = static_cast<int> (sourceTracks.size());
        }
        if (! audio)
        {
            targetTracks.push_back (t.id);
            target.addItem (utf8 (t.name), static_cast<int> (targetTracks.size()));
        }
    }
    source.addSeparator();
    source.addItem ("A file (MIDI or audio)...", kFileItem);
    source.addItem ("Words (describe the feel)", kWordsItem);
    // The selected track's part, else the drums, else the words.
    int pick = 0;
    for (std::size_t i = 0; i < sourceTracks.size(); ++i)
        if (sourceTracks[i] == session.selectedTrack())
            pick = static_cast<int> (i) + 1;
    source.setSelectedId (pick > 0 ? pick : (firstDrums > 0 ? firstDrums : kWordsItem), juce::sendNotificationSync);
    target.setSelectedId (1, juce::dontSendNotification);
}

void GrooveView::refreshLibrary (const std::string& select)
{
    library = grooves::library (session.song().baseTempo());
    grooveBox.clear (juce::dontSendNotification);
    bool user = false;
    int pick = 0;
    for (std::size_t i = 0; i < library.size(); ++i)
    {
        if (library[i].source != "built-in" && ! user)
        {
            grooveBox.addSectionHeading ("Your grooves");
            user = true;
        }
        grooveBox.addItem (utf8 (library[i].name), static_cast<int> (i) + 1);
        if (! select.empty() && library[i].name == select)
            pick = static_cast<int> (i) + 1;
    }
    grooveBox.setSelectedId (pick > 0 ? pick : 4, juce::sendNotificationSync); // Swing 58 % by default
}

const groove::Groove* GrooveView::current() const
{
    const int i = grooveBox.getSelectedId() - 1;
    return i >= 0 && i < static_cast<int> (library.size()) ? &library[static_cast<std::size_t> (i)] : nullptr;
}

void GrooveView::setStatus (const juce::String& text)
{
    status.setText (text, juce::dontSendNotification);
    resized();
}

void GrooveView::takeWith (nlohmann::json args)
{
    try
    {
        const auto result = nlohmann::json::parse (tools->server().call (args.contains ("words") ? "make_groove" : "extract_groove", args.dump()));
        refreshLibrary (result.value ("name", std::string()));
        setStatus ("Kept \"" + utf8 (result.value ("name", std::string())) + "\" in your grooves: " + utf8 (result.value ("feel", std::string())) + ".");
    }
    catch (const std::exception& e)
    {
        setStatus (utf8 (e.what()));
    }
}

void GrooveView::take()
{
    const int id = source.getSelectedId();
    if (id == kWordsItem)
    {
        const auto text = words.getText().trim();
        if (text.isEmpty())
        {
            setStatus ("Write how it should feel, for example \"lazy swing, behind the beat\" or \"pushed 16ths, tight kick\".");
            return;
        }
        takeWith ({ { "name", text.substring (0, 40).toStdString() }, { "words", text.toStdString() } });
        return;
    }
    if (id == kFileItem)
    {
        chooser = std::make_unique<juce::FileChooser> ("Take the groove from a file", juce::File(), "*.mid;*.midi;" + audio_files::wildcard());
        juce::Component::SafePointer<GrooveView> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc) {
            if (safe == nullptr || fc.getResult() == juce::File())
                return;
            const auto file = fc.getResult();
            safe->takeWith ({ { "name", file.getFileNameWithoutExtension().toStdString() }, { "file", file.getFullPathName().toStdString() } });
        });
        return;
    }
    if (id < 1 || id > static_cast<int> (sourceTracks.size()))
        return;
    const auto* t = edit::findTrack (session.song(), sourceTracks[static_cast<std::size_t> (id - 1)]);
    if (t == nullptr)
        return;
    nlohmann::json args = { { "name", "Groove of " + t->name }, { "track", t->id } };
    const auto [from, to] = range (session.song());
    if (to != std::numeric_limits<Tick>::max())
    {
        const Tick bar = std::max<Tick> (1, session.song().timeSignature.ticksPerBar());
        args["from_bar"] = from / bar + 1;
        args["to_bar"] = std::max (from / bar + 1, (to - 1) / bar + 1);
    }
    takeWith (args);
}

std::vector<TrackId> GrooveView::targets() const
{
    const int i = target.getSelectedId() - 1;
    if (i > 0 && i < static_cast<int> (targetTracks.size()))
        return { targetTracks[static_cast<std::size_t> (i)] };
    // All other tracks: every MIDI track but the one the groove came from.
    std::vector<TrackId> out;
    const int s = source.getSelectedId();
    const TrackId from = s >= 1 && s <= static_cast<int> (sourceTracks.size()) ? sourceTracks[static_cast<std::size_t> (s - 1)] : 0;
    for (std::size_t k = 1; k < targetTracks.size(); ++k)
        if (targetTracks[k] != from)
            out.push_back (targetTracks[k]);
    return out;
}

groove::ApplyOptions GrooveView::options() const
{
    groove::ApplyOptions o;
    o.timing = timing.slider.getValue() / 100.0;
    o.velocity = accents.slider.getValue() / 100.0;
    o.laneAmount[static_cast<std::size_t> (groove::Lane::kick)] = kick.slider.getValue() / 100.0;
    o.laneAmount[static_cast<std::size_t> (groove::Lane::snare)] = snare.slider.getValue() / 100.0;
    o.laneAmount[static_cast<std::size_t> (groove::Lane::hats)] = hats.slider.getValue() / 100.0;
    std::tie (o.from, o.to) = range (session.song());
    return o;
}

nlohmann::json GrooveView::applyArgs() const
{
    nlohmann::json tracks = nlohmann::json::array();
    for (auto id : targets())
        tracks.push_back (id);
    nlohmann::json a = { { "groove", current() != nullptr ? current()->name : std::string() }, { "tracks", tracks },
                         { "timing", timing.slider.getValue() }, { "velocity", accents.slider.getValue() },
                         { "kick", kick.slider.getValue() }, { "snare", snare.slider.getValue() }, { "hats", hats.slider.getValue() } };
    const auto [from, to] = range (session.song());
    if (to != std::numeric_limits<Tick>::max())
    {
        const Tick bar = std::max<Tick> (1, session.song().timeSignature.ticksPerBar());
        a["from_bar"] = from / bar + 1;
        a["to_bar"] = std::max (from / bar + 1, (to - 1) / bar + 1);
    }
    return a;
}

void GrooveView::updateMatch()
{
    const auto* g = current();
    if (g == nullptr || g->empty())
    {
        match.setText ({}, juce::dontSendNotification);
        return;
    }
    const auto& song = session.song();
    const auto ids = targets();
    auto after = song;
    const auto o = options();
    groove::apply (after, ids, *g, o);
    double before = 0.0, now = 0.0, error = 0.0;
    int parts = 0;
    bool drums = false;
    for (auto id : ids)
    {
        const auto* a = edit::findTrack (song, id);
        const auto* b = edit::findTrack (after, id);
        if (a == nullptr || b == nullptr || groove::notesOf (*a, o.from, o.to).size() < 4)
            continue;
        const bool drum = arranging::isDrumTrack (*a, &session.deviceLibrary());
        drums = drums || drum;
        const auto ba = groove::compareChange (*a, *b, *g, song.timeSignature, song.baseTempo(), o.from, o.to, drum);
        if (ba.notes < 4)
            continue;
        const auto& m0 = ba.before;
        const auto& m1 = ba.after;
        before += m0.score;
        now += m1.score;
        error += m1.timingErrorMs;
        ++parts;
    }
    for (auto* a : { &kick, &snare, &hats })
    {
        a->label.setVisible (drums);
        a->slider.setVisible (drums);
    }
    if (parts == 0)
        match.setText ("No notes to change in the parts you chose.", juce::dontSendNotification);
    else
        match.setText ("Feel match: " + juce::String (juce::roundToInt (before / parts)) + " now, " + juce::String (juce::roundToInt (now / parts))
                           + " with the groove (of 100; timing " + juce::String (error / parts, 1) + " ms from it)",
                       juce::dontSendNotification);
    resized();
}

void GrooveView::play (bool after)
{
    try
    {
        auto a = applyArgs();
        a["listen"] = after ? "after" : "before";
        tools->server().call ("play_groove", a.dump());
        playing = true;
        setStatus (after ? "Playing with the groove. Press Play Before to compare." : "Playing as it is now.");
    }
    catch (const std::exception& e)
    {
        setStatus (utf8 (e.what()));
    }
}

void GrooveView::stopPlaying()
{
    if (! playing)
        return;
    playing = false;
    session.audition (std::nullopt);
    if (! getLocalBounds().isEmpty())
        setStatus ({});
}

void GrooveView::apply()
{
    stopPlaying();
    try
    {
        const auto result = nlohmann::json::parse (tools->server().call ("apply_groove", applyArgs().dump()));
        setStatus ("Changed " + juce::String (result.value ("notes_changed", 0)) + " notes. Undo takes it back.");
        updateMatch();
    }
    catch (const std::exception& e)
    {
        setStatus (utf8 (e.what()));
    }
}

void GrooveView::takeFromWords (const juce::String& text)
{
    source.setSelectedId (kWordsItem, juce::sendNotificationSync);
    words.setText (text, false);
    take();
}

void GrooveView::choose (const juce::String& name, TrackId on)
{
    for (std::size_t i = 0; i < library.size(); ++i)
        if (utf8 (library[i].name) == name)
            grooveBox.setSelectedId (static_cast<int> (i) + 1, juce::sendNotificationSync);
    for (std::size_t i = 0; i < targetTracks.size(); ++i)
        if (targetTracks[i] == on)
            target.setSelectedId (static_cast<int> (i) + 1, juce::sendNotificationSync);
}

void GrooveView::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    title.setBounds (r.removeFromTop (24));
    const auto block = [&] (juce::Label& l, int gap) {
        const int h = l.getText().isEmpty() ? 0 : static_cast<int> (textHeight (l.getText(), l.getFont(), r.getWidth())) + 8;
        l.setBounds (r.removeFromTop (h));
        r.removeFromTop (h > 0 ? gap : 0);
    };
    block (about, 8);
    const auto row = [&] (juce::Label& label) {
        auto line = r.removeFromTop (26);
        r.removeFromTop (6);
        label.setBounds (line.removeFromLeft (62));
        return line;
    };
    {
        auto line = row (fromLabel);
        takeButton.setBounds (line.removeFromRight (82));
        line.removeFromRight (6);
        source.setBounds (line);
    }
    if (words.isVisible())
    {
        auto line = r.removeFromTop (26);
        r.removeFromTop (6);
        line.removeFromLeft (62);
        words.setBounds (line);
    }
    grooveBox.setBounds (row (grooveLabel));
    {
        auto line = r.removeFromTop (static_cast<int> (textHeight (feel.getText(), feel.getFont(), r.getWidth() - 62)) + 8);
        r.removeFromTop (6);
        line.removeFromLeft (62);
        feel.setBounds (line);
    }
    target.setBounds (row (onLabel));
    for (auto* a : { &timing, &accents, &kick, &snare, &hats })
    {
        if (! a->slider.isVisible())
            continue;
        auto line = r.removeFromTop (24);
        r.removeFromTop (4);
        a->label.setBounds (line.removeFromLeft (62));
        a->slider.setBounds (line);
    }
    r.removeFromTop (6);
    block (match, 8);
    auto buttons = r.removeFromTop (28);
    const int w = (buttons.getWidth() - 3 * 6) / 4;
    for (auto* b : { &beforeButton, &afterButton, &stopButton, &applyButton })
    {
        b->setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (6);
    }
    r.removeFromTop (8);
    block (status, 0);
}

} // namespace arranger::ui

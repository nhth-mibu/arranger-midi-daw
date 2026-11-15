#include "AiToolsView.h"

#include "arranger/core/GeneralMidi.h"
#include "arranger/core/Notation.h"
#include "arranger/core/SongEdit.h"

#include <nlohmann/json.hpp>

namespace arranger::ui
{

using nlohmann::json;

namespace
{
    juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    bool isTrackTool (const juce::String& tool) { return tool != "ai_melody" && tool != "install"; }
    bool newTrackAllowed (const juce::String& tool) { return tool == "ai_write_part" || tool == "ai_melody"; }

    juce::String aboutText (const juce::String& tool)
    {
        if (tool == "ai_humanize")
            return "Makes the notes sound played by a person: small changes in timing and loudness. Magenta Groove learned this from real drummers (drum tracks in 4/4). Simple works on any track and needs no install.";
        if (tool == "ai_drumify")
            return "Magenta Drumify makes a drum part from the rhythm of a track, for example a bass line or notes you tapped. The drums go to a new drum track.";
        if (tool == "ai_continue")
            return "The AI goes on from the bars before. Magenta: one track (a melody or drums). Anticipatory: one track that plays along with the others, or all tracks; when music comes after the bars, it fills the gap.";
        if (tool == "ai_write_part")
            return "The Anticipatory Music Transformer writes a new part that fits the other tracks, for example a bass line under the piano and drums.";
        if (tool == "ai_melody")
            return "Magenta ImprovRNN writes a melody over the chord symbols of the song (Score > Chords, or ask the AI chat to find them).";
        return "The AI music tools run on this computer. Magenta needs about 100 MB (and Node.js, which the install gets when it is missing). The Anticipatory Music Transformer needs PyTorch and about 2 GB.";
    }
} // namespace

juce::String AiToolsView::titleFor (const juce::String& tool)
{
    if (tool == "ai_humanize") return "Humanize";
    if (tool == "ai_drumify") return "Drumify";
    if (tool == "ai_continue") return "Continue with AI";
    if (tool == "ai_write_part") return "Write a Part";
    if (tool == "ai_melody") return "Melody over Chords";
    return "AI Music Tools";
}

AiToolsView::AiToolsView (Session& s, MidiAiService& m, const juce::String& t) : session (s), service (m), tool (t)
{
    const auto& theme = currentTheme();
    for (auto* l : { &title, &about, &trackLabel, &engineLabel, &soundLabel, &barsLabel, &toLabel, &variationLabel, &status })
    {
        l->setColour (juce::Label::textColourId, theme.colour (l == &title ? "text" : "textMuted"));
        addAndMakeVisible (*l);
    }
    title.setText (titleFor (tool), juce::dontSendNotification);
    title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    about.setText (aboutText (tool), juce::dontSendNotification);
    about.setJustificationType (juce::Justification::topLeft);
    about.setMinimumHorizontalScale (1.0f);
    trackLabel.setText (tool == "ai_drumify" ? "Rhythm from" : "Track", juce::dontSendNotification);
    engineLabel.setText ("Model", juce::dontSendNotification);
    soundLabel.setText ("Sound", juce::dontSendNotification);
    barsLabel.setText ("Bars", juce::dontSendNotification);
    toLabel.setText ("to", juce::dontSendNotification);
    toLabel.setJustificationType (juce::Justification::centred);
    variationLabel.setText ("Variation", juce::dontSendNotification);

    // Tracks: the MIDI tracks; "All tracks" or "New track" first where the tool allows it.
    const auto& song = session.song();
    int id = 1;
    if (tool == "ai_continue")
    {
        track.addItem ("All tracks (Anticipatory)", id++);
        trackIds.push_back (0);
    }
    if (newTrackAllowed (tool))
    {
        track.addItem ("New track", id++);
        trackIds.push_back (0);
    }
    int selected = 1;
    for (const auto& tr : song.tracks)
    {
        if (tr.output.kind == TrackOutput::Kind::audio)
            continue;
        if (tr.id == session.selectedTrack())
            selected = id;
        track.addItem (utf8 (tr.name), id++);
        trackIds.push_back (tr.id);
    }
    track.setSelectedId (newTrackAllowed (tool) ? 1 : selected, juce::dontSendNotification);
    track.onChange = [this] { refresh(); };

    if (tool == "ai_humanize")
    {
        engine.addItem ("Magenta Groove (drums)", 1);
        engine.addItem ("Simple (any track, no AI)", 2);
    }
    else if (tool == "ai_continue")
    {
        engine.addItem ("Magenta (one track)", 1);
        engine.addItem ("Anticipatory Music Transformer", 2);
    }
    engine.setSelectedId (1, juce::dontSendNotification);
    engine.onChange = [this] { refresh(); };

    sound.addItem ("Drums", 129);
    for (int p = 0; p < 128; ++p)
        sound.addItem (kGeneralMidiNames[p], p + 1);
    sound.setSelectedId (tool == "ai_melody" ? 74 : 34, juce::dontSendNotification); // Flute; Electric Bass (finger)

    // Bars: the loop, else the selected clips, else bars 1-4. Continue starts after the music.
    const Tick barLength = song.timeSignature.ticksPerBar();
    int first = 1, last = 4;
    if (song.loop.enabled && song.loop.isValid())
    {
        first = static_cast<int> (song.loop.start / barLength) + 1;
        last = static_cast<int> ((song.loop.end - 1) / barLength) + 1;
    }
    else if (! session.selectedClips().empty())
    {
        Tick from = std::numeric_limits<Tick>::max(), to = 0;
        for (const auto clip : session.selectedClips())
            if (const auto c = edit::findClipPtr (song, clip))
            {
                from = std::min (from, c->start);
                to = std::max (to, c->end());
            }
        if (to > 0)
        {
            first = static_cast<int> (from / barLength) + 1;
            last = static_cast<int> ((to - 1) / barLength) + 1;
        }
    }
    if (tool == "ai_continue" && ! (song.loop.enabled && song.loop.isValid()))
    {
        first = static_cast<int> ((std::max (song.contentEnd(), barLength) + barLength - 1) / barLength) + 1;
        last = first + 3;
    }
    fromBar.setText (juce::String (first), false);
    toBar.setText (juce::String (last), false);
    for (auto* e : { &fromBar, &toBar })
        e->setInputRestrictions (3, "0123456789");

    variation.setRange (0.1, 2.0, 0.05);
    variation.setValue (1.0, juce::dontSendNotification);
    variation.setTooltip ("Low: safe and plain. High: wild.");

    output.setMultiLine (true);
    output.setReadOnly (true);
    output.setScrollbarsShown (true);
    output.setColour (juce::TextEditor::backgroundColourId, theme.colour ("panelRaised"));
    output.setColour (juce::TextEditor::outlineColourId, theme.colour ("line"));
    output.setColour (juce::TextEditor::textColourId, theme.colour ("text"));

    withAnticipation.setToggleState (true, juce::dontSendNotification);
    withAnticipation.setColour (juce::ToggleButton::textColourId, theme.colour ("text"));

    for (auto* c : std::initializer_list<juce::Component*> { &track, &engine, &sound, &fromBar, &toBar, &variation, &output, &withAnticipation, &bar,
                                                             &goButton, &stopButton, &installButton })
        addAndMakeVisible (c);
    goButton.onClick = [this] { go(); };
    stopButton.onClick = [this] {
        service.cancel();
        running = false;
        stopTimer();
        setStatus ("Stopped.");
        refresh();
    };
    installButton.onClick = [this] { install(); };

    setSize (560, tool == "install" ? 420 : 440);
    refresh();
}

AiToolsView::~AiToolsView()
{
    stopTimer();
    if (running)
        service.cancel();
}

static TrackId selectedId (const juce::ComboBox& box, const std::vector<TrackId>& ids)
{
    const int i = box.getSelectedId() - 1;
    return i >= 0 && i < static_cast<int> (ids.size()) ? ids[static_cast<std::size_t> (i)] : 0;
}

juce::String AiToolsView::neededHelper() const
{
    if (tool == "ai_humanize")
        return engine.getSelectedId() == 1 ? ai::kMagenta : "";
    if (tool == "ai_continue")
        return engine.getSelectedId() == 2 || selectedId (track, trackIds) == 0 ? ai::kAnticipation : ai::kMagenta;
    if (tool == "ai_write_part")
        return ai::kAnticipation;
    if (tool == "install")
        return {};
    return ai::kMagenta;
}

void AiToolsView::refresh()
{
    const bool installPage = tool == "install";
    const auto helper = neededHelper();
    const bool ready = helper.isEmpty() || (helper == ai::kMagenta ? MidiAiService::magentaInstalled() : MidiAiService::anticipationInstalled());
    const bool newTrack = newTrackAllowed (tool) && selectedId (track, trackIds) == 0;

    for (auto* c : std::initializer_list<juce::Component*> { &trackLabel, &track })
        c->setVisible (! installPage);
    for (auto* c : std::initializer_list<juce::Component*> { &engineLabel, &engine })
        c->setVisible (tool == "ai_humanize" || tool == "ai_continue");
    for (auto* c : std::initializer_list<juce::Component*> { &soundLabel, &sound })
        c->setVisible (newTrack);
    for (auto* c : std::initializer_list<juce::Component*> { &barsLabel, &fromBar, &toLabel, &toBar, &variationLabel, &variation, &goButton })
        c->setVisible (! installPage);
    if (tool == "ai_continue" && selectedId (track, trackIds) == 0)
        engine.setSelectedId (2, juce::dontSendNotification);

    withAnticipation.setVisible (installPage || ! ready);
    installButton.setVisible (installPage || ! ready);
    installButton.setEnabled (! service.installing());
    goButton.setEnabled (ready && ! running && ! service.installing());
    stopButton.setVisible (running);
    bar.setVisible (running || service.installing());
    if (! running && ! service.installing())
        setStatus (ready ? (installPage ? MidiAiService::statusText() : juce::String ("Ready. Undo takes the result back."))
                         : juce::String ("This needs the AI music tools. ") + MidiAiService::statusText());
    if (! ready && helper == ai::kAnticipation)
        withAnticipation.setToggleState (true, juce::dontSendNotification);
    resized();
}

void AiToolsView::setStatus (const juce::String& text)
{
    statusTextValue = text;
    showStatus();
}

void AiToolsView::showStatus()
{
    auto text = statusTextValue;
    if (running && runStartedMs > 0.0)
    {
        const auto seconds = static_cast<int> ((juce::Time::getMillisecondCounterHiRes() - runStartedMs) / 1000.0);
        text << "  " << seconds / 60 << ":" << juce::String (seconds % 60).paddedLeft ('0', 2);
    }
    status.setText (text, juce::dontSendNotification);
}

void AiToolsView::timerCallback()
{
    if (running)
        showStatus();
    else
        stopTimer();
}

juce::String AiToolsView::argumentsJson() const
{
    json a = json::object();
    a["from_bar"] = std::max (1, fromBar.getText().getIntValue());
    a["to_bar"] = std::max (a["from_bar"].get<int>(), toBar.getText().getIntValue());
    const auto id = selectedId (track, trackIds);
    if (id != 0)
    {
        a["track"] = id;
    }
    const double v = variation.getValue();
    if (tool == "ai_humanize")
    {
        a["engine"] = engine.getSelectedId() == 1 ? "magenta" : "simple";
        a["amount"] = juce::jlimit (0.0, 1.0, v / 2.0);
        a["temperature"] = juce::jlimit (0.1, 2.0, v * 0.5);
        a["seed"] = static_cast<unsigned> (juce::Random::getSystemRandom().nextInt (1 << 30));
    }
    else if (tool == "ai_continue")
    {
        a["engine"] = engine.getSelectedId() == 1 && id != 0 ? "magenta" : "anticipation";
        a["temperature"] = v;
        a["top_p"] = juce::jlimit (0.5, 1.0, 0.85 + v * 0.075);
    }
    else if (tool == "ai_write_part")
    {
        a["top_p"] = juce::jlimit (0.5, 1.0, 0.85 + v * 0.075);
    }
    else
    {
        a["temperature"] = v;
    }
    if (newTrackAllowed (tool) && id == 0)
    {
        const int s = sound.getSelectedId();
        if (s == 129)
            a["sound"] = "drums";
        else
            a["sound"] = s;
    }
    return utf8 (a.dump());
}

void AiToolsView::go()
{
    if (running || tool == "install")
        return;
    ai::Job job;
    try
    {
        job = ai::prepare (session.song(), tool.toStdString(), argumentsJson().toStdString());
    }
    catch (const std::exception& e)
    {
        setStatus (utf8 (e.what()));
        return;
    }
    auto apply = [this] (const ai::Job& j, const std::string& answer) {
        try
        {
            std::string result;
            session.edit (j.title, [&] (Song& s) { result = j.apply (s, answer); });
            const auto r = json::parse (result);
            juce::String text = "Done";
            if (r.contains ("notes"))
                text << ": " << r["notes"].get<int>() << " notes on " << utf8 (r.value ("track", std::string()));
            else if (r.contains ("tracks"))
                for (const auto& t : r["tracks"])
                    text << (text == "Done" ? ": " : ", ") << t.value ("notes", 0) << " notes on " << utf8 (t.value ("track", std::string()));
            if (r.contains ("note"))
                text << ". " << utf8 (r["note"].get<std::string>());
            setStatus (text + ". Undo takes it back.");
        }
        catch (const std::exception& e)
        {
            setStatus (utf8 (e.what()));
        }
    };
    if (job.helper.empty())
    {
        apply (job, {});
        return;
    }
    running = true;
    runStartedMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (2);
    progress = 0.0;
    setStatus ("Starting the AI...");
    const auto error = service.start (std::move (job),
        [safe = juce::Component::SafePointer<AiToolsView> (this)] (double v, const juce::String& text) {
            if (safe != nullptr)
            {
                safe->progress = v;
                safe->setStatus (text + " " + juce::String (juce::roundToInt (v * 100.0)) + "%");
            }
        },
        [safe = juce::Component::SafePointer<AiToolsView> (this), apply] (const ai::Job& j, const std::string& answer, const juce::String& err) {
            if (safe == nullptr)
                return;
            safe->running = false;
            safe->stopTimer();
            safe->progress = 1.0;
            if (err.isNotEmpty())
                safe->setStatus (err);
            else
                apply (j, answer);
            safe->refresh();
        });
    if (error.isNotEmpty())
    {
        running = false;
        stopTimer();
        setStatus (error);
    }
    refresh();
}

void AiToolsView::install()
{
    output.clear();
    progress = -1.0; // busy
    const auto error = service.install (withAnticipation.getToggleState(),
        [safe = juce::Component::SafePointer<AiToolsView> (this)] (const juce::String& line) {
            if (safe != nullptr)
            {
                safe->output.moveCaretToEnd();
                safe->output.insertTextAtCaret (line + "\n");
                safe->setStatus (line);
            }
        },
        [safe = juce::Component::SafePointer<AiToolsView> (this)] (const juce::String& err) {
            if (safe == nullptr)
                return;
            safe->progress = 0.0;
            safe->refresh();
            if (err.isNotEmpty())
                safe->setStatus (err);
        });
    if (error.isNotEmpty())
        setStatus (error);
    refresh();
}

void AiToolsView::paint (juce::Graphics& g)
{
    g.fillAll (currentTheme().colour ("panel"));
}

void AiToolsView::resized()
{
    auto r = getLocalBounds().reduced (20, 16);
    title.setBounds (r.removeFromTop (26));
    about.setBounds (r.removeFromTop (72));
    r.removeFromTop (8);
    auto row = [&] (juce::Label& label, int height = 28) {
        auto line = r.removeFromTop (height);
        r.removeFromTop (8);
        label.setBounds (line.removeFromLeft (90));
        return line;
    };
    if (track.isVisible())
        track.setBounds (row (trackLabel));
    if (engine.isVisible())
        engine.setBounds (row (engineLabel));
    if (sound.isVisible())
        sound.setBounds (row (soundLabel));
    if (fromBar.isVisible())
    {
        auto line = row (barsLabel);
        fromBar.setBounds (line.removeFromLeft (60));
        toLabel.setBounds (line.removeFromLeft (36));
        toBar.setBounds (line.removeFromLeft (60));
        variation.setBounds (row (variationLabel));
    }
    auto buttons = r.removeFromBottom (32);
    goButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (8);
    stopButton.setBounds (buttons.removeFromRight (80));
    if (installButton.isVisible())
        installButton.setBounds (buttons.removeFromLeft (190));
    r.removeFromBottom (8);
    status.setBounds (r.removeFromBottom (40));
    bar.setBounds (r.removeFromBottom (12));
    r.removeFromBottom (6);
    if (withAnticipation.isVisible())
    {
        withAnticipation.setBounds (r.removeFromTop (26).withTrimmedLeft (8));
        r.removeFromTop (6);
    }
    output.setVisible (tool == "install" || service.installing() || output.getTotalNumChars() > 0);
    output.setBounds (r);
}

} // namespace arranger::ui

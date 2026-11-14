#include "AnalysisView.h"

#include "arranger/core/SongEdit.h"

namespace arranger::ui
{

namespace
{
    juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    const std::pair<const char*, const char*> kLanguages[] = {
        { "auto", "Find the language" }, { "en", "English" }, { "da", "Danish" }, { "vi", "Vietnamese" }, { "de", "German" },
        { "fr", "French" }, { "es", "Spanish" }, { "it", "Italian" }, { "nl", "Dutch" }, { "sv", "Swedish" }, { "no", "Norwegian" },
        { "pt", "Portuguese" }, { "ja", "Japanese" }, { "ko", "Korean" }, { "zh", "Chinese" },
    };

    juce::String trackName (const Song& s, TrackId id)
    {
        const auto* t = edit::findTrack (s, id);
        return t != nullptr ? utf8 (t->name) : juce::String();
    }
} // namespace

AnalysisView::AnalysisView (Session& s, ClipId c) : session (s), service (s.analysis()), clip (c)
{
    const auto& theme = currentTheme();
    for (auto* l : { &title, &toolsLine, &modelLine, &stepLine })
    {
        l->setColour (juce::Label::textColourId, theme.colour (l == &title ? "text" : "textMuted"));
        addAndMakeVisible (*l);
    }
    title.setFont (juce::FontOptions (15.0f, juce::Font::bold));

    for (auto* t : { &stems, &notes, &chords, &instruments, &lyrics })
    {
        t->setToggleState (true, juce::dontSendNotification);
        t->setColour (juce::ToggleButton::textColourId, theme.colour ("text"));
        addAndMakeVisible (*t);
    }

    for (int i = 0; i < static_cast<int> (std::size (kLanguages)); ++i)
        language.addItem (kLanguages[i].second, i + 1);
    language.setSelectedId (1, juce::dontSendNotification);
    language.setTooltip ("The language of the words");
    addAndMakeVisible (language);

    prompt.setTextToShowWhenEmpty ("Song title or words that help Whisper (optional)", theme.colour ("textDim"));
    addAndMakeVisible (prompt);

    output.setMultiLine (true);
    output.setReadOnly (true);
    output.setScrollbarsShown (true);
    output.setColour (juce::TextEditor::backgroundColourId, theme.colour ("panelRaised"));
    output.setColour (juce::TextEditor::outlineColourId, theme.colour ("line"));
    output.setColour (juce::TextEditor::textColourId, theme.colour ("text"));
    addAndMakeVisible (output);

    const auto names = AnalysisService::modelNames();
    for (int i = 0; i < names.size(); ++i)
        model.addItem (names[i] + " - " + AnalysisService::modelDescription (names[i]), i + 1);
    model.setSelectedId (names.indexOf (AnalysisService::chosenModel()) + 1, juce::dontSendNotification);
    model.onChange = [this] {
        const auto name = AnalysisService::modelNames()[model.getSelectedId() - 1];
        AnalysisService::chooseModel (name);
        refresh();
    };
    addAndMakeVisible (model);

    addAndMakeVisible (bar);
    for (auto* b : { &analyseButton, &stopButton, &installButton, &downloadButton, &askAiButton })
        addAndMakeVisible (*b);
    analyseButton.onClick = [this] { analyse(); };
    stopButton.onClick = [this] { service.cancel(); };
    installButton.onClick = [this] { install(); };
    downloadButton.onClick = [this] { downloadModel(); };
    askAiButton.onClick = [this] {
        const auto st = service.status();
        if (onAskAi && st.applied.lyricsTrack != 0)
            onAskAi ("Whisper wrote down the lyrics of the track \"" + trackName (session.song(), st.applied.lyricsTrack) + "\" (track id "
                     + juce::String (static_cast<int> (st.applied.lyricsTrack)) + "). Read them with get_lyrics and correct the words it heard wrong, "
                     "so that the lines make sense and rhyme where the song does. If you know the song, use its real words. "
                     "Fix them with edit_lyrics (one word or syllable per note), then tell me in short what you changed.");
    };

    if (const auto a = edit::findAudioClip (session.song(), clip))
        title.setText ("Analyse \"" + utf8 (a->name.empty() ? std::string ("audio clip") : a->name) + "\"", juce::dontSendNotification);
    else
        title.setText ("Select an audio clip first", juce::dontSendNotification);

    shownState = service.status().state;
    setSize (560, 600);
    refresh();
    startTimer (250);
}

AnalysisView::~AnalysisView() = default;

void AnalysisView::paint (juce::Graphics& g)
{
    g.fillAll (currentTheme().colour ("panel"));
}

void AnalysisView::resized()
{
    auto r = getLocalBounds().reduced (16);
    title.setBounds (r.removeFromTop (24));
    r.removeFromTop (6);
    for (auto* t : { &stems, &notes, &chords, &instruments, &lyrics })
        t->setBounds (r.removeFromTop (24));
    r.removeFromTop (6);
    auto row = r.removeFromTop (26);
    language.setBounds (row.removeFromLeft (180));
    row.removeFromLeft (8);
    prompt.setBounds (row);
    r.removeFromTop (10);

    row = r.removeFromTop (28);
    analyseButton.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (8);
    stopButton.setBounds (row.removeFromLeft (80));
    row.removeFromLeft (8);
    askAiButton.setBounds (row.removeFromRight (150));
    r.removeFromTop (8);
    bar.setBounds (r.removeFromTop (18));
    stepLine.setBounds (r.removeFromTop (22));

    // The tools, at the bottom.
    auto tools = r.removeFromBottom (64);
    row = tools.removeFromTop (28);
    installButton.setBounds (row.removeFromRight (130));
    toolsLine.setBounds (row);
    tools.removeFromTop (6);
    row = tools.removeFromTop (28);
    downloadButton.setBounds (row.removeFromRight (90));
    row.removeFromRight (8);
    model.setBounds (row.removeFromRight (240));
    modelLine.setBounds (row);
    r.removeFromBottom (8);
    output.setBounds (r);
}

void AnalysisView::analyse()
{
    AnalysisService::Request req;
    req.clip = clip;
    req.stems = stems.getToggleState();
    req.notes = notes.getToggleState();
    req.chords = chords.getToggleState();
    req.instruments = instruments.getToggleState();
    req.lyrics = lyrics.getToggleState();
    req.language = kLanguages[std::clamp (language.getSelectedId() - 1, 0, static_cast<int> (std::size (kLanguages)) - 1)].first;
    req.prompt = prompt.getText().trim();
    const auto why = service.start (req);
    if (why.isNotEmpty())
        output.setText (why, false);
    else
        output.clear();
    refresh();
}

void AnalysisView::install()
{
    installLog.clear();
    const auto why = service.installTools ([safe = juce::Component::SafePointer<AnalysisView> (this)] (const juce::String& line) {
        if (safe != nullptr)
            safe->log (line);
    }, [] (bool) {});
    if (why.isNotEmpty())
        log (why);
    refresh();
}

void AnalysisView::downloadModel()
{
    const auto name = AnalysisService::modelNames()[model.getSelectedId() - 1];
    modelProgress = 0.0;
    const auto safe = juce::Component::SafePointer<AnalysisView> (this);
    const auto why = service.downloadModel (name, [safe] (double p) {
        if (safe != nullptr)
        {
            safe->modelProgress = p;
            safe->refresh();
        } }, [safe] (const juce::String& error) {
        if (safe == nullptr)
            return;
        safe->modelProgress = -1.0;
        if (error.isNotEmpty())
            safe->log (error);
        safe->refresh();
    });
    if (why.isNotEmpty())
    {
        modelProgress = -1.0;
        log (why);
    }
    refresh();
}

void AnalysisView::log (const juce::String& line)
{
    installLog << line << "\n";
    output.setText (installLog, false);
    output.moveCaretToEnd();
}

juce::String AnalysisView::resultText (const AnalysisService::Status& st) const
{
    juce::String text;
    const auto& song = session.song();
    if (st.state == AnalysisService::Status::State::failed)
        text << st.error << "\n";
    if (st.state == AnalysisService::Status::State::done)
    {
        const auto& a = st.applied;
        text << "Added " << static_cast<int> (a.audioTracks.size()) << " stem tracks and " << static_cast<int> (a.midiTracks.size()) << " MIDI tracks.\n";
        if (a.chords > 0)
            text << a.chords << " chord symbols on \"" << trackName (song, a.chordTrack) << "\".\n";
        if (a.words > 0)
            text << a.words << " words on \"" << trackName (song, a.lyricsTrack) << "\"" << (st.language.empty() ? juce::String() : " (language: " + utf8 (st.language) + ")") << ".\n";
        if (! st.instruments.empty())
        {
            text << "\nInstruments heard:\n";
            for (const auto& [stem, name] : st.instruments)
                text << "  " << utf8 (stem) << ": " << utf8 (name) << "\n";
        }
        if (! st.chords.empty())
        {
            text << "\nChords: ";
            for (std::size_t i = 0; i < st.chords.size() && i < 64; ++i)
                text << utf8 (st.chords[i]) << " ";
            text << "\n";
        }
        if (! st.lyricLines.empty())
        {
            text << "\nLyrics, as Whisper heard them:\n";
            for (const auto& l : st.lyricLines)
                text << "  " << utf8 (l) << "\n";
        }
    }
    if (! st.warnings.empty())
    {
        text << "\n";
        for (const auto& w : st.warnings)
            text << utf8 (w) << "\n";
    }
    return text;
}

void AnalysisView::timerCallback()
{
    const auto st = service.status();
    if (st.state != shownState && (st.state == AnalysisService::Status::State::done || st.state == AnalysisService::Status::State::failed))
        output.setText (resultText (st), false);
    shownState = st.state;
    refresh();
}

void AnalysisView::refresh()
{
    const auto st = service.status();
    const bool running = service.isRunning();
    const bool installing = service.isInstalling();
    const bool haveClip = edit::findAudioClip (session.song(), clip) != nullptr;
    analyseButton.setEnabled (! running && ! installing && haveClip);
    stopButton.setEnabled (running);
    askAiButton.setVisible (st.state == AnalysisService::Status::State::done && st.applied.lyricsTrack != 0 && onAskAi != nullptr);
    progress = running ? st.progress : (st.state == AnalysisService::Status::State::done ? 1.0 : 0.0);
    stepLine.setText (running ? st.step : (st.state == AnalysisService::Status::State::failed ? juce::String ("Failed") : st.step), juce::dontSendNotification);

    const auto setup = service.setup();
    if (installing)
        toolsLine.setText ("Installing the analysis tools...", juce::dontSendNotification);
    else if (setup.python)
        toolsLine.setText ("Analysis tools ready" + (setup.device.isNotEmpty() ? " (" + setup.device + ")" : juce::String()), juce::dontSendNotification);
    else
        toolsLine.setText ("Stems, notes and instruments need the analysis tools", juce::dontSendNotification);
    installButton.setButtonText (setup.python ? "Install Again..." : "Install Tools...");
    installButton.setEnabled (! installing && ! running);

    if (! setup.whisper)
        modelLine.setText ("No Whisper in this build", juce::dontSendNotification);
    else if (modelProgress >= 0.0)
        modelLine.setText ("Downloading: " + juce::String (juce::roundToInt (modelProgress * 100.0)) + "%", juce::dontSendNotification);
    else
        modelLine.setText (setup.model.existsAsFile() ? "Whisper model ready" : "Lyrics need a Whisper model", juce::dontSendNotification);
    downloadButton.setEnabled (setup.whisper && modelProgress < 0.0 && ! setup.model.existsAsFile());
    lyrics.setEnabled (setup.whisper);
}

} // namespace arranger::ui

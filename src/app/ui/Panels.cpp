#include "Panels.h"

namespace arranger::ui
{

namespace
{
    const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");

    juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    void sectionLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r)
    {
        const auto& t = currentTheme();
        g.setColour (t.colour ("textDim"));
        g.setFont (t.font (11.0f, true).withExtraKerningFactor (0.1f));
        g.drawText (text.toUpperCase(), r, juce::Justification::bottomLeft);
    }
}

void drawCard (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = currentTheme();
    const float radius = t.metric ("radius", 8.0f) + 4.0f;
    g.setColour (t.colour ("panel"));
    g.fillRoundedRectangle (r, radius);
    g.setColour (t.colour ("line"));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

// ---------------------------------------------------------------------------
TracksPanel::TracksPanel (const Song& s) : song (s) {}

void TracksPanel::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    auto r = getLocalBounds();

    auto header = r.removeFromTop (56).reduced (20, 0);
    g.setColour (t.colour ("text"));
    g.setFont (t.font (15.0f, true));
    g.drawText ("Tracks", header, juce::Justification::centredLeft);
    g.setColour (t.colour ("textMuted"));
    g.setFont (t.font (15.0f));
    g.drawText (juce::String (song.tracks.size()), header.withTrimmedLeft (64), juce::Justification::centredLeft);
    g.setColour (t.colour ("line"));
    g.fillRect (r.getX(), r.getY(), r.getWidth(), 1);

    const int rowH = static_cast<int> (t.metric ("trackHeight", 64.0f));
    for (const auto& track : song.tracks)
    {
        auto row = r.removeFromTop (rowH);
        if (row.getY() > getHeight())
            break;

        const auto c = t.trackColour (utf8 (track.colour));
        g.setColour (c);
        g.fillRoundedRectangle (row.reduced (0, 10).withX (16).withWidth (4).toFloat(), 2.0f);

        auto text = row.reduced (32, 12);
        g.setColour (t.colour ("text"));
        g.setFont (t.font (15.0f, true));
        g.drawText (utf8 (track.name), text.removeFromTop (text.getHeight() / 2), juce::Justification::bottomLeft, true);
        g.setColour (t.colour ("textMuted"));
        g.setFont (t.font (13.0f));
        g.drawText ("MIDI" + dot + "Channel " + juce::String (track.midiChannel), text.withTrimmedTop (3), juce::Justification::topLeft, true);

        g.setColour (t.colour ("line"));
        g.fillRect (row.removeFromBottom (1));
    }

    if (song.tracks.empty())
    {
        g.setColour (t.colour ("textDim"));
        g.setFont (t.font (13.0f));
        g.drawFittedText ("No tracks yet.\nTracks and recording\narrive in Phase 1.", r.reduced (20).removeFromTop (80),
                          juce::Justification::topLeft, 3);
    }
}

// ---------------------------------------------------------------------------
TimelineView::TimelineView (const Song& s, const LiveState& l) : song (s), live (l)
{
    setTooltip ("Click the ruler to move the playhead");
}

void TimelineView::setView (int index)
{
    view = index;
    repaint();
}

float TimelineView::barWidth() const
{
    return juce::jmax (40.0f, 200.0f * static_cast<float> (song.timeSignature.ticksPerBar()) / (4.0f * kTicksPerQuarter) * 0.4f);
}

juce::Rectangle<int> TimelineView::gridArea() const
{
    return getLocalBounds().withTrimmedTop (56);
}

int TimelineView::playheadX() const
{
    const double bars = live.positionTicks / static_cast<double> (song.timeSignature.ticksPerBar());
    return gridArea().getX() + juce::roundToInt ((bars - static_cast<double> (firstBar)) * barWidth());
}

void TimelineView::refresh()
{
    // Page the view so the playhead stays visible.
    const auto visibleBars = static_cast<std::int64_t> (static_cast<float> (gridArea().getWidth()) / barWidth());
    const auto bar = static_cast<std::int64_t> (live.positionTicks / static_cast<double> (song.timeSignature.ticksPerBar()));
    if (visibleBars > 0)
    {
        const auto page = (bar / visibleBars) * visibleBars;
        if (page != firstBar)
        {
            firstBar = page;
            repaint();
        }
    }

    const int x = playheadX();
    if (x != shownPlayheadX)
    {
        repaint (shownPlayheadX - 2, 0, 5, getHeight());
        repaint (x - 2, 0, 5, getHeight());
        shownPlayheadX = x;
    }
}

void TimelineView::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    const auto grid = gridArea();
    const float bw = barWidth();
    const int beats = song.timeSignature.numerator;

    // Ruler
    auto ruler = getLocalBounds().removeFromTop (56);
    for (int i = 0; grid.getX() + i * bw < getWidth(); ++i)
    {
        const auto bar = firstBar + i + 1;
        const float x = static_cast<float> (grid.getX()) + i * bw;
        g.setColour ((bar - 1) % 4 == 0 ? t.colour ("text").withAlpha (0.85f) : t.colour ("textDim"));
        g.setFont (t.font (12.5f, (bar - 1) % 4 == 0));
        g.drawText (juce::String (bar), juce::Rectangle<float> (x + 6, 0, bw, 30), juce::Justification::centredLeft);
    }
    g.setColour (t.colour ("line"));
    g.fillRect (ruler.removeFromBottom (1));

    // Grid: bar lines and fainter beat lines
    for (int i = 0; grid.getX() + i * bw < getWidth(); ++i)
    {
        const float x = static_cast<float> (grid.getX()) + i * bw;
        g.setColour (t.colour ("line"));
        g.fillRect (juce::Rectangle<float> (x, static_cast<float> (grid.getY()), 1.0f, static_cast<float> (grid.getHeight())));
        g.setColour (t.colour ("line").withAlpha (0.4f));
        for (int b = 1; b < beats; ++b)
            g.fillRect (juce::Rectangle<float> (x + bw * b / beats, static_cast<float> (grid.getY()), 1.0f, static_cast<float> (grid.getHeight())));
    }

    // Playhead
    const int x = playheadX();
    g.setColour (live.playing ? t.colour ("playhead") : t.colour ("playhead").withAlpha (0.6f));
    g.fillRect (x, 30, 2, getHeight() - 30);
    juce::Path cap;
    cap.addTriangle (static_cast<float> (x) - 5.0f, 30.0f, static_cast<float> (x) + 7.0f, 30.0f, static_cast<float> (x) + 1.0f, 38.0f);
    g.fillPath (cap);

    // Empty state, or a note about views that come later
    const char* titles[] = { "The timeline is ready", "Score view", "Beat view", "Clip grid" };
    const char* bodies[] = {
        "Press Space to play the metronome. Drag the tempo or click the meter to change them.\nClips, recording and the piano roll come in Phase 1.",
        "Notation arrives in Phase 4 (read-only) and Phase 5 (editing).",
        "The drum machine and slicer arrive in Phase 2.",
        "Clip launching arrives in Phase 2.",
    };
    auto card = grid.withSizeKeepingCentre (juce::jmin (grid.getWidth() - 48, 520), 112).toFloat();
    g.setColour (t.colour ("panelRaised").withAlpha (0.92f));
    g.fillRoundedRectangle (card, t.metric ("radius", 8.0f) + 2.0f);
    g.setColour (t.colour ("lineStrong"));
    g.drawRoundedRectangle (card.reduced (0.5f), t.metric ("radius", 8.0f) + 2.0f, 1.0f);
    auto text = card.reduced (24.0f, 20.0f);
    g.setColour (t.colour ("text"));
    g.setFont (t.font (16.0f, true));
    g.drawText (titles[view], text.removeFromTop (24.0f), juce::Justification::centredLeft);
    g.setColour (t.colour ("textMuted"));
    g.setFont (t.font (13.5f));
    g.drawFittedText (bodies[view], text.toNearestInt().withTrimmedTop (6), juce::Justification::topLeft, 3);
}

void TimelineView::mouseDown (const juce::MouseEvent& e)
{
    if (e.y > 56 || onLocate == nullptr)
        return;
    const double bars = static_cast<double> (firstBar) + (e.x - gridArea().getX()) / barWidth();
    // Snap to the nearest beat.
    const auto beat = song.timeSignature.ticksPerBeat();
    const auto tick = static_cast<Tick> (std::round (bars * static_cast<double> (song.timeSignature.ticksPerBar()) / static_cast<double> (beat))) * beat;
    onLocate (juce::jmax<Tick> (0, tick));
}

// ---------------------------------------------------------------------------
Inspector::Inspector (Song& s, const LiveState& l) : song (s), live (l)
{
    addAndMakeVisible (titleEditor);
    titleEditor.setIndents (10, 7);
    titleEditor.onTextChange = [this] {
        song.title = titleEditor.getText().toStdString();
        if (onSongEdited)
            onSongEdited();
    };
    titleEditor.onReturnKey = [this] { unfocusAllComponents(); };

    addAndMakeVisible (keyBox);
    const juce::StringArray tonics { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    int id = 1;
    for (const auto& mode : { "major", "minor" })
        for (const auto& k : tonics)
            keyBox.addItem (k + " " + mode, id++);
    keyBox.onChange = [this] {
        song.key = keyBox.getText().toStdString();
        if (onSongEdited)
            onSongEdited();
    };

    for (auto* b : { &clickToggle, &accentToggle })
    {
        addAndMakeVisible (b);
        b->onClick = [this] {
            song.metronome.enabled = clickToggle.getToggleState();
            song.metronome.accentDownbeat = accentToggle.getToggleState();
            if (onSongEdited)
                onSongEdited();
        };
    }

    addAndMakeVisible (levelSlider);
    levelSlider.setRange (0.0, 1.0);
    levelSlider.onValueChange = [this] {
        song.metronome.level = static_cast<float> (levelSlider.getValue());
        if (onSongEdited)
            onSongEdited();
    };

    addAndMakeVisible (audioButton);
    audioButton.onClick = [this] {
        if (onAudioSettings)
            onAudioSettings();
    };

    songChanged();
}

void Inspector::songChanged()
{
    titleEditor.setText (utf8 (song.title), false);
    keyBox.setText (utf8 (song.key), juce::dontSendNotification);
    clickToggle.setToggleState (song.metronome.enabled, juce::dontSendNotification);
    accentToggle.setToggleState (song.metronome.accentDownbeat, juce::dontSendNotification);
    levelSlider.setValue (song.metronome.level, juce::dontSendNotification);
    titleEditor.setFont (currentTheme().font (14.0f));
    repaint();
}

void Inspector::refresh()
{
    juce::String audio;
    if (live.audio.running)
        audio = utf8 (live.audio.device) + "\n" + juce::String (live.audio.sampleRate / 1000.0, 1) + " kHz" + dot
              + juce::String (live.audio.bufferSize) + " samples" + dot + juce::String (live.audio.outputLatencyMs, 1) + " ms";
    else
        audio = "No audio device open";

    juce::String midi;
    for (const auto& in : live.midiInputs)
        midi << (in.enabled ? "1" : "0") << in.name << "\t" << in.lastMessage << "\n";

    const int beat = live.playing ? toBarBeat (static_cast<Tick> (live.positionTicks), song.timeSignature).beat : -1;
    clickToggle.setToggleState (song.metronome.enabled, juce::dontSendNotification);

    if (audio != shownAudio)
    {
        shownAudio = audio;
        repaint (audioInfo);
    }
    if (midi != shownMidi || live.midiActiveRecently (juce::Time::getMillisecondCounterHiRes(), 400.0))
    {
        shownMidi = midi;
        repaint (midiInfo);
    }
    if (beat != shownBeat)
    {
        shownBeat = beat;
        repaint (beatLights);
    }
}

void Inspector::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    sectionLabel (g, "Project", projectLabel);
    sectionLabel (g, "Key", keyLabel);
    sectionLabel (g, "Metronome", metronomeLabel);
    sectionLabel (g, "Click level", levelLabel);
    sectionLabel (g, "Audio output", audioLabel);
    sectionLabel (g, "MIDI inputs", midiLabel);

    // Beat lights: one per beat in the bar, the current one lit.
    const int beats = juce::jmin (song.timeSignature.numerator, 12);
    const float size = 10.0f, gap = 6.0f;
    for (int i = 0; i < beats; ++i)
    {
        const auto r = juce::Rectangle<float> (beatLights.getX() + i * (size + gap), beatLights.getCentreY() - size / 2, size, size);
        const bool lit = shownBeat == i + 1;
        g.setColour (lit ? (i == 0 ? t.colour ("accent") : t.colour ("text")) : t.colour ("lineStrong"));
        g.fillEllipse (r);
    }

    // Audio
    g.setColour (live.audio.running ? t.colour ("text") : t.colour ("textMuted"));
    g.setFont (t.font (13.5f));
    g.drawFittedText (shownAudio, audioInfo, juce::Justification::topLeft, 3);

    // MIDI inputs with activity dots
    auto r = midiInfo;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (live.midiInputs.empty())
    {
        g.setColour (t.colour ("textMuted"));
        g.drawFittedText ("No MIDI inputs found. Plug in a keyboard, then open Audio and MIDI settings.", r, juce::Justification::topLeft, 3);
    }
    for (const auto& in : live.midiInputs)
    {
        auto row = r.removeFromTop (40);
        if (row.getBottom() > midiInfo.getBottom())
            break;
        const bool active = in.enabled && now - in.lastTimeMs < 250.0;
        g.setColour (active ? t.colour ("ok") : (in.enabled ? t.colour ("ok").withAlpha (0.45f) : t.colour ("lineStrong")));
        g.fillEllipse (row.removeFromLeft (14).withSizeKeepingCentre (8, 8).withY (row.getY() + 6).toFloat());
        row.removeFromLeft (4);
        g.setColour (in.enabled ? t.colour ("text") : t.colour ("textDim"));
        g.setFont (t.font (13.5f));
        g.drawText (in.name, row.removeFromTop (20), juce::Justification::centredLeft, true);
        g.setColour (t.colour ("textMuted"));
        g.setFont (t.font (12.0f));
        g.drawText (in.enabled ? (in.lastMessage.isEmpty() ? juce::String ("Waiting for notes") : in.lastMessage) : juce::String ("Off"),
                    row, juce::Justification::topLeft, true);
    }
}

void Inspector::resized()
{
    auto r = getLocalBounds().reduced (20, 8);
    auto take = [&r] (int h) { return r.removeFromTop (h); };

    projectLabel = take (28);
    take (6);
    titleEditor.setBounds (take (34));
    keyLabel = take (26);
    take (6);
    keyBox.setBounds (take (34));

    metronomeLabel = take (34);
    take (6);
    auto clickRow = take (28);
    clickToggle.setBounds (clickRow);
    clickToggle.setButtonText ("Click on");
    beatLights = clickRow.withTrimmedLeft (90).withWidth (120);
    take (4);
    accentToggle.setBounds (take (28));
    levelLabel = take (26);
    levelSlider.setBounds (take (26));

    audioLabel = take (34);
    take (6);
    audioInfo = take (40);
    take (6);
    audioButton.setBounds (take (34));

    midiLabel = take (34);
    take (8);
    midiInfo = r;
}

// ---------------------------------------------------------------------------
StatusBar::StatusBar (const LiveState& l) : live (l) {}

void StatusBar::refresh()
{
    juce::String s;
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (const auto& in : live.midiInputs)
        s << in.name << (in.enabled ? (now - in.lastTimeMs < 250.0 ? "*" : "+") : "-");
    if (s != shown)
    {
        shown = s;
        repaint();
    }
}

void StatusBar::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    g.fillAll (t.colour ("panel"));
    g.setColour (t.colour ("line"));
    g.fillRect (getLocalBounds().removeFromTop (1));

    auto r = getLocalBounds().reduced (16, 0);
    g.setFont (t.font (12.5f));

    const auto hint = juce::String::fromUTF8 ("Space Play \xc2\xb7 C Metronome \xc2\xb7 Home Start");
    g.setColour (t.colour ("textDim"));
    g.drawText (hint, r.removeFromRight (280), juce::Justification::centredRight);

    const double now = juce::Time::getMillisecondCounterHiRes();
    if (live.midiInputs.empty())
    {
        g.setColour (t.colour ("textDim"));
        g.drawText ("No MIDI devices", r, juce::Justification::centredLeft);
    }
    for (const auto& in : live.midiInputs)
    {
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), in.name) + 34.0f;
        auto item = r.removeFromLeft (static_cast<int> (w));
        if (item.getRight() > r.getRight() + static_cast<int> (w))
            break;
        const bool active = now - in.lastTimeMs < 250.0;
        g.setColour (! in.enabled ? t.colour ("lineStrong") : active ? t.colour ("ok") : t.colour ("ok").withAlpha (0.5f));
        g.fillEllipse (item.removeFromLeft (14).withSizeKeepingCentre (7, 7).toFloat());
        g.setColour (in.enabled ? t.colour ("textMuted") : t.colour ("textDim"));
        g.drawText (in.name, item, juce::Justification::centredLeft);
    }
}

} // namespace arranger::ui

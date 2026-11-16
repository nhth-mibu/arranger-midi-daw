#include "Panels.h"

#include "arranger/core/GeneralMidi.h"
#include "arranger/core/Notation.h"
#include "arranger/core/Theory.h"

#include <map>

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
    // A flatter, modern panel: much less corner rounding and a lighter edge than a "card".
    const float radius = t.metric ("radius", 8.0f) * 0.7f;
    g.setColour (t.colour ("panel"));
    g.fillRoundedRectangle (r, radius);
    g.setColour (t.colour ("line").withAlpha (0.72f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

// ---------------------------------------------------------------------------
Inspector::Inspector (Session& s, const LiveState& l) : session (s), live (l)
{
    session.addChangeListener (this);

    // ---- track
    addAndMakeVisible (trackName);
    trackName.setIndents (10, 7);
    trackName.onTextChange = [this] {
        const auto id = session.selectedTrack();
        const auto name = trackName.getText().toStdString();
        session.edit ("Rename Track", [id, name] (Song& song) {
            if (auto* t = edit::findTrack (song, id))
                t->name = name;
        }, "rename-" + std::to_string (id));
    };
    trackName.onReturnKey = [this] { session.endMerge(); unfocusAllComponents(); };
    trackName.onFocusLost = [this] { session.endMerge(); };

    addAndMakeVisible (outputBox);
    outputBox.setTooltip ("Where this track plays: the built-in SoundFont, the drum sampler (your own audio files) or a MIDI port (your keyboard or synth)");
    outputBox.onChange = [this] {
        const int id = outputBox.getSelectedId();
        if (id <= 0 || id == 4)
            return;
        TrackOutput out;
        const auto* t = edit::findTrack (session.song(), session.selectedTrack());
        if (t == nullptr)
            return;
        out.channel = t->output.channel;
        if (id == 1)
            out.kind = TrackOutput::Kind::none;
        else if (id == 2)
            out.kind = TrackOutput::Kind::soundFont;
        else if (id == 3)
            out.kind = TrackOutput::Kind::sampler;
        else if (id == 5)
            out.kind = TrackOutput::Kind::plugin;
        else
        {
            out.kind = TrackOutput::Kind::midiPort;
            out.port = outputBox.getItemText (outputBox.indexOfItemId (id)).toStdString();
        }
        const auto track = t->id;
        session.edit ("Change Output", [track, out] (Song& song) {
            if (auto* tr = edit::findTrack (song, track))
                tr->output = out;
        });
    };

    addAndMakeVisible (channelBox);
    for (int ch = 1; ch <= 16; ++ch)
        channelBox.addItem ("Ch " + juce::String (ch), ch);
    channelBox.setTooltip ("MIDI channel. Channel 10 plays drums on GM sound sets.");
    channelBox.onChange = [this] {
        const int ch = channelBox.getSelectedId();
        const auto track = session.selectedTrack();
        if (ch > 0)
            session.edit ("Change Channel", [track, ch] (Song& song) {
                if (auto* tr = edit::findTrack (song, track))
                    tr->output.channel = ch;
            });
    };

    addAndMakeVisible (soundButton);
    soundButton.setTooltip ("Choose the sound (program change and bank)");
    soundButton.onClick = [this] {
        const auto* t = edit::findTrack (session.song(), session.selectedTrack());
        if (t != nullptr && t->output.kind == TrackOutput::Kind::plugin)
        {
            if (onOpenInstrument)
                onOpenInstrument();
        }
        else if (t != nullptr && t->output.kind == TrackOutput::Kind::sampler)
            showKitMenu();
        else
            showSoundMenu();
    };

    addAndMakeVisible (pluginsButton);
    pluginsButton.setTooltip ("The plugins on this track: its instrument and its effects");
    pluginsButton.onClick = [this] {
        if (onShowPlugins)
            onShowPlugins (&pluginsButton);
    };

    addChildComponent (monitorButton);
    monitorButton.setClickingTogglesState (true);
    monitorButton.setTooltip ("Hear the audio input of this track while you play or record. Use headphones: with speakers you get feedback.");
    monitorButton.onClick = [this] { session.setInputMonitor (monitorButton.getToggleState()); };
    addAndMakeVisible (soundFontButton);
    soundFontButton.setTooltip ("Load an SF2 SoundFont for the built-in instrument");
    soundFontButton.onClick = [this] {
        if (onChooseSoundFont)
            onChooseSoundFont();
    };

    // ---- project
    addAndMakeVisible (titleEditor);
    titleEditor.setIndents (10, 7);
    titleEditor.onTextChange = [this] {
        const auto title = titleEditor.getText().toStdString();
        session.edit ("Rename Song", [title] (Song& song) { song.title = title; }, "song-title");
    };
    titleEditor.onReturnKey = [this] { session.endMerge(); unfocusAllComponents(); };
    titleEditor.onFocusLost = [this] { session.endMerge(); };

    addAndMakeVisible (keyBox);
    const juce::StringArray tonics { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    int id = 1;
    for (const auto& mode : { "major", "minor" })
        for (const auto& k : tonics)
            keyBox.addItem (k + " " + mode, id++);
    keyBox.onChange = [this] {
        const auto key = keyBox.getText().toStdString();
        session.edit ("Change Key", [key] (Song& song) { song.key = key; });
    };
    addAndMakeVisible (findKeyButton);
    findKeyButton.setTooltip ("Find the key from the notes of the song (drums left out)");
    findKeyButton.onClick = [this] {
        const auto& song = session.song();
        std::vector<Note> notes;
        for (const auto& t : song.tracks)
            if (! notation::isDrumTrack (t))
            {
                const auto n = notation::timelineNotes (t);
                notes.insert (notes.end(), n.begin(), n.end());
            }
        const auto found = theory::findKey (notes, 0, std::max<Tick> (song.contentEnd(), 1));
        if (! found)
        {
            findKeyButton.setTooltip ("Too few notes to tell the key");
            return;
        }
        const auto key = theory::name (found->key);
        session.edit ("Find Key", [key] (Song& s) { s.key = key; });
    };

    for (auto* b : { &clickToggle, &accentToggle })
    {
        addAndMakeVisible (b);
        b->onClick = [this] {
            const bool on = clickToggle.getToggleState(), accent = accentToggle.getToggleState();
            session.edit ("Metronome", [on, accent] (Song& song) {
                song.metronome.enabled = on;
                song.metronome.accentDownbeat = accent;
            });
        };
    }

    addAndMakeVisible (levelSlider);
    levelSlider.setRange (0.0, 1.0);
    levelSlider.onValueChange = [this] {
        const auto level = static_cast<float> (levelSlider.getValue());
        session.edit ("Click Level", [level] (Song& song) { song.metronome.level = level; }, "click-level");
    };
    levelSlider.onDragEnd = [this] { session.endMerge(); };

    addAndMakeVisible (audioButton);
    audioButton.onClick = [this] {
        if (onAudioSettings)
            onAudioSettings();
    };

    songChanged();
}

Inspector::~Inspector() { session.removeChangeListener (this); }

void Inspector::setOutputPorts (const juce::StringArray& names)
{
    if (names == outputPorts)
        return;
    outputPorts = names;
    songChanged();
}

void Inspector::songChanged()
{
    const auto& song = session.song();
    const auto* t = edit::findTrack (song, session.selectedTrack());

    for (auto* c : std::initializer_list<juce::Component*> { &trackName, &outputBox, &channelBox, &soundButton })
        c->setEnabled (t != nullptr);

    if (t != nullptr)
    {
        if (! trackName.hasKeyboardFocus (true))
            trackName.setText (utf8 (t->name), false);

        const bool audioTrack = t->output.kind == TrackOutput::Kind::audio;
        outputBox.clear (juce::dontSendNotification);
        if (audioTrack)
            outputBox.addItem ("Audio", 4); // an audio track plays its files and nothing else
        else
        {
            outputBox.addItem ("No output", 1);
            outputBox.addItem ("Built-in SoundFont", 2);
            outputBox.addItem ("Drum sampler", 3);
            if (session.pluginHost() != nullptr)
                outputBox.addItem ("Plugin instrument", 5);
        }
        outputBox.setEnabled (! audioTrack);
        soundButton.setEnabled (! audioTrack);
        juce::StringArray ports = outputPorts;
        if (t->output.kind == TrackOutput::Kind::midiPort && ! ports.contains (utf8 (t->output.port)))
            ports.add (utf8 (t->output.port)); // saved port that isn't plugged in right now
        if (audioTrack)
            ports.clear();
        if (! ports.isEmpty())
            outputBox.addSectionHeading ("MIDI outputs");
        for (int i = 0; i < ports.size(); ++i)
            outputBox.addItem (ports[i], 10 + i);
        int selected = 1;
        if (audioTrack)
            selected = 4;
        else if (t->output.kind == TrackOutput::Kind::soundFont)
            selected = 2;
        else if (t->output.kind == TrackOutput::Kind::sampler)
            selected = 3;
        else if (t->output.kind == TrackOutput::Kind::plugin)
            selected = 5;
        else if (t->output.kind == TrackOutput::Kind::midiPort)
            selected = 10 + ports.indexOf (utf8 (t->output.port));
        outputBox.setSelectedId (selected, juce::dontSendNotification);
        channelBox.setSelectedId (t->output.channel, juce::dontSendNotification);
        channelBox.setEnabled (t->output.kind != TrackOutput::Kind::sampler && ! audioTrack);
        // An audio track has no MIDI channel: the switch to hear the input is in its place.
        channelBox.setVisible (! audioTrack);
        monitorButton.setVisible (audioTrack);
        monitorButton.setToggleState (session.inputMonitor(), juce::dontSendNotification);
        const auto effectCount = Session::canHaveEffects (*t) ? t->effects.size() : 0;
        pluginsButton.setButtonText (effectCount == 0 ? juce::String ("Effects...") : "Effects (" + juce::String (effectCount) + ")...");
        pluginsButton.setVisible (session.pluginHost() != nullptr);
        if (t->output.kind == TrackOutput::Kind::plugin)
            soundButton.setButtonText (t->instrument.empty() ? juce::String ("Choose instrument...")
                                                             : utf8 (t->instrument.name) + (session.pluginAt (t->id, -1) == nullptr ? " (not loaded)" : ""));
        else if (audioTrack)
            soundButton.setButtonText (t->audio.empty() ? juce::String ("No audio clips") : juce::String (t->audio.size()) + (t->audio.size() == 1 ? " audio clip" : " audio clips"));
        else if (t->output.kind == TrackOutput::Kind::sampler)
            soundButton.setButtonText (t->kit.pads.empty() ? juce::String ("Load samples...")
                                                           : (t->kit.name.empty() ? juce::String ("Kit") : utf8 (t->kit.name)) + " (" + juce::String (t->kit.pads.size()) + " pads)");
        else
        soundButton.setButtonText (t->sound.name.empty() ? (t->sound.program >= 0 ? "Program " + juce::String (t->sound.program + 1) : juce::String ("Choose sound...")) : utf8 (t->sound.name));
    }
    else
    {
        trackName.setText ({}, false);
        soundButton.setButtonText ("No track selected");
    }

    if (! titleEditor.hasKeyboardFocus (true))
        titleEditor.setText (utf8 (song.title), false);
    keyBox.setText (utf8 (song.key), juce::dontSendNotification);
    clickToggle.setToggleState (song.metronome.enabled, juce::dontSendNotification);
    accentToggle.setToggleState (song.metronome.accentDownbeat, juce::dontSendNotification);
    levelSlider.setValue (song.metronome.level, juce::dontSendNotification);
    titleEditor.setFont (currentTheme().font (14.0f));
    trackName.setFont (currentTheme().font (14.0f));
    repaint();
}

void Inspector::showKitMenu()
{
    const auto* t = edit::findTrack (session.song(), session.selectedTrack());
    if (t == nullptr)
        return;
    const auto track = t->id;

    juce::PopupMenu menu;
    menu.addItem ("Load samples...", [this] {
        if (onLoadKitSamples)
            onLoadKitSamples();
    });
    menu.addItem ("Slice a loop...", [this] {
        if (onSliceLoop)
            onSliceLoop();
    });
    if (! t->kit.pads.empty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("Pads (click to hear)");
        for (const auto& p : t->kit.pads)
        {
            const bool lost = session.missingSamples().count (p.sampleFile) > 0;
            juce::PopupMenu sub;
            const int note = p.note;
            sub.addItem ("Play", [this, track, note] { session.previewNote (track, note, 110, 300); });
            for (const int group : { 0, 1, 2 })
                sub.addItem (group == 0 ? juce::String ("No choke group") : "Choke group " + juce::String (group), true, p.chokeGroup == group, [this, track, note, group] {
                    session.edit ("Change Choke Group", [=] (Song& song) {
                        if (auto* tr = edit::findTrack (song, track))
                            for (auto& pad : tr->kit.pads)
                                if (pad.note == note)
                                    pad.chokeGroup = group;
                    });
                });
            sub.addItem ("Remove pad", [this, track, note] {
                session.edit ("Remove Pad", [=] (Song& song) {
                    if (auto* tr = edit::findTrack (song, track))
                        std::erase_if (tr->kit.pads, [note] (const KitPad& pad) { return pad.note == note; });
                });
            });
            menu.addSubMenu (juce::MidiMessage::getMidiNoteName (p.note, true, true, 3) + "  " + utf8 (p.name) + (lost ? "  (file not found)" : ""), sub);
        }
        menu.addSeparator();
        menu.addItem ("Remove all pads", [this, track] {
            session.edit ("Clear Kit", [track] (Song& song) {
                if (auto* tr = edit::findTrack (song, track))
                    tr->kit = {};
            });
        });
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (soundButton));
}

void Inspector::showSoundMenu()
{
    const auto* t = edit::findTrack (session.song(), session.selectedTrack());
    if (t == nullptr)
        return;
    const auto track = t->id;
    auto choose = [this, track] (TrackSound sound) {
        session.edit ("Change Sound", [track, sound] (Song& song) {
            if (auto* tr = edit::findTrack (song, track))
                tr->sound = sound;
        });
        session.previewNote (track, 60, 100, 500);
    };

    juce::PopupMenu menu;
    const auto font = session.soundFont();
    if (t->output.kind == TrackOutput::Kind::soundFont && font != nullptr)
    {
        // The loaded SoundFont's own presets, grouped by bank.
        std::map<int, juce::PopupMenu> banks;
        for (const auto& p : font->presets())
        {
            TrackSound s;
            s.name = p.name;
            s.program = p.program;
            s.bankMsb = p.bank < 128 ? p.bank : -1;
            const bool ticked = t->sound.program == s.program && t->sound.bankMsb == s.bankMsb;
            banks[p.bank].addItem (juce::String (p.program + 1).paddedLeft ('0', 3) + "  " + utf8 (p.name), true, ticked, [choose, s] { choose (s); });
        }
        for (auto& [bank, sub] : banks)
            menu.addSubMenu (bank == 128 ? juce::String ("Drum kits (use channel 10)") : "Bank " + juce::String (bank), sub);
    }
    else
    {
        // The device's own sound list from its profile (General MIDI when none matches).
        const auto& profile = session.profileFor (*t);
        menu.addSectionHeader (utf8 (profile.name));
        for (const auto& group : profile.groups)
        {
            juce::PopupMenu sub;
            bool groupTicked = false;
            for (const auto& sound : group.sounds)
            {
                const auto s = profile.trackSound (group, sound);
                const bool ticked = t->sound.program == s.program && t->sound.bankMsb == s.bankMsb && t->sound.bankLsb == s.bankLsb;
                groupTicked = groupTicked || ticked;
                sub.addItem ((sound.program >= 0 ? juce::String (sound.program + 1) + "  " : juce::String()) + utf8 (sound.name), true, ticked,
                             [choose, s] { choose (s); });
            }
            menu.addSubMenu (utf8 (group.name), sub, true, nullptr, groupTicked);
        }
        if (t->output.kind == TrackOutput::Kind::midiPort && ! profile.reset.empty())
        {
            menu.addSeparator();
            const auto track = t->id;
            menu.addItem ("Reset " + utf8 (profile.name) + " (" + utf8 (profile.dialect).toUpperCase() + ")", [this, track] { session.resetDevice (track); });
        }
    }
    menu.addSeparator();
    menu.addItem ("Don't send a sound (keep the device's own)", [choose] { choose ({}); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (soundButton));
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

    const auto& song = session.song();
    const int beat = live.playing ? toBarBeat (static_cast<Tick> (live.positionTicks), song.timeSignature).beat : -1;

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
    if (session.soundFontStatus() != shownFont)
    {
        shownFont = session.soundFontStatus();
        repaint (fontInfo);
    }
}

void Inspector::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    const auto& song = session.song();
    sectionLabel (g, "Track", trackLabel);
    sectionLabel (g, "Song", projectLabel);
    sectionLabel (g, "Metronome", metronomeLabel);
    sectionLabel (g, "Audio output", audioLabel);
    sectionLabel (g, "MIDI inputs", midiLabel);

    g.setColour (t.colour ("textMuted"));
    g.setFont (t.font (12.0f));
    g.drawFittedText (shownFont, fontInfo, juce::Justification::centredLeft, 2);

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
    g.setFont (t.font (13.0f));
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
        auto row = r.removeFromTop (38);
        if (row.getBottom() > midiInfo.getBottom())
            break;
        const bool active = in.enabled && now - in.lastTimeMs < 250.0;
        g.setColour (active ? t.colour ("ok") : (in.enabled ? t.colour ("ok").withAlpha (0.45f) : t.colour ("lineStrong")));
        g.fillEllipse (row.removeFromLeft (14).withSizeKeepingCentre (8, 8).withY (row.getY() + 6).toFloat());
        row.removeFromLeft (4);
        g.setColour (in.enabled ? t.colour ("text") : t.colour ("textDim"));
        g.setFont (t.font (13.0f));
        g.drawText (in.name, row.removeFromTop (19), juce::Justification::centredLeft, true);
        g.setColour (t.colour ("textMuted"));
        g.setFont (t.font (12.0f));
        g.drawText (in.enabled ? (in.lastMessage.isEmpty() ? juce::String ("Waiting for notes") : in.lastMessage) : juce::String ("Off"),
                    row, juce::Justification::topLeft, true);
    }
}

void Inspector::resized()
{
    auto r = getLocalBounds().reduced (20, 4);
    auto take = [&r] (int h) { return r.removeFromTop (h); };

    trackLabel = take (26);
    take (6);
    trackName.setBounds (take (32));
    take (6);
    outputBox.setBounds (take (32));
    take (6);
    auto row = take (32);
    channelBox.setBounds (row.removeFromLeft (92));
    monitorButton.setBounds (channelBox.getBounds());
    row.removeFromLeft (6);
    soundButton.setBounds (row);
    take (6);
    if (pluginsButton.isVisible())
    {
        pluginsButton.setBounds (take (30));
        take (6);
    }
    auto fontRow = take (30);
    soundFontButton.setBounds (fontRow.removeFromRight (96));
    fontRow.removeFromRight (6);
    fontInfo = fontRow;

    projectLabel = take (32);
    take (6);
    titleEditor.setBounds (take (32));
    take (6);
    auto keyRow = take (32);
    findKeyButton.setBounds (keyRow.removeFromRight (56));
    keyRow.removeFromRight (6);
    keyBox.setBounds (keyRow);

    metronomeLabel = take (32);
    take (4);
    auto clickRow = take (26);
    clickToggle.setBounds (clickRow);
    clickToggle.setButtonText ("Click on");
    beatLights = clickRow.withTrimmedLeft (90).withWidth (120);
    auto accentRow = take (26);
    accentToggle.setBounds (accentRow.removeFromLeft (accentRow.getWidth() / 2 + 20));
    levelSlider.setBounds (accentRow);

    audioLabel = take (32);
    take (4);
    audioInfo = take (38);
    take (4);
    audioButton.setBounds (take (30));

    midiLabel = take (32);
    take (6);
    midiInfo = r;
}

// ---------------------------------------------------------------------------
StatusBar::StatusBar (const LiveState& l) : live (l)
{
    for (auto* b : { &zoomOutButton, &zoomInButton })
    {
        addAndMakeVisible (*b);
        b->setWantsKeyboardFocus (false);
    }
    zoomOutButton.setTooltip ("Shorter tracks or steps");
    zoomInButton.setTooltip ("Taller tracks or steps");
    zoomOutButton.onClick = [this] { if (onZoomVertical) onZoomVertical (1.0 / 1.2); };
    zoomInButton.onClick = [this] { if (onZoomVertical) onZoomVertical (1.2); };
}

void StatusBar::resized()
{
    zoomOutButton.setBounds (8, 3, 22, 22);
    zoomInButton.setBounds (34, 3, 22, 22);
}

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

void StatusBar::setActivity (const juce::String& text)
{
    if (text != activity)
    {
        activity = text;
        repaint();
    }
}

void StatusBar::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    g.fillAll (t.colour ("panel"));
    g.setColour (t.colour ("line"));
    g.fillRect (getLocalBounds().removeFromTop (1));

    auto r = getLocalBounds().reduced (16, 0).withTrimmedLeft (46);
    g.setFont (t.font (12.5f));

    const auto hint = activity.isNotEmpty() ? activity : juce::String::fromUTF8 ("Space Play \xc2\xb7 C Metronome \xc2\xb7 Home Start");
    g.setColour (t.colour (activity.isNotEmpty() ? "text" : "textDim"));
    g.drawText (hint, r.removeFromRight (activity.isNotEmpty() ? 420 : 280), juce::Justification::centredRight);

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

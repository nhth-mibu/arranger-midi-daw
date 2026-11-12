#include "MainComponent.h"

#include "arranger/core/MidiFile.h"
#include "BinaryData.h"
#include "arranger/core/ProjectFile.h"

#include <filesystem>

namespace arranger::ui
{

namespace
{
    const char* noteName (int n)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[n % 12];
    }

    juce::String describe (const platform::IncomingMidi& m)
    {
        const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        if (m.size == 0)
            return {};
        const int status = m.bytes[0] & 0xf0, channel = (m.bytes[0] & 0x0f) + 1;
        const auto ch = dot + "ch " + juce::String (channel);
        switch (status)
        {
            case 0x90:
                if (m.bytes[2] > 0)
                    return "Note " + juce::String (noteName (m.bytes[1])) + juce::String (m.bytes[1] / 12 - 1) + dot + "vel " + juce::String (m.bytes[2]) + ch;
                [[fallthrough]];
            case 0x80: return "Note off " + juce::String (noteName (m.bytes[1])) + juce::String (m.bytes[1] / 12 - 1) + ch;
            case 0xb0: return "CC " + juce::String (m.bytes[1]) + " = " + juce::String (m.bytes[2]) + ch;
            case 0xc0: return "Program " + juce::String (m.bytes[1] + 1) + ch;
            case 0xe0: return "Pitch bend " + juce::String (((m.bytes[2] << 7) | m.bytes[1]) - 8192) + ch;
            case 0xd0: return "Pressure " + juce::String (m.bytes[1]) + ch;
            default: return "MIDI " + juce::String::toHexString (m.bytes.data(), m.size);
        }
    }
}


namespace
{
    std::filesystem::path toPath (const juce::File& f)
    {
#if JUCE_WINDOWS
        return std::filesystem::path (f.getFullPathName().toWideCharPointer());
#else
        return std::filesystem::path (f.getFullPathName().toStdString());
#endif
    }

    bool isMidiFile (const juce::File& f) { return f.hasFileExtension ("mid;midi;kar;smf"); }

    void showError (const juce::String& title, const juce::String& text)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, text);
    }
}

MainComponent::MainComponent (Engine& e, platform::JuceDeviceHost& d, juce::PropertiesFile* s)
    : engine (e), devices (d), settings (s)
{
    if (settings != nullptr)
        setCurrentTheme (Theme::builtIn (settings->getBoolValue ("darkTheme", true)));
    lookAndFeel.applyTheme();
    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    for (auto* c : std::initializer_list<juce::Component*> { &header, &trackList, &arrangement, &mixer, &pianoRoll, &inspector, &statusBar })
        addAndMakeVisible (c);
    mixer.setVisible (false);

    geometry.onChange = [this] {
        trackList.repaint();
        arrangement.repaint();
    };

    header.playButton.onClick = [this] { session.togglePlay(); };
    header.stopButton.onClick = [this] { session.stop(); };
    header.recordButton.onClick = [this] { session.toggleRecord(); };
    header.loopButton.onClick = [this] {
        session.edit ("Loop", [] (Song& song) { song.loop.enabled = ! song.loop.enabled; });
    };
    header.clickButton.onClick = [this] {
        session.edit ("Metronome", [] (Song& song) { song.metronome.enabled = ! song.metronome.enabled; });
    };
    header.lcd.onTempoChange = [this] (double bpm) {
        session.edit ("Change Tempo", [bpm] (Song& song) { song.tempo.set (0, clampTempo (bpm)); }, "tempo");
    };
    header.lcd.onMeterChange = [this] (TimeSignature sig) {
        session.edit ("Change Time Signature", [sig] (Song& song) { song.timeSignature = sig; });
    };
    header.tabs.onChange = [this] (int index) { showView (index); };

    inspector.onAudioSettings = [this] { showAudioSettings(); };
    inspector.onChooseSoundFont = [this] { chooseSoundFont(); };

    session.onError = [] (const juce::String& text) { showError ("Arranger MIDI DAW", text); };
    session.addChangeListener (this);

    commands.registerAllCommandsForTarget (this);
    commands.setFirstCommandTarget (this);
    addKeyListener (commands.getKeyMappings());
    setWantsKeyboardFocus (true);

    loadDeviceProfiles();

    // Start with one SoundFont track so a new project plays right away.
    Song song;
    edit::addTrack (song, "Piano");
    session.load (std::move (song));
    if (settings != nullptr)
        if (const juce::File font (settings->getValue ("soundFont")); font.existsAsFile())
            session.loadSoundFont (font);

    updateTitle();
    pollLiveState();
    setSize (1440, 900);
    startTimerHz (4);
}

MainComponent::~MainComponent()
{
    session.removeChangeListener (this);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    header.refresh();
    updateTitle();
    commands.commandStatusChanged();
    const bool editorOpen = session.editedClip() != 0 && view == 0;
    if (editorOpen != pianoRoll.isVisible())
        resized();
}

bool MainComponent::openFile (const juce::File& file, bool showErrors)
{
    try
    {
        if (isMidiFile (file))
        {
            auto song = midi_file::load (toPath (file));
            if (song.title.empty())
                song.title = file.getFileNameWithoutExtension().toStdString();
            session.load (std::move (song));
            session.doc().markSaved();
        }
        else
        {
            session.load (project_file::load (file.getFullPathName().toStdString()), file);
            session.doc().markSaved();
            if (settings != nullptr)
                settings->setValue ("lastProject", file.getFullPathName());
        }
        updateTitle();
        return true;
    }
    catch (const std::exception& error)
    {
        if (showErrors)
            showError (isMidiFile (file) ? "Can't import MIDI file" : "Can't open project", juce::String::fromUTF8 (error.what()));
        return false;
    }
}

void MainComponent::setThemeDark (bool dark)
{
    setCurrentTheme (Theme::builtIn (dark));
    lookAndFeel.applyTheme();
    inspector.songChanged();
    sendLookAndFeelChange();
    if (settings != nullptr)
        settings->setValue ("darkTheme", dark);
    repaint();
}

void MainComponent::showView (int index)
{
    view = index;
    header.tabs.setSelected (index);
    resized();
    repaint();
}

void MainComponent::openFirstClipInEditor()
{
    for (const auto& t : session.song().tracks)
        if (! t.clips.empty())
        {
            session.selectTrack (t.id);
            session.selectClips ({ t.clips.front()->id });
            session.openInEditor (t.clips.front()->id);
            return;
        }
}

void MainComponent::setPreviewPosition (double ticks, bool playing)
{
    previewMode = true;
    session.setPreviewPosition (ticks);
    live.positionTicks = ticks;
    live.playing = playing;
    header.refresh();
    inspector.refresh();
}

void MainComponent::updateTitle()
{
    const auto& song = session.song();
    auto title = juce::String::fromUTF8 (song.title.c_str());
    if (session.file() != juce::File())
        title << " - " << session.file().getFileName();
    if (session.doc().isModified())
        title << " *";
    if (onTitleChanged)
        onTitleChanged (title);
}

void MainComponent::pollLiveState()
{
    if (! previewMode)
    {
        live.playing = engine.isPlaying();
        live.positionTicks = engine.positionTicks();
    }
    live.audio = devices.audioStatus();

    // MIDI inputs: keep activity per port.
    const auto ports = devices.midiInputs();
    std::vector<MidiInputActivity> updated;
    for (const auto& p : ports)
    {
        MidiInputActivity a;
        a.name = juce::String::fromUTF8 (p.name.c_str());
        a.enabled = p.enabled;
        for (const auto& old : live.midiInputs)
            if (old.name == a.name)
            {
                a.lastMessage = old.lastMessage;
                a.lastTimeMs = old.lastTimeMs;
            }
        updated.push_back (a);
    }
    live.midiInputs = std::move (updated);

    const double now = juce::Time::getMillisecondCounterHiRes();
    for (const auto& m : devices.takeIncomingMidi())
    {
        const auto port = juce::String::fromUTF8 (m.port.c_str());
        const auto text = describe (m);
        live.lastMidiMessage = text;
        live.lastMidiTimeMs = now;
        for (auto& in : live.midiInputs)
            if (in.name == port)
            {
                in.lastMessage = text;
                in.lastTimeMs = now;
            }
    }

    // MIDI outputs change rarely; look every few seconds.
    if (--outputPollCountdown <= 0)
    {
        outputPollCountdown = 12;
        juce::StringArray names;
        if (! previewMode)
            for (const auto& p : devices.midiOutputs())
                names.add (juce::String::fromUTF8 (p.name.c_str()));
        inspector.setOutputPorts (names);
    }
}

void MainComponent::timerCallback()
{
    pollLiveState();
    header.refresh();
    if (view == 0)
        arrangement.refresh();
    if (view == 1)
        mixer.refresh();
    if (pianoRoll.isVisible())
        pianoRoll.refresh();
    inspector.refresh();
    statusBar.refresh();

    // Fast only while something moves, so an idle app uses almost no CPU.
    const bool busy = live.playing || view == 1 || live.midiActiveRecently (juce::Time::getMillisecondCounterHiRes(), 1000.0);
    const int hz = busy ? 60 : 4;
    if (getTimerInterval() != 1000 / hz)
        startTimerHz (hz);
}

void MainComponent::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    g.fillAll (t.colour ("background"));
    drawCard (g, mainCard.toFloat());
    if (pianoRoll.isVisible())
        drawCard (g, editorCard.toFloat());
    drawCard (g, inspectorCard.toFloat());

    if (view >= 2)
    {
        static const char* const later[] = { "Score view arrives in Phase 4.", "The drum machine arrives in Phase 2.", "The clip launcher arrives in Phase 2." };
        g.setColour (t.colour ("textDim"));
        g.setFont (t.font (15.0f));
        g.drawText (later[view - 2], mainCard, juce::Justification::centred);
    }
}

void MainComponent::resized()
{
    const auto& t = currentTheme();
    auto r = getLocalBounds();
    header.setBounds (r.removeFromTop (static_cast<int> (t.metric ("headerHeight", 64.0f))));
    statusBar.setBounds (r.removeFromBottom (static_cast<int> (t.metric ("statusHeight", 28.0f))));

    r.reduce (12, 12);
    inspectorCard = r.removeFromRight (static_cast<int> (t.metric ("inspectorWidth", 296.0f)));
    r.removeFromRight (12);
    inspector.setBounds (inspectorCard.reduced (1, 8));

    const bool editorOpen = session.editedClip() != 0 && view == 0;
    pianoRoll.setVisible (editorOpen);
    if (editorOpen)
    {
        editorCard = r.removeFromBottom (juce::jlimit (240, 520, r.getHeight() * 45 / 100));
        r.removeFromBottom (12);
        pianoRoll.setBounds (editorCard.reduced (1));
    }
    mainCard = r;

    trackList.setVisible (view == 0);
    arrangement.setVisible (view == 0);
    mixer.setVisible (view == 1);

    const int sidebar = static_cast<int> (t.metric ("sidebarWidth", 248.0f));
    trackList.setBounds (mainCard.withWidth (sidebar).reduced (1));
    arrangement.setBounds (mainCard.withTrimmedLeft (sidebar).reduced (1).withTrimmedLeft (-1));
    mixer.setBounds (mainCard.reduced (1));
    repaint();
}

bool MainComponent::editorHasFocus() const
{
    const auto* focused = juce::Component::getCurrentlyFocusedComponent();
    return pianoRoll.isVisible() && focused != nullptr && (focused == &pianoRoll || pianoRoll.isParentOf (focused));
}

// ---- Commands ---------------------------------------------------------------
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& ids)
{
    for (int id = cmdNew; id <= cmdLightTheme; ++id)
        ids.add (id);
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    using KP = juce::KeyPress;
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    const auto& t = currentTheme();
    const auto& song = session.song();
    const bool hasClips = ! session.selectedClips().empty();

    switch (id)
    {
        case cmdNew: info.setInfo ("New", "New empty project", "File", 0); info.addDefaultKeypress ('n', cmd); break;
        case cmdOpen: info.setInfo ("Open...", "Open a project or MIDI file", "File", 0); info.addDefaultKeypress ('o', cmd); break;
        case cmdSave: info.setInfo ("Save", "Save the project", "File", 0); info.addDefaultKeypress ('s', cmd); break;
        case cmdSaveAs: info.setInfo ("Save As...", "Save under a new name", "File", 0); info.addDefaultKeypress ('s', cmd | shift); break;
        case cmdImportMidi: info.setInfo ("Import MIDI File...", "Open a Standard MIDI File", "File", 0); info.addDefaultKeypress ('i', cmd | shift); break;
        case cmdExportMidi: info.setInfo ("Export MIDI File...", "Save the song as a Standard MIDI File", "File", 0); info.addDefaultKeypress ('e', cmd | shift); break;
        case cmdChooseSoundFont: info.setInfo ("Choose SoundFont...", "Load an SF2 file for the built-in instrument", "File", 0); break;
        case cmdUndo:
            info.setInfo ("Undo " + juce::String (session.doc().undoDescription()), "Undo the last change", "Edit", 0);
            info.addDefaultKeypress ('z', cmd);
            info.setActive (session.doc().canUndo());
            break;
        case cmdRedo:
            info.setInfo ("Redo " + juce::String (session.doc().redoDescription()), "Redo", "Edit", 0);
            info.addDefaultKeypress ('z', cmd | shift);
            info.addDefaultKeypress ('y', cmd);
            info.setActive (session.doc().canRedo());
            break;
        case cmdDelete:
            info.setInfo ("Delete", "Delete the selected notes or clips", "Edit", 0);
            info.addDefaultKeypress (KP::deleteKey, 0);
            info.addDefaultKeypress (KP::backspaceKey, 0);
            break;
        case cmdDuplicate: info.setInfo ("Duplicate", "Copy the selected clips after themselves", "Edit", 0); info.addDefaultKeypress ('d', cmd); info.setActive (hasClips); break;
        case cmdSplit: info.setInfo ("Split at Playhead", "Cut the selected clips at the playhead", "Edit", 0); info.addDefaultKeypress ('e', cmd); info.setActive (hasClips); break;
        case cmdSelectAll: info.setInfo ("Select All", "Select every note, or every clip", "Edit", 0); info.addDefaultKeypress ('a', cmd); break;
        case cmdQuantise: info.setInfo ("Quantise", "Snap notes to the grid", "Edit", 0); info.addDefaultKeypress ('q', 0); break;
        case cmdQuantiseOnRecord:
            info.setInfo ("Quantise While Recording", "Quantise new takes as they are recorded", "Edit", 0);
            info.setTicked (session.quantiseOnRecord);
            break;
        case cmdAddTrack: info.setInfo ("Add Track", "Add a track after the selected one", "Track", 0); info.addDefaultKeypress ('t', cmd); break;
        case cmdPlayStop: info.setInfo ("Play / Stop", "Start or stop playback", "Transport", 0); info.addDefaultKeypress (KP::spaceKey, 0); break;
        case cmdGoToStart: info.setInfo ("Go to Start", "Move the playhead to bar 1", "Transport", 0); info.addDefaultKeypress (KP::homeKey, 0); info.addDefaultKeypress ('\r', 0); break;
        case cmdRecord:
            info.setInfo ("Record", "Record on the armed (or selected) track", "Transport", 0);
            info.addDefaultKeypress ('r', 0);
            info.setTicked (session.isRecording());
            break;
        case cmdLoop:
            info.setInfo ("Loop", "Loop between the locators", "Transport", 0);
            info.addDefaultKeypress ('l', 0);
            info.setTicked (song.loop.enabled);
            break;
        case cmdToggleMetronome:
            info.setInfo ("Metronome", "Turn the click on or off", "Transport", 0);
            info.addDefaultKeypress ('c', 0);
            info.setTicked (song.metronome.enabled);
            break;
        case cmdShowTimeline: info.setInfo ("Timeline", "Tracks and clips", "View", 0); info.addDefaultKeypress ('1', cmd); info.setTicked (view == 0); break;
        case cmdShowMixer: info.setInfo ("Mixer", "Faders, pan and sends", "View", 0); info.addDefaultKeypress ('2', cmd); info.setTicked (view == 1); break;
        case cmdCloseEditor: info.setInfo ("Close Note Editor", "Hide the piano roll", "View", 0); info.addDefaultKeypress (KP::escapeKey, 0); info.setActive (session.editedClip() != 0); break;
        case cmdAudioSettings: info.setInfo ("Audio and MIDI Settings...", "Choose devices", "Options", 0); info.addDefaultKeypress (',', cmd); break;
        case cmdDeviceFolder: info.setInfo ("Show Device Profiles Folder", "Add your own device profiles (YAML) here", "Options", 0); break;
        case cmdDarkTheme: info.setInfo ("Dark Theme", "Studio dark", "View", 0); info.setTicked (t.isDark); break;
        case cmdLightTheme: info.setInfo ("Light Theme", "Studio light", "View", 0); info.setTicked (! t.isDark); break;
        default: break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    // Keys typed into a text field stay there.
    if (info.invocationMethod == InvocationInfo::fromKeyPress && dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        return false;

    switch (info.commandID)
    {
        case cmdNew:
            confirmDiscard ([this] {
                Song song;
                edit::addTrack (song, "Piano");
                session.load (std::move (song));
                session.doc().markSaved();
                updateTitle();
            });
            return true;
        case cmdOpen:
            confirmDiscard ([this] {
                chooser = std::make_unique<juce::FileChooser> ("Open project or MIDI file", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                               juce::String ("*") + project_file::kExtension + ";*.mid;*.midi");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this] (const juce::FileChooser& fc) {
                                          if (fc.getResult() != juce::File())
                                              openFile (fc.getResult(), true);
                                      });
            });
            return true;
        case cmdSave: save (false); return true;
        case cmdSaveAs: save (true); return true;
        case cmdImportMidi: importMidi(); return true;
        case cmdExportMidi: exportMidi(); return true;
        case cmdChooseSoundFont: chooseSoundFont(); return true;
        case cmdUndo: session.undo(); return true;
        case cmdRedo: session.redo(); return true;
        case cmdDelete:
            if (editorHasFocus())
                pianoRoll.deleteSelected();
            else
                arrangement.deleteSelected();
            return true;
        case cmdDuplicate: arrangement.duplicateSelected(); return true;
        case cmdSplit: arrangement.splitAtPlayhead(); return true;
        case cmdSelectAll:
            if (editorHasFocus())
                pianoRoll.selectAll();
            else
            {
                std::set<ClipId> all;
                for (const auto& t : session.song().tracks)
                    for (const auto& c : t.clips)
                        all.insert (c->id);
                session.selectClips (all);
            }
            return true;
        case cmdQuantise: quantise(); return true;
        case cmdQuantiseOnRecord: session.quantiseOnRecord = ! session.quantiseOnRecord; commands.commandStatusChanged(); return true;
        case cmdAddTrack: trackList.addTrack(); return true;
        case cmdPlayStop: session.togglePlay(); return true;
        case cmdGoToStart: session.locate (0); return true;
        case cmdRecord: session.toggleRecord(); return true;
        case cmdLoop: header.loopButton.onClick(); return true;
        case cmdToggleMetronome: header.clickButton.onClick(); return true;
        case cmdShowTimeline: showView (0); return true;
        case cmdShowMixer: showView (1); return true;
        case cmdCloseEditor: session.openInEditor (0); return true;
        case cmdAudioSettings: showAudioSettings(); return true;
        case cmdDeviceFolder:
            deviceFolder().createDirectory();
            deviceFolder().revealToUser();
            return true;
        case cmdDarkTheme: setThemeDark (true); return true;
        case cmdLightTheme: setThemeDark (false); return true;
        default: return false;
    }
}

void MainComponent::quantise()
{
    if (session.editedClip() != 0)
    {
        pianoRoll.quantiseNotes();
        return;
    }
    const auto clips = session.selectedClips();
    if (clips.empty())
        return;
    const auto settings = session.quantise;
    session.edit ("Quantise", [&] (Song& song) {
        for (const auto id : clips)
            edit::modifyClip (song, id, [&] (Clip& c) { arranger::quantise (c, settings); });
    });
}

juce::StringArray MainComponent::getMenuBarNames() { return { "File", "Edit", "Track", "Transport", "View", "Options" }; }

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    auto add = [&] (std::initializer_list<int> ids) {
        for (const int id : ids)
        {
            if (id == 0)
                m.addSeparator();
            else
                m.addCommandItem (&commands, id);
        }
    };
    switch (index)
    {
        case 0:
            add ({ cmdNew, cmdOpen, 0, cmdSave, cmdSaveAs, 0, cmdImportMidi, cmdExportMidi, 0, cmdChooseSoundFont });
#if ! JUCE_MAC
            m.addSeparator();
            m.addCommandItem (&commands, juce::StandardApplicationCommandIDs::quit);
#endif
            break;
        case 1: add ({ cmdUndo, cmdRedo, 0, cmdDelete, cmdDuplicate, cmdSplit, cmdSelectAll, 0, cmdQuantise, cmdQuantiseOnRecord }); break;
        case 2: add ({ cmdAddTrack }); break;
        case 3: add ({ cmdPlayStop, cmdGoToStart, cmdRecord, cmdLoop, cmdToggleMetronome }); break;
        case 4: add ({ cmdShowTimeline, cmdShowMixer, cmdCloseEditor, 0, cmdDarkTheme, cmdLightTheme }); break;
        case 5: add ({ cmdAudioSettings, cmdDeviceFolder }); break;
        default: break;
    }
    return m;
}

// ---- Files -------------------------------------------------------------------
void MainComponent::save (bool askForFile)
{
    auto writeTo = [this] (juce::File file) {
        file = file.withFileExtension (project_file::kExtension);
        try
        {
            project_file::save (session.song(), file.getFullPathName().toStdString());
            session.setFile (file);
            session.doc().markSaved();
            updateTitle();
            if (settings != nullptr)
                settings->setValue ("lastProject", file.getFullPathName());
        }
        catch (const std::exception& error)
        {
            showError ("Can't save project", juce::String::fromUTF8 (error.what()));
        }
    };

    if (! askForFile && session.file() != juce::File())
    {
        writeTo (session.file());
        return;
    }

    const auto start = session.file() != juce::File()
                         ? session.file()
                         : juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                               .getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (session.song().title.c_str())) + project_file::kExtension);
    chooser = std::make_unique<juce::FileChooser> ("Save project", start, juce::String ("*") + project_file::kExtension);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [writeTo] (const juce::FileChooser& fc) {
                              if (fc.getResult() != juce::File())
                                  writeTo (fc.getResult());
                          });
}

void MainComponent::importMidi()
{
    confirmDiscard ([this] {
        chooser = std::make_unique<juce::FileChooser> ("Import MIDI file", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.mid;*.midi;*.kar");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) {
                                  if (fc.getResult() != juce::File())
                                      openFile (fc.getResult(), true);
                              });
    });
}

void MainComponent::exportMidi()
{
    const auto base = session.file() != juce::File() ? session.file().getParentDirectory() : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto start = base.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (session.song().title.c_str())) + ".mid");
    chooser = std::make_unique<juce::FileChooser> ("Export MIDI file", start, "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              if (fc.getResult() == juce::File())
                                  return;
                              try
                              {
                                  midi_file::save (session.song(), toPath (fc.getResult().withFileExtension ("mid")));
                              }
                              catch (const std::exception& error)
                              {
                                  showError ("Can't export MIDI file", juce::String::fromUTF8 (error.what()));
                              }
                          });
}

void MainComponent::chooseSoundFont()
{
    const auto current = juce::File (juce::String::fromUTF8 (session.song().soundFont.c_str()));
    const auto start = current.existsAsFile() ? current : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Choose a SoundFont", start, "*.sf2;*.SF2");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              session.loadSoundFont (f);
                              if (settings != nullptr)
                                  settings->setValue ("soundFont", f.getFullPathName());
                          });
}

void MainComponent::confirmDiscard (std::function<void()> then)
{
    if (! session.doc().isModified())
    {
        then();
        return;
    }

    auto options = juce::MessageBoxOptions::makeOptionsYesNoCancel (juce::MessageBoxIconType::QuestionIcon, "Save changes?",
                                                                    "This project has unsaved changes.", "Save", "Don't Save", "Cancel", this);
    juce::AlertWindow::showAsync (options, [this, then] (int result) {
        if (result == 1)
        {
            save (false);
            if (! session.doc().isModified())
                then();
        }
        else if (result == 2)
        {
            then();
        }
    });
}

juce::File MainComponent::deviceFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Arranger MIDI DAW").getChildFile ("Devices");
}

void MainComponent::loadDeviceProfiles()
{
    DeviceLibrary library;
    juce::StringArray problems;

    // Built in first, so the user's own files with the same id replace them.
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const juce::String file (BinaryData::originalFilenames[i]);
        if (! file.endsWithIgnoreCase (".yaml"))
            continue;
        int size = 0;
        const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
        try
        {
            library.add (device_profile::parse (std::string (data, static_cast<std::size_t> (size))));
        }
        catch (const std::exception& e)
        {
            problems.add (file + ": " + juce::String::fromUTF8 (e.what()));
        }
    }

    for (const auto& f : deviceFolder().findChildFiles (juce::File::findFiles, false, "*.yaml;*.yml"))
    {
        try
        {
            library.add (device_profile::parse (f.loadFileAsString().toStdString()));
        }
        catch (const std::exception& e)
        {
            problems.add (f.getFileName() + ": " + juce::String::fromUTF8 (e.what()));
        }
    }

    session.setDeviceLibrary (std::move (library));
    if (! problems.isEmpty() && settings != nullptr)
        showError ("Some device profiles have mistakes", problems.joinIntoString ("\n"));
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (devices.deviceManager(), 0, 0, 2, 2, true, true, true, false);
    selector->setSize (520, 560);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio and MIDI Settings";
    o.dialogBackgroundColour = currentTheme().colour ("panel");
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    o.launchAsync();
}

} // namespace arranger::ui

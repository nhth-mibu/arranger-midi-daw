#include "MainComponent.h"
#include "../AudioFiles.h"

#include "arranger/core/Style.h"

#include "SlicerView.h"

#include "arranger/core/MidiFile.h"
#include "arranger/core/MusicXml.h"
#include "BinaryData.h"
#include "arranger/core/ProjectFile.h"

#include <filesystem>

namespace arranger::ui
{

namespace
{
    constexpr int kMixerMinHeight = 180; // room for the name, the buttons and a short fader
    constexpr int kViewMinHeight = 160;

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
    bool isMusicXmlFile (const juce::File& f) { return f.hasFileExtension ("musicxml;mxl;xml"); }

    /** The text of a MusicXML file. A .mxl file is a ZIP archive; its container file names the score inside. */
    std::string musicXmlText (const juce::File& file)
    {
        if (! file.hasFileExtension ("mxl"))
        {
            if (! file.existsAsFile())
                throw musicxml::Error ("Can't open " + file.getFullPathName().toStdString());
            return file.loadFileAsString().toStdString();
        }
        juce::ZipFile zip (file);
        juce::String inner;
        if (const auto* container = zip.getEntry ("META-INF/container.xml"))
            if (const std::unique_ptr<juce::InputStream> in { zip.createStreamForEntry (*container) })
                if (const auto xml = juce::parseXML (in->readEntireStreamAsString()))
                    if (const auto* rootfiles = xml->getChildByName ("rootfiles"))
                        if (const auto* rootfile = rootfiles->getChildByName ("rootfile"))
                            inner = rootfile->getStringAttribute ("full-path");
        if (inner.isEmpty())
            for (int i = 0; i < zip.getNumEntries(); ++i)
            {
                const auto name = zip.getEntry (i)->filename;
                if (! name.startsWithIgnoreCase ("META-INF") && (name.endsWithIgnoreCase (".xml") || name.endsWithIgnoreCase (".musicxml")))
                {
                    inner = name;
                    break;
                }
            }
        const auto* entry = inner.isNotEmpty() ? zip.getEntry (inner) : nullptr;
        const std::unique_ptr<juce::InputStream> in { entry != nullptr && entry->uncompressedSize < 200 * 1024 * 1024 ? zip.createStreamForEntry (*entry) : nullptr };
        if (in == nullptr)
            throw musicxml::Error ("This .mxl file has no score in it");
        return in->readEntireStreamAsString().toStdString();
    }

    bool isStyleFile (const juce::File& f) { return style_file::hasStyleExtension (f.getFileName().toStdString()); }

    void showError (const juce::String& title, const juce::String& text)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, text);
    }
}

MainComponent::MainComponent (Engine& e, platform::JuceDeviceHost& d, juce::PropertiesFile* s, PluginHost* p)
    : engine (e), devices (d), settings (s), pluginHost (p)
{
    session.setPluginHost (pluginHost);
    if (settings != nullptr)
        setCurrentTheme (Theme::builtIn (settings->getBoolValue ("darkTheme", true)));
    lookAndFeel.applyTheme();
    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    for (auto* c : std::initializer_list<juce::Component*> { &header, &trackList, &arrangement, &mixer, &pianoRoll, &inspector, &statusBar })
        addAndMakeVisible (c);
    addAndMakeVisible (mixerBar);
    if (settings != nullptr)
    {
        mixerShown = settings->getBoolValue ("mixerShown", true);
        mixerHeight = settings->getIntValue ("mixerHeight", mixerHeight);
    }
    mixerBar.onStart = [this] { mixerHeightAtDragStart = mixer.getHeight() + 2; };
    mixerBar.onDrag = [this] (int dy) { setMixerHeight (mixerHeightAtDragStart - dy); };
    mixerBar.onReset = [this] { setMixerHeight (280); };
    header.mixerButton.onClick = [this] { setMixerShown (! mixerShown); };
    addChildComponent (beat);
    addChildComponent (score);
    addChildComponent (clipGrid);

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
    inspector.onLoadKitSamples = [this] { loadKitSamples(); };
    inspector.onSliceLoop = [this] { sliceLoop(); };
    inspector.onShowPlugins = [this] (juce::Component* anchor) { showPlugins (session.selectedTrack(), anchor); };
    mixer.onShowEffects = [this] (TrackId id, juce::Component* anchor) { showPlugins (id, anchor); };
    inspector.onOpenInstrument = [this] {
        const auto* t = edit::findTrack (session.song(), session.selectedTrack());
        if (t == nullptr)
            return;
        if (! t->instrument.empty() && session.pluginAt (t->id, -1) != nullptr)
        {
            pluginWindows.show (t->id, -1);
            return;
        }
        const auto id = t->id;
        PluginRackPanel::showPluginMenu (session, true, &inspector, [this, id] (const PluginSlot& slot) {
            session.edit ("Choose Instrument", [&] (Song& song) {
                if (auto* tr = edit::findTrack (song, id))
                    tr->instrument = slot;
            });
            pluginWindows.show (id, -1);
        }, [this] { scanForPlugins(); });
    };

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
    if (slicerWindow != nullptr)
        delete slicerWindow.getComponent();
    session.removeChangeListener (this);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == commands.getKeyMappings())
    {
        // A shortcut changed: keep only the differences from the standard keys.
        if (settings != nullptr)
        {
            if (const auto xml = commands.getKeyMappings()->createXml (true))
                settings->setValue ("shortcuts", xml.get());
        }
        menuItemsChanged();
        return;
    }
    header.refresh();
    updateTitle();
    commands.commandStatusChanged();
    const bool editorOpen = session.editedClip() != 0 && (view == viewTimeline || view == viewClips);
    if (editorOpen != pianoRoll.isVisible())
        resized();
}

bool MainComponent::openFile (const juce::File& file, bool showErrors)
{
    try
    {
        if (isStyleFile (file))
        {
            session.load (style_convert::toSong (style_file::load (toPath (file))));
            session.doc().markSaved();
            showView (viewClips); // a style lives in the Clip grid
        }
        else if (isMusicXmlFile (file))
        {
            auto song = musicxml::read (musicXmlText (file));
            if (song.title == "Imported score")
                song.title = file.getFileNameWithoutExtension().toStdString();
            session.load (std::move (song));
            session.doc().markSaved();
            showView (viewScore);
        }
        else if (isMidiFile (file))
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
            showError (isStyleFile (file) ? "Can't open style file" : isMusicXmlFile (file) ? "Can't import MusicXML file" : (isMidiFile (file) ? "Can't import MIDI file" : "Can't open project"), juce::String::fromUTF8 (error.what()));
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

void MainComponent::setMixerShown (bool shown)
{
    mixerShown = shown;
    if (settings != nullptr)
        settings->setValue ("mixerShown", shown);
    commands.commandStatusChanged();
    resized();
}

void MainComponent::setMixerHeight (int height)
{
    mixerHeight = juce::jmax (kMixerMinHeight, height);
    if (settings != nullptr)
        settings->setValue ("mixerHeight", mixerHeight);
    resized();
}

void MainComponent::ResizeBar::paint (juce::Graphics& g)
{
    g.setColour (currentTheme().colour (isMouseOverOrDragging() ? "accent" : "lineStrong"));
    g.fillRoundedRectangle (getLocalBounds().toFloat().withSizeKeepingCentre (44.0f, 4.0f), 2.0f);
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
    for (const auto& m : session.takeMonitor())
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
    if (view == viewTimeline)
        arrangement.refresh();
    if (mixerShown)
        mixer.refresh();
    if (view == viewBeat)
        beat.refresh();
    if (view == viewClips)
        clipGrid.refresh();
    if (pianoRoll.isVisible())
        pianoRoll.refresh();
    inspector.refresh();
    statusBar.refresh();

    // Fast only while something moves, so an idle app uses almost no CPU.
    const bool busy = live.playing || live.midiActiveRecently (juce::Time::getMillisecondCounterHiRes(), mixerShown ? 4000.0 : 1000.0);
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
    if (mixerShown)
        drawCard (g, mixerCard.toFloat());
    drawCard (g, inspectorCard.toFloat());
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

    // The mixer panel sits under every view. It never takes more than the view can give.
    mixer.setVisible (mixerShown);
    mixerBar.setVisible (mixerShown);
    header.mixerButton.setToggleState (mixerShown, juce::dontSendNotification);
    if (mixerShown)
    {
        const int most = juce::jmax (kMixerMinHeight, r.getHeight() - kViewMinHeight);
        mixerCard = r.removeFromBottom (juce::jlimit (kMixerMinHeight, most, mixerHeight));
        mixerBar.setBounds (r.removeFromBottom (12));
        mixer.setBounds (mixerCard.reduced (1));
    }

    const bool editorOpen = session.editedClip() != 0 && (view == viewTimeline || view == viewClips);
    pianoRoll.setVisible (editorOpen);
    if (editorOpen)
    {
        editorCard = r.removeFromBottom (juce::jlimit (juce::jmin (240, r.getHeight() / 2), 520, r.getHeight() * 45 / 100));
        r.removeFromBottom (12);
        pianoRoll.setBounds (editorCard.reduced (1));
    }
    mainCard = r;

    trackList.setVisible (view == viewTimeline);
    arrangement.setVisible (view == viewTimeline);

    const int sidebar = static_cast<int> (t.metric ("sidebarWidth", 248.0f));
    trackList.setBounds (mainCard.withWidth (sidebar).reduced (1));
    arrangement.setBounds (mainCard.withTrimmedLeft (sidebar).reduced (1).withTrimmedLeft (-1));
    score.setVisible (view == viewScore);
    score.setBounds (mainCard.reduced (1));
    beat.setVisible (view == viewBeat);
    beat.setBounds (mainCard.reduced (1));
    clipGrid.setVisible (view == viewClips);
    clipGrid.setBounds (mainCard.reduced (1));
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
    for (int id = cmdNew; id <= cmdShortcuts; ++id)
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
        case cmdOpenStyle: info.setInfo ("Open Style File to Edit...", "Open a Yamaha style file in the Clip grid, where you edit its sections and parts", "Style", 0); break;
        case cmdSaveStyle: info.setInfo ("Save as Style File...", "Save the Clip grid as a Yamaha style file (.sty)", "Style", 0); info.setActive (song.style.has_value()); break;
        case cmdMakeStyle: info.setInfo ("Make the Clip Grid a Style", "Turn the tracks of the Clip grid into style parts that follow chords", "Style", 0); info.setActive (! song.style.has_value()); break;
        case cmdFillFromStyle: info.setInfo ("Fill Tracks from a Style...", "Write a section of a Yamaha style onto new tracks, for a number of bars and with your chords", "Style", 0); break;
        case cmdSectionFromLoop:
            info.setInfo ("Make a Style Section from the Loop Range...", "Copy the notes in the loop range of the Timeline to a section of the style", "Style", 0);
            info.setActive (song.loop.isValid());
            break;
        case cmdImportMidi: info.setInfo ("Import MIDI File...", "Open a Standard MIDI File", "File", 0); info.addDefaultKeypress ('i', cmd | shift); break;
        case cmdExportMidi: info.setInfo ("Export MIDI File...", "Save the song as a Standard MIDI File", "File", 0); info.addDefaultKeypress ('e', cmd | shift); break;
        case cmdImportMusicXml: info.setInfo ("Import MusicXML File...", "Open a score from a notation program", "File", 0); break;
        case cmdImportAudio: info.setInfo ("Import Audio File...", "Put an audio file on an audio track, at the playhead", "File", 0); break;
        case cmdExportMusicXml: info.setInfo ("Export MusicXML File...", "Save the score for a notation program", "File", 0); break;
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
        case cmdAddBus: info.setInfo ("Add Bus", "Add a bus to the mixer: a group for more tracks, or a return for a shared effect", "Track", 0); break;
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
        case cmdShowTimeline: info.setInfo ("Timeline", "Tracks and clips", "View", 0); info.addDefaultKeypress ('1', cmd); info.setTicked (view == viewTimeline); break;
        case cmdShowMixer: info.setInfo ("Mixer", "Show or hide the mixer under the view", "View", 0); info.addDefaultKeypress ('2', cmd); info.setTicked (mixerShown); break;
        case cmdShowBeat: info.setInfo ("Beat", "Step sequencer for the selected track", "View", 0); info.addDefaultKeypress ('4', cmd); info.setTicked (view == viewBeat); break;
        case cmdShowClips: info.setInfo ("Clips", "Clip grid: start loops and scenes", "View", 0); info.addDefaultKeypress ('5', cmd); info.setTicked (view == viewClips); break;
        case cmdCloseEditor: info.setInfo ("Close Note Editor", "Hide the piano roll", "View", 0); info.addDefaultKeypress (KP::escapeKey, 0); info.setActive (session.editedClip() != 0); break;
        case cmdAudioSettings: info.setInfo ("Audio and MIDI Settings...", "Choose devices", "Options", 0); info.addDefaultKeypress (',', cmd); break;
        case cmdScanPlugins: info.setInfo ("Scan for Plugins...", "Find the VST3 and Audio Unit plugins on this computer", "Options", 0); info.setActive (pluginHost != nullptr); break;
        case cmdMasterEffects: info.setInfo ("Master Effects...", "Effect plugins on the whole mix", "Options", 0); info.setActive (pluginHost != nullptr); break;
        case cmdShortcuts: info.setInfo ("Keyboard Shortcuts...", "Change the keys of the menu commands", "Options", 0); break;
        case cmdDeviceFolder: info.setInfo ("Show Device Profiles Folder", "Add your own device profiles and controller pages (YAML) here", "Options", 0); break;
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
                                                               juce::String ("*") + project_file::kExtension + ";*.mid;*.midi;*.musicxml;*.mxl");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this] (const juce::FileChooser& fc) {
                                          if (fc.getResult() != juce::File())
                                              openFile (fc.getResult(), true);
                                      });
            });
            return true;
        case cmdSave: save (false); return true;
        case cmdSaveAs: save (true); return true;
        case cmdOpenStyle: openStyle(); return true;
        case cmdSaveStyle: saveStyle(); return true;
        case cmdMakeStyle:
            session.edit ("Make Style", [] (Song& song) { style_convert::makeStyle (song); });
            showView (viewClips);
            return true;
        case cmdFillFromStyle: chooseStyleToFill(); return true;
        case cmdSectionFromLoop:
        {
            juce::PopupMenu m;
            m.addSectionHeader ("Section for bars in the loop range");
            int id = 1;
            for (const auto* name : kStyleSectionNames)
            {
                const auto& scenes = session.song().scenes;
                const bool has = std::find (scenes.begin(), scenes.end(), name) != scenes.end();
                m.addItem (id++, juce::String (name) + (has ? " (replace)" : ""));
            }
            juce::Component::SafePointer<MainComponent> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (header), [safe] (int r) {
                if (safe == nullptr || r <= 0)
                    return;
                const std::string name = kStyleSectionNames[r - 1];
                bool made = false;
                safe->session.edit ("Make Style Section", [&] (Song& song) {
                    made = style_convert::sectionFromTimeline (song, song.loop.start, song.loop.end, name) >= 0;
                });
                if (made)
                    safe->showView (viewClips);
                else
                    showError ("No notes in the loop range", "Set the loop range in the Timeline around the bars for this section.");
            });
            return true;
        }
        case cmdImportMidi: importMidi(); return true;
        case cmdExportMidi: exportMidi(); return true;
        case cmdImportMusicXml: importMusicXml(); return true;
        case cmdExportMusicXml: exportMusicXml(); return true;
        case cmdImportAudio: importAudio(); return true;
        case cmdChooseSoundFont: chooseSoundFont(); return true;
        case cmdUndo: session.undo(); return true;
        case cmdRedo: session.redo(); return true;
        case cmdDelete:
            if (view == viewScore)
                score.deleteSelected();
            else if (editorHasFocus())
                pianoRoll.deleteSelected();
            else
                arrangement.deleteSelected();
            return true;
        case cmdDuplicate: arrangement.duplicateSelected(); return true;
        case cmdSplit: arrangement.splitAtPlayhead(); return true;
        case cmdSelectAll:
            if (view == viewScore)
                score.selectAll();
            else if (editorHasFocus())
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
        case cmdAddBus: mixer.addBus(); setMixerShown (true); return true;
        case cmdPlayStop: session.togglePlay(); return true;
        case cmdGoToStart: session.locate (0); return true;
        case cmdRecord: session.toggleRecord(); return true;
        case cmdLoop: header.loopButton.onClick(); return true;
        case cmdToggleMetronome: header.clickButton.onClick(); return true;
        case cmdShowTimeline: showView (viewTimeline); return true;
        case cmdShowMixer: setMixerShown (! mixerShown); return true;
        case cmdShowBeat: showView (viewBeat); return true;
        case cmdShowClips: showView (viewClips); return true;
        case cmdCloseEditor: session.openInEditor (0); return true;
        case cmdAudioSettings: showAudioSettings(); return true;
        case cmdShortcuts: showShortcuts(); return true;
        case cmdScanPlugins: scanForPlugins(); return true;
        case cmdMasterEffects: showPlugins (0, &header); return true;
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

juce::StringArray MainComponent::getMenuBarNames() { return { "File", "Edit", "Track", "Style", "Transport", "View", "Options" }; }

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
            add ({ cmdNew, cmdOpen, 0, cmdSave, cmdSaveAs, 0, cmdImportMidi, cmdExportMidi, cmdImportMusicXml, cmdExportMusicXml, cmdImportAudio, 0, cmdChooseSoundFont });
#if ! JUCE_MAC
            m.addSeparator();
            m.addCommandItem (&commands, juce::StandardApplicationCommandIDs::quit);
#endif
            break;
        case 1: add ({ cmdUndo, cmdRedo, 0, cmdDelete, cmdDuplicate, cmdSplit, cmdSelectAll, 0, cmdQuantise, cmdQuantiseOnRecord }); break;
        case 2: add ({ cmdAddTrack, cmdAddBus }); break;
        case 3: add ({ cmdOpenStyle, cmdSaveStyle, 0, cmdFillFromStyle, 0, cmdMakeStyle, cmdSectionFromLoop }); break;
        case 4: add ({ cmdPlayStop, cmdGoToStart, cmdRecord, cmdLoop, cmdToggleMetronome }); break;
        case 5: add ({ cmdShowTimeline, cmdShowMixer, cmdShowBeat, cmdShowClips, cmdCloseEditor, 0, cmdDarkTheme, cmdLightTheme }); break;
        case 6: add ({ cmdAudioSettings, cmdShortcuts, cmdDeviceFolder, 0, cmdScanPlugins, cmdMasterEffects }); break;
        default: break;
    }
    return m;
}

// ---- Files -------------------------------------------------------------------
void MainComponent::save (bool askForFile)
{
    session.capturePluginStates(); // the plugins' settings as they are now
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

void MainComponent::openStyle()
{
    confirmDiscard ([this] {
        chooser = std::make_unique<juce::FileChooser> ("Open style file", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.sty;*.prs;*.sst;*.pst;*.pcs;*.bcs;*.fps");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) {
                                  if (fc.getResult() != juce::File())
                                      openFile (fc.getResult(), true);
                              });
    });
}

void MainComponent::chooseStyleToFill()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a style file", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.sty;*.prs;*.sst;*.pst;*.pcs;*.bcs;*.fps");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              if (fc.getResult() != juce::File())
                                  askHowToFill (fc.getResult());
                          });
}

void MainComponent::askHowToFill (const juce::File& styleFile)
{
    Style style;
    try
    {
        style = style_file::load (toPath (styleFile));
    }
    catch (const std::exception& error)
    {
        showError ("Can't open style file", juce::String::fromUTF8 (error.what()));
        return;
    }
    juce::StringArray sections;
    for (const auto& s : style.sections)
        sections.add (juce::String::fromUTF8 (s.name.c_str()));
    if (sections.isEmpty())
    {
        showError ("Can't use this style", "The style has no sections.");
        return;
    }

    // The fill starts at the bar of the playhead.
    const Tick bar = session.song().timeSignature.ticksPerBar();
    const int startBar = static_cast<int> (static_cast<Tick> (session.position()) / bar) + 1;

    auto* w = new juce::AlertWindow ("Fill Tracks from a Style",
                                     juce::String::fromUTF8 (style.name.c_str()) + ": the app makes a track for each part of the section and writes the notes for your chords.",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addComboBox ("section", sections, "Section:");
    w->getComboBoxComponent ("section")->setSelectedItemIndex (std::max (0, sections.indexOf ("Main A")), juce::dontSendNotification);
    w->addTextEditor ("chords", "C Am F G", "Chords, one for each bar:");
    w->addTextEditor ("bars", "8", "Number of bars:");
    w->addTextEditor ("start", juce::String (startBar), "Start at bar:");
    w->addComboBox ("tempo", { "Keep the tempo of the song (" + juce::String (juce::roundToInt (session.song().baseTempo())) + " BPM)",
                               "Use the tempo of the style (" + juce::String (juce::roundToInt (style.tempo)) + " BPM)" }, "Tempo:");
    w->addButton ("Fill", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MainComponent> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, styleFile] (int result) {
        if (safe == nullptr || result != 1)
            return;
        const auto section = w->getComboBoxComponent ("section")->getText();
        const auto chords = w->getTextEditorContents ("chords");
        const int bars = juce::jlimit (1, 999, w->getTextEditorContents ("bars").getIntValue());
        const int start = juce::jmax (1, w->getTextEditorContents ("start").getIntValue());
        const bool styleTempo = w->getComboBoxComponent ("tempo")->getSelectedItemIndex() == 1;
        if (chords.trim().isNotEmpty() && style_chord::parseList (chords.toStdString()).empty())
        {
            showError ("Can't read the chords", "Write chord names with a space between them, for example: C Am F G7");
            return;
        }
        if (! safe->fillFromStyle (styleFile, section, chords, bars, start, styleTempo))
            showError ("Can't fill the tracks", "The section " + section + " has no notes.");
    }), true);
}

bool MainComponent::fillFromStyle (const juce::File& styleFile, const juce::String& section, const juce::String& chords, int bars, int startBar, bool useStyleTempo)
{
    Style style;
    try
    {
        style = style_file::load (toPath (styleFile));
    }
    catch (const std::exception&)
    {
        return false;
    }
    style_convert::Fill fill;
    fill.section = section.toStdString();
    fill.chords = style_chord::parseList (chords.toStdString());
    fill.bars = bars;
    fill.at = session.song().timeSignature.ticksPerBar() * (std::max (1, startBar) - 1);
    std::vector<TrackId> made;
    session.edit ("Fill Tracks from a Style", [&] (Song& song) {
        made = style_convert::fillTimeline (song, style, fill);
        if (! made.empty() && useStyleTempo)
            song.tempo.set (0, clampTempo (style.tempo));
    });
    if (made.empty())
        return false;
    session.selectTrack (made.front());
    showView (viewTimeline);
    return true;
}

void MainComponent::saveStyle()
{
    const auto base = session.file() != juce::File() ? session.file().getParentDirectory() : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto start = base.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (session.song().title.c_str())).upToLastOccurrenceOf (".STY", false, true) + ".sty");
    chooser = std::make_unique<juce::FileChooser> ("Save as style file", start, "*.sty");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              if (fc.getResult() == juce::File())
                                  return;
                              try
                              {
                                  style_file::save (style_convert::fromSong (session.song()), toPath (fc.getResult().withFileExtension ("sty")));
                              }
                              catch (const std::exception& error)
                              {
                                  showError ("Can't save style file", juce::String::fromUTF8 (error.what()));
                              }
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

void MainComponent::importMusicXml()
{
    confirmDiscard ([this] {
        chooser = std::make_unique<juce::FileChooser> ("Import MusicXML file", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.musicxml;*.mxl;*.xml");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) {
                                  if (fc.getResult() != juce::File())
                                      openFile (fc.getResult(), true);
                              });
    });
}

void MainComponent::importAudio()
{
    chooser = std::make_unique<juce::FileChooser> ("Import audio file", juce::File::getSpecialLocation (juce::File::userMusicDirectory), audio_files::wildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              const auto bar = session.song().timeSignature.ticksPerBar();
                              const auto at = static_cast<Tick> (session.position()) / bar * bar;
                              if (arrangement.addAudioFile (file, session.selectedTrack(), at))
                                  showView (viewTimeline);
                              else
                                  showError ("Can't import the audio file", "The app reads these files: " + audio_files::formatNames() + ".");
                          });
}

void MainComponent::exportMusicXml()
{
    const auto base = session.file() != juce::File() ? session.file().getParentDirectory() : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto start = base.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (session.song().title.c_str())) + ".musicxml");
    chooser = std::make_unique<juce::FileChooser> ("Export MusicXML file", start, "*.musicxml");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              if (fc.getResult() == juce::File())
                                  return;
                              try
                              {
                                  musicxml::save (session.song(), toPath (fc.getResult().withFileExtension ("musicxml")));
                              }
                              catch (const std::exception& error)
                              {
                                  showError ("Can't export MusicXML file", juce::String::fromUTF8 (error.what()));
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

void MainComponent::loadKitSamples()
{
    const auto track = session.selectedTrack();
    if (edit::findTrack (session.song(), track) == nullptr)
        return;
    const auto start = settings != nullptr && juce::File (settings->getValue ("sampleFolder")).isDirectory()
                           ? juce::File (settings->getValue ("sampleFolder"))
                           : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Choose audio files for the kit (one file for each pad)", start, audio_files::wildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, track] (const juce::FileChooser& fc) {
                              auto files = fc.getResults();
                              if (files.isEmpty())
                                  return;
                              files.sort();
                              if (settings != nullptr)
                                  settings->setValue ("sampleFolder", files[0].getParentDirectory().getFullPathName());

                              session.edit ("Load Kit Samples", [&] (Song& song) {
                                  auto* t = edit::findTrack (song, track);
                                  if (t == nullptr)
                                      return;
                                  t->output.kind = TrackOutput::Kind::sampler;
                                  if (t->kit.name.empty())
                                      t->kit.name = files[0].getParentDirectory().getFileName().toStdString();
                                  // New pads go on the next free notes, from C1 (36) up.
                                  int note = 36;
                                  for (const auto& f : files)
                                  {
                                      while (note < 128 && t->kit.padForNote (note) != nullptr)
                                          ++note;
                                      if (note > 127)
                                          break;
                                      KitPad pad;
                                      pad.name = f.getFileNameWithoutExtension().toStdString();
                                      pad.note = note;
                                      pad.sampleFile = f.getFullPathName().toStdString();
                                      t->kit.pads.push_back (std::move (pad));
                                  }
                              });

                              juce::StringArray bad;
                              for (const auto& f : files)
                                  if (session.missingSamples().count (f.getFullPathName().toStdString()) > 0)
                                      bad.add (f.getFileName());
                              if (! bad.isEmpty())
                                  showError ("Some files could not be read", "The app reads these files: " + audio_files::formatNames() + ".\n" + bad.joinIntoString ("\n"));
                          });
}

void MainComponent::sliceLoop()
{
    if (edit::findTrack (session.song(), session.selectedTrack()) == nullptr)
        return;
    const auto start = settings != nullptr && juce::File (settings->getValue ("sampleFolder")).isDirectory()
                           ? juce::File (settings->getValue ("sampleFolder"))
                           : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Choose a loop to slice", start, audio_files::wildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              if (settings != nullptr)
                                  settings->setValue ("sampleFolder", f.getParentDirectory().getFullPathName());
                              openSlicer (f);
                          });
}

void MainComponent::openSlicer (const juce::File& loop)
{
    if (slicerWindow != nullptr)
        delete slicerWindow.getComponent();

    auto view = std::make_unique<SlicerView> (session, session.selectedTrack(), loop);
    if (! view->isLoaded())
    {
        showError ("This file can't be read", "The app reads these files: " + audio_files::formatNames() + ".\n" + loop.getFileName());
        return;
    }
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Slicer: " + loop.getFileName();
    options.dialogBackgroundColour = currentTheme().colour ("panel");
    options.content.setOwned (view.release());
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    slicerWindow = options.launchAsync();
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
    std::vector<ControllerProfile> controllers;
    juce::StringArray problems;

    // A file is a controller page when it has a "controller:" line, a device profile otherwise.
    const auto add = [&] (const juce::String& file, const juce::String& text) {
        try
        {
            if (text.startsWith ("controller:") || text.contains ("\ncontroller:"))
            {
                auto c = controller_profile::parse (text.toStdString());
                std::erase_if (controllers, [&] (const ControllerProfile& old) { return old.id == c.id; });
                controllers.push_back (std::move (c));
            }
            else
                library.add (device_profile::parse (text.toStdString()));
        }
        catch (const std::exception& e)
        {
            problems.add (file + ": " + juce::String::fromUTF8 (e.what()));
        }
    };

    // Built in first, so the user's own files with the same id replace them.
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const juce::String file (BinaryData::originalFilenames[i]);
        if (! file.endsWithIgnoreCase (".yaml"))
            continue;
        int size = 0;
        const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
        add (file, juce::String::fromUTF8 (data, size));
    }
    for (const auto& f : deviceFolder().findChildFiles (juce::File::findFiles, false, "*.yaml;*.yml"))
        add (f.getFileName(), f.loadFileAsString());

    session.setDeviceLibrary (std::move (library));
    session.setControllers (std::move (controllers));
    if (! problems.isEmpty() && settings != nullptr)
        showError ("Some device files have mistakes", problems.joinIntoString ("\n"));
}

void MainComponent::scanForPlugins()
{
    if (pluginHost == nullptr)
        return;
    scanPlugins (*pluginHost, [safe = juce::Component::SafePointer<MainComponent> (this)] (juce::String summary) {
        if (safe == nullptr)
            return;
        safe->session.setPluginHost (safe->pluginHost); // load plugins that were missing before the scan
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Scan for plugins", summary);
    });
}

void MainComponent::showPlugins (TrackId track, juce::Component* anchor)
{
    auto panel = std::make_unique<PluginRackPanel> (session, pluginWindows, track);
    panel->onScan = [this] { scanForPlugins(); };
    juce::CallOutBox::launchAsynchronously (std::move (panel), anchor != nullptr ? anchor->getScreenBounds() : getScreenBounds(), nullptr);
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (devices.deviceManager(), 0, 2, 2, 2, true, true, true, false);
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

void MainComponent::restoreShortcuts()
{
    auto* keys = commands.getKeyMappings();
    keys->removeChangeListener (this);
    if (settings != nullptr)
        if (const auto xml = settings->getXmlValue ("shortcuts"))
            keys->restoreFromXml (*xml);
    keys->addChangeListener (this);
}

std::unique_ptr<juce::Component> MainComponent::makeShortcutEditor()
{
    auto editor = std::make_unique<juce::KeyMappingEditorComponent> (*commands.getKeyMappings(), true);
    const auto& t = currentTheme();
    editor->setColours (t.colour ("panel"), t.colour ("text"));
    for (auto* child : editor->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
            button->setButtonText ("Reset to Standard Keys");
    editor->setSize (560, 620);
    return editor;
}

void MainComponent::showShortcuts()
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (makeShortcutEditor().release());
    o.dialogTitle = "Keyboard Shortcuts";
    o.dialogBackgroundColour = currentTheme().colour ("panel");
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    o.launchAsync();
}

} // namespace arranger::ui

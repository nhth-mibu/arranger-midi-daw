#include "MainComponent.h"

#include "arranger/core/ProjectFile.h"

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

MainComponent::MainComponent (Engine& e, platform::JuceDeviceHost& d, juce::PropertiesFile* s)
    : engine (e), devices (d), settings (s)
{
    if (settings != nullptr)
        setCurrentTheme (Theme::builtIn (settings->getBoolValue ("darkTheme", true)));
    lookAndFeel.applyTheme();
    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    for (auto* c : std::initializer_list<juce::Component*> { &header, &tracks, &timeline, &inspector, &statusBar })
        addAndMakeVisible (c);

    header.playButton.onClick = [this] { engine.togglePlay(); };
    header.stopButton.onClick = [this] { stopOrReturn(); };
    header.clickButton.onClick = [this] {
        song.metronome.enabled = ! song.metronome.enabled;
        inspector.songChanged();
        songEdited (true);
    };
    header.lcd.onTempoChange = [this] (double bpm) {
        song.tempoBpm = clampTempo (bpm);
        songEdited (true);
    };
    header.lcd.onMeterChange = [this] (TimeSignature sig) {
        song.timeSignature = sig;
        songEdited (true);
        timeline.repaint();
        inspector.repaint();
    };
    header.tabs.onChange = [this] (int index) { timeline.setView (index); };
    timeline.onLocate = [this] (Tick t) {
        engine.locate (t);
        if (previewMode)
            live.positionTicks = static_cast<double> (t);
    };
    inspector.onSongEdited = [this] { songEdited (true); };
    inspector.onAudioSettings = [this] { showAudioSettings(); };

    commands.registerAllCommandsForTarget (this);
    commands.setFirstCommandTarget (this);
    addKeyListener (commands.getKeyMappings());
    setWantsKeyboardFocus (true);

    engine.loadSong (song);
    updateTitle();
    pollLiveState();
    setSize (1440, 900);
    startTimerHz (4);
}

MainComponent::~MainComponent()
{
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MainComponent::setSong (const Song& s)
{
    song = s;
    engine.stop();
    engine.locate (0);
    engine.loadSong (song);
    inspector.songChanged();
    header.refresh();
    timeline.repaint();
    tracks.repaint();
    dirty = false;
    updateTitle();
}

bool MainComponent::openFile (const juce::File& file, bool showErrors)
{
    try
    {
        setSong (project_file::load (file.getFullPathName().toStdString()));
        currentFile = file;
        dirty = false;
        updateTitle();
        if (settings != nullptr)
            settings->setValue ("lastProject", file.getFullPathName());
        return true;
    }
    catch (const project_file::Error& error)
    {
        if (showErrors)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Can't open project",
                                                    juce::String::fromUTF8 (error.what()));
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

void MainComponent::setPreviewPosition (double ticks, bool playing)
{
    previewMode = true;
    live.positionTicks = ticks;
    live.playing = playing;
    header.refresh();
    timeline.refresh();
    inspector.refresh();
}

void MainComponent::songEdited (bool sendToEngine)
{
    if (sendToEngine)
        engine.loadSong (song);
    dirty = true;
    header.refresh();
    updateTitle();
}

void MainComponent::updateTitle()
{
    auto title = juce::String::fromUTF8 (song.title.c_str());
    if (currentFile != juce::File())
        title << " - " << currentFile.getFileName();
    if (dirty)
        title << " *";
    if (onTitleChanged)
        onTitleChanged (title);
}

void MainComponent::stopOrReturn()
{
    // Stop when playing; a second stop goes back to the start.
    if (engine.isPlaying())
        engine.stop();
    else
        engine.locate (0);
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
}

void MainComponent::timerCallback()
{
    pollLiveState();
    header.refresh();
    timeline.refresh();
    inspector.refresh();
    statusBar.refresh();

    // Fast only while something moves, so an idle app uses almost no CPU.
    const bool busy = live.playing || live.midiActiveRecently (juce::Time::getMillisecondCounterHiRes(), 1000.0);
    const int hz = busy ? 60 : 4;
    if (getTimerInterval() != 1000 / hz)
        startTimerHz (hz);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (currentTheme().colour ("background"));
    drawCard (g, tracksCard.toFloat());
    drawCard (g, timelineCard.toFloat());
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
    tracksCard = r.removeFromLeft (static_cast<int> (t.metric ("sidebarWidth", 248.0f)));
    timelineCard = r;
    // The track list and timeline share one card, like a single surface.
    tracksCard = tracksCard.withRight (timelineCard.getRight());

    tracks.setBounds (tracksCard.withWidth (static_cast<int> (t.metric ("sidebarWidth", 248.0f))).reduced (1));
    timeline.setBounds (timelineCard.withTrimmedLeft (1).reduced (1).withTrimmedRight (6));
    inspector.setBounds (inspectorCard.reduced (1, 8));
}

bool MainComponent::keyPressed (const juce::KeyPress&) { return false; }

// ---- Commands ---------------------------------------------------------------
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& ids)
{
    ids.addArray ({ cmdNew, cmdOpen, cmdSave, cmdSaveAs, cmdPlayStop, cmdGoToStart, cmdToggleMetronome, cmdAudioSettings, cmdDarkTheme, cmdLightTheme });
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    using KP = juce::KeyPress;
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto& t = currentTheme();

    switch (id)
    {
        case cmdNew: info.setInfo ("New", "New empty project", "File", 0); info.addDefaultKeypress ('n', cmd); break;
        case cmdOpen: info.setInfo ("Open...", "Open a project", "File", 0); info.addDefaultKeypress ('o', cmd); break;
        case cmdSave: info.setInfo ("Save", "Save the project", "File", 0); info.addDefaultKeypress ('s', cmd); break;
        case cmdSaveAs: info.setInfo ("Save As...", "Save under a new name", "File", 0); info.addDefaultKeypress ('s', cmd | juce::ModifierKeys::shiftModifier); break;
        case cmdPlayStop: info.setInfo ("Play / Stop", "Start or stop playback", "Transport", 0); info.addDefaultKeypress (KP::spaceKey, 0); break;
        case cmdGoToStart: info.setInfo ("Go to Start", "Move the playhead to bar 1", "Transport", 0); info.addDefaultKeypress (KP::homeKey, 0); info.addDefaultKeypress ('\r', 0); break;
        case cmdToggleMetronome:
            info.setInfo ("Metronome", "Turn the click on or off", "Transport", 0);
            info.addDefaultKeypress ('c', 0);
            info.setTicked (song.metronome.enabled);
            break;
        case cmdAudioSettings: info.setInfo ("Audio and MIDI Settings...", "Choose devices", "Options", 0); info.addDefaultKeypress (',', cmd); break;
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
                currentFile = juce::File();
                setSong ({});
            });
            return true;
        case cmdOpen:
            confirmDiscard ([this] {
                chooser = std::make_unique<juce::FileChooser> ("Open project", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                               juce::String ("*") + project_file::kExtension);
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this] (const juce::FileChooser& fc) {
                                          if (fc.getResult() != juce::File())
                                              openFile (fc.getResult(), true);
                                      });
            });
            return true;
        case cmdSave: save (false); return true;
        case cmdSaveAs: save (true); return true;
        case cmdPlayStop: engine.togglePlay(); return true;
        case cmdGoToStart: engine.locate (0); return true;
        case cmdToggleMetronome: header.clickButton.onClick(); return true;
        case cmdAudioSettings: showAudioSettings(); return true;
        case cmdDarkTheme: setThemeDark (true); return true;
        case cmdLightTheme: setThemeDark (false); return true;
        default: return false;
    }
}

juce::StringArray MainComponent::getMenuBarNames() { return { "File", "Transport", "View", "Options" }; }

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    switch (index)
    {
        case 0:
            m.addCommandItem (&commands, cmdNew);
            m.addCommandItem (&commands, cmdOpen);
            m.addSeparator();
            m.addCommandItem (&commands, cmdSave);
            m.addCommandItem (&commands, cmdSaveAs);
#if ! JUCE_MAC
            m.addSeparator();
            m.addCommandItem (&commands, juce::StandardApplicationCommandIDs::quit);
#endif
            break;
        case 1:
            m.addCommandItem (&commands, cmdPlayStop);
            m.addCommandItem (&commands, cmdGoToStart);
            m.addCommandItem (&commands, cmdToggleMetronome);
            break;
        case 2:
            m.addCommandItem (&commands, cmdDarkTheme);
            m.addCommandItem (&commands, cmdLightTheme);
            break;
        case 3:
            m.addCommandItem (&commands, cmdAudioSettings);
            break;
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
            project_file::save (song, file.getFullPathName().toStdString());
            currentFile = file;
            dirty = false;
            updateTitle();
            if (settings != nullptr)
                settings->setValue ("lastProject", file.getFullPathName());
        }
        catch (const project_file::Error& error)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Can't save project",
                                                    juce::String::fromUTF8 (error.what()));
        }
    };

    if (! askForFile && currentFile != juce::File())
    {
        writeTo (currentFile);
        return;
    }

    const auto start = currentFile != juce::File()
                         ? currentFile
                         : juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                               .getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (song.title.c_str())) + project_file::kExtension);
    chooser = std::make_unique<juce::FileChooser> ("Save project", start, juce::String ("*") + project_file::kExtension);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [writeTo] (const juce::FileChooser& fc) {
                              if (fc.getResult() != juce::File())
                                  writeTo (fc.getResult());
                          });
}

void MainComponent::confirmDiscard (std::function<void()> then)
{
    if (! dirty)
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
            if (! dirty)
                then();
        }
        else if (result == 2)
        {
            then();
        }
    });
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

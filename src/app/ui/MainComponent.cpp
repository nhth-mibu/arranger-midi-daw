#include "MainComponent.h"

#include "arranger/core/RecorderImport.h"

#include "arranger/core/PsrExport.h"
#include "../AudioFiles.h"

#include "arranger/core/Style.h"

#include "SlicerView.h"
#include "AnalysisView.h"
#include "AiToolsView.h"
#include "ChatView.h"
#include "SoundEditorView.h"
#include "SpeechView.h"

#include "arranger/core/MidiFile.h"
#include "arranger/core/MusicXml.h"
#include "BinaryData.h"
#include "arranger/core/ProjectFile.h"
#include "arranger/core/Version.h"

#include <filesystem>
#include <cmath>

namespace arranger::ui
{

namespace
{
    constexpr int kMixerMinHeight = 180; // room for the name, the buttons and a short fader
    constexpr int kViewMinHeight = 160;
    constexpr int kHandle = 8;                // the grab area of a resize handle, centred on its seam
    constexpr int kScrollBar = 12;
    constexpr int kLeftPanelMin = 140;        // room for a few track names
    constexpr int kAiPanelMinHeight = 200;    // room for the chat's controls
    constexpr int kInspectorMinHeight = 220;  // what the AI panel never takes away completely

    /** Draws a resize handle in one consistent style: a thin groove with a centred knob, lit while hovered or dragged. */
    void paintResizeHandle (juce::Graphics& g, bool vertical, juce::Rectangle<int> bounds, bool hot)
    {
        const auto& t = currentTheme();
        const auto r = bounds.toFloat();
        if (hot)
        {
            g.setColour (t.colour ("accent").withAlpha (0.10f));
            g.fillRoundedRectangle (r.reduced (vertical ? 3.0f : 1.0f, vertical ? 1.0f : 3.0f), 3.0f);
        }
        g.setColour (t.colour ("lineStrong").withAlpha (0.5f));
        if (vertical)
            g.fillRoundedRectangle (r.withSizeKeepingCentre (1.5f, juce::jmin (48.0f, r.getHeight())), 0.75f);
        else
            g.fillRoundedRectangle (r.withSizeKeepingCentre (juce::jmin (48.0f, r.getWidth()), 1.5f), 0.75f);
        g.setColour (t.colour (hot ? "accent" : "lineStrong"));
        if (vertical)
            g.fillRoundedRectangle (r.withSizeKeepingCentre (4.0f, 34.0f), 2.0f);
        else
            g.fillRoundedRectangle (r.withSizeKeepingCentre (34.0f, 4.0f), 2.0f);
    }

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

    /** The default place for styles that the app unpacks: Music/Arranger MIDI DAW/Styles. */
    juce::File defaultStylesFolder()
    {
        auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        if (! music.isDirectory())
            music = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        return music.getChildFile ("Arranger MIDI DAW").getChildFile ("Styles");
    }

    /** The demo style pack next to the program, or an invalid file when there is none. */
    juce::File findDemoStylesZip()
    {
        std::vector<juce::File> folders;
        const auto add = [&folders] (juce::File f)
        {
            if (f == juce::File())
                return;
            if (f.existsAsFile())
                f = f.getParentDirectory();
            if (! f.isDirectory())
                return;
            if (std::find (folders.begin(), folders.end(), f) == folders.end())
                folders.push_back (f);
        };

        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        add (exe);
        add (exe.getParentDirectory());
        add (exe.getParentDirectory().getParentDirectory());
        add (exe.getParentDirectory().getParentDirectory().getParentDirectory());
        add (exe.getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory());
        add (juce::File::getSpecialLocation (juce::File::currentApplicationFile));
        add (juce::File::getSpecialLocation (juce::File::currentApplicationFile).getParentDirectory());
        add (juce::File::getCurrentWorkingDirectory());

        for (const auto& folder : folders)
            for (const auto* name : { "ArrangerMidiDaw-Demo-Styles.zip", "ArrangerMidiDaw-Demo-Styles.ZIP" })
                if (const auto file = folder.getChildFile (name); file.existsAsFile())
                    return file;
        return {};
    }

    /** Where the style file chooser starts: the selected style folder, else Music. */
    juce::File styleStartFolder()
    {
        const auto chosen = SessionTools::styleFolder();
        return chosen.isDirectory() ? chosen : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    }
}

MainComponent::MainComponent (Engine& e, platform::JuceDeviceHost& d, juce::PropertiesFile* s, PluginHost* p)
    : engine (e), devices (d), settings (s), pluginHost (p)
{
    session.setPluginHost (pluginHost);
    if (settings != nullptr)
        setCurrentTheme (Theme::builtIn (settings->getBoolValue ("darkTheme", true)));
    if (settings != nullptr)
        SessionTools::setStyleFolder (juce::File (settings->getValue ("styleFolder")));
    speechService = std::make_unique<SpeechService> (session, devices, settings);
    speechService->onTranscript = [this] (const juce::String& text)
    {
        openAiChat();
        if (aiPanel != nullptr)
            aiPanel->setChatInput (text);
    };
    speechService->onError = [this] (const juce::String& text)
    {
        showError ("Speech", text);
    };
    speechService->onStateChanged = [this]
    {
        if (aiPanel != nullptr)
            aiPanel->setSpeechState (speechService->isListening(), speechService->isEnabled() && speechService->speaksReplies());
    };
    if (settings != nullptr && settings->getBoolValue ("allowAi", false))
        setAiAllowed (true);
    lookAndFeel.applyTheme();
    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    for (auto* c : std::initializer_list<juce::Component*> { &header, &trackList, &arrangement, &mixer, &pianoRoll, &statusBar })
        addAndMakeVisible (c);
    inspectorViewport.setViewedComponent (&inspector, false);
    inspectorViewport.setScrollBarsShown (true, false);
    inspectorViewport.setScrollBarThickness (8);
    addAndMakeVisible (inspectorViewport);
    addAndMakeVisible (sideResizer);
    addAndMakeVisible (leftResizer);
    addAndMakeVisible (mixerBar);
    addAndMakeVisible (aiBar);
    aiBar.setVisible (false);
    for (auto* bar : { &timelineHScroll, &timelineVScroll })
    {
        bar->setAutoHide (false);
        bar->addListener (this);
        addAndMakeVisible (bar);
    }
    trackList.onTrackZoom = [this] (double factor) { zoomVertical (factor); };
    statusBar.onZoomVertical = [this] (double factor) { zoomVertical (factor); };
    header.onZoomHorizontal = [this] (double factor) { zoomHorizontal (factor); };
    session.onJamFinished = [this] { showView (viewTimeline); };
    session.onKidsModeChanged = [this] { kidsModeChanged(); };
    if (settings != nullptr)
    {
        Session::KidsModeSettings kids;
        kids.pauseAfterSeconds = settings->getDoubleValue ("kidsPauseAfter", kids.pauseAfterSeconds);
        kids.keysSound = settings->getBoolValue ("kidsKeysSound", false);
        session.setKidsModeSettings (kids);
    }
    arrangement.onAnalyse = [this] (ClipId id) { showAnalysis (id); };
    arrangement.onReharmonize = [this] (Tick at) {
        showAiTool ("chords");
        aiPanel->reharmonizeAt (at);
    };
    arrangement.onChordTools = [this] (int bar) {
        showAiTool ("chords");
        aiPanel->typeChordsAt (bar);
    };
    midiAi = std::make_unique<MidiAiService>();
    MidiAiService::onActivity = [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::String& text) {
        if (safe != nullptr)
            safe->statusBar.setActivity (text);
    };
    if (settings != nullptr)
    {
        mixerShown = settings->getBoolValue ("mixerShown", true);
        mixerHeight = settings->getIntValue ("mixerHeight", mixerHeight);
        sidePanelWidth = juce::jlimit (280, 560, settings->getIntValue ("sidePanelWidth", sidePanelWidth));
        leftPanelWidth = juce::jlimit (kLeftPanelMin, 520, settings->getIntValue ("leftPanelWidth", leftPanelWidth));
        aiPanelHeight = juce::jlimit (kAiPanelMinHeight, 600, settings->getIntValue ("aiPanelHeight", aiPanelHeight));
    }
    sideResizer.onStart = [this] { sidePanelWidthAtDragStart = sidePanelWidth; };
    sideResizer.onDrag = [this] (int dx) { setSidePanelWidth (sidePanelWidthAtDragStart - dx); };
    sideResizer.onReset = [this] { setSidePanelWidth (320); };
    leftResizer.onStart = [this] { leftPanelWidthAtDragStart = leftPanelWidth; };
    leftResizer.onDrag = [this] (int dx) { setLeftPanelWidth (leftPanelWidthAtDragStart + dx); };
    leftResizer.onReset = [this] { setLeftPanelWidth (248); };
    aiBar.onStart = [this] { aiPanelHeightAtDragStart = aiPanelHeight; };
    aiBar.onDrag = [this] (int dy) { setAiPanelHeight (aiPanelHeightAtDragStart - dy); };
    aiBar.onReset = [this] { setAiPanelHeight (300); };
    mixerBar.onStart = [this] { mixerHeightAtDragStart = mixer.getHeight() + 1; };
    mixerBar.onDrag = [this] (int dy) { setMixerHeight (mixerHeightAtDragStart - dy); };
    mixerBar.onReset = [this] { setMixerHeight (280); };
    header.mixerButton.onClick = [this] { setMixerShown (! mixerShown); };
    header.aiButton.onClick = [this] { toggleAiChat(); };
    addChildComponent (beat);
    addChildComponent (score);
    addChildComponent (clipGrid);
    addChildComponent (mapView);
    mapView.onProfileChanged = [this] (const ControllerProfile& page) {
        if (! SessionTools::saveDeviceFile)
            return;
        try
        {
            SessionTools::saveDeviceFile (page.id + ".yaml", controller_profile::write (page));
        }
        catch (const std::exception& e)
        {
            showError ("MIDI Map", juce::String ("The controller page could not be saved: ") + e.what());
        }
    };
    mapView.onAskAi = [this] (const juce::String& prompt) { openAiChat (prompt); };

    geometry.onChange = [this] {
        trackList.repaint();
        arrangement.repaint();
        updateTimelineScrollBars();
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

    session.onError = [] (const juce::String& text) { showError ("Lazy Arranger Midi DAW", text); };
    session.onOpenBeat = [this] { showView (viewBeat); };
    session.addChangeListener (this);

    commands.registerAllCommandsForTarget (this);
    commands.setFirstCommandTarget (this);
    header.tabs.tooltipForTab = [this] (int index) {
        static const char* labels[] = { "Timeline", "Score", "Drums", "Loops", "MIDI Map" };
        static const char* commandNames[] = { "Timeline", "", "Drums", "Loops", "MIDI Map" };
        if (index < 0 || index >= static_cast<int> (std::size (labels)))
            return juce::String();
        if (commandNames[index][0] == 0)
            return juce::String (labels[index]);
        return shortcuts::withShortcut (labels[index], commands, commandNames[index]);
    };
    addKeyListener (commands.getKeyMappings());
    setWantsKeyboardFocus (true);

    // Ports set up by hand (a profile for an output, a controller page for an input).
    if (settings != nullptr)
    {
        const auto setup = juce::JSON::parse (settings->getValue ("portSetup"));
        if (auto* d = setup["devices"].getDynamicObject())
            for (const auto& p : d->getProperties())
                session.assignDeviceProfile (p.name.toString().toStdString(), p.value.toString().toStdString());
        if (auto* c = setup["controllers"].getDynamicObject())
            for (const auto& p : c->getProperties())
                session.assignControllerPage (p.name.toString().toStdString(), p.value.toString().toStdString());
        session.onPortSetupChanged = [this] {
            const auto toObject = [] (const std::map<std::string, std::string>& m) {
                auto* o = new juce::DynamicObject();
                for (const auto& [port, id] : m)
                    o->setProperty (juce::Identifier (juce::String::fromUTF8 (port.c_str())), juce::String::fromUTF8 (id.c_str()));
                return juce::var (o);
            };
            auto* root = new juce::DynamicObject();
            root->setProperty ("devices", toObject (session.deviceAssignments()));
            root->setProperty ("controllers", toObject (session.controllerAssignments()));
            settings->setValue ("portSetup", juce::JSON::toString (juce::var (root), true));
        };
    }
    // Arranging help: the user's choices and their own device numbers.
    if (settings != nullptr)
    {
        session.setArrangingPreferences (arranging::Preferences::fromJson (settings->getValue ("arrangingChoices", "{}").toStdString()));
        const auto caps = juce::JSON::parse (settings->getValue ("deviceCapabilities"));
        if (auto* o = caps.getDynamicObject())
            for (const auto& p : o->getProperties())
            {
                try
                {
                    session.setDeviceCapabilities (p.name.toString().toStdString(), device_profile::parseCapabilitiesJson (juce::JSON::toString (p.value, true).toStdString()));
                }
                catch (const std::exception&)
                {
                }
            }
        session.onArrangingSettingsChanged = [this] {
            settings->setValue ("arrangingChoices", juce::String::fromUTF8 (session.arrangingPreferences().toJson().c_str()));
            auto* root = new juce::DynamicObject();
            for (const auto& [id, c] : session.userDeviceCapabilities())
                root->setProperty (juce::Identifier (juce::String::fromUTF8 (id.c_str())), juce::JSON::parse (juce::String::fromUTF8 (device_profile::capabilitiesJson (c).c_str())));
            settings->setValue ("deviceCapabilities", juce::JSON::toString (juce::var (root), true));
        };
    }
    loadDeviceProfiles();
    SessionTools::listDeviceFiles = [] {
        std::vector<mcp::Host::DeviceFile> files;
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const juce::String file (BinaryData::originalFilenames[i]);
            if (! file.endsWithIgnoreCase (".yaml"))
                continue;
            int size = 0;
            const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
            files.push_back ({ file.toStdString(), juce::String::fromUTF8 (data, size).toStdString(), true });
        }
        for (const auto& f : deviceFolder().findChildFiles (juce::File::findFiles, false, "*.yaml;*.yml"))
            files.push_back ({ f.getFileName().toStdString(), f.loadFileAsString().toStdString(), false });
        return files;
    };
    session.saveUserSounds = [this] (const SoundAddOn& a) { saveUserSounds (a); };
    SessionTools::saveDeviceFile = [safe = juce::Component::SafePointer<MainComponent> (this)] (const std::string& name, const std::string& text) {
        const auto folder = deviceFolder();
        folder.createDirectory();
        const auto file = folder.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (name.c_str())));
        if (file.getParentDirectory() != folder || ! file.replaceWithText (juce::String::fromUTF8 (text.c_str())))
            throw std::runtime_error ("the file could not be saved in " + folder.getFullPathName().toStdString());
        if (safe != nullptr)
            safe->loadDeviceProfiles();
    };

    // Start with one SoundFont track so a new project plays right away.
    Song song;
    edit::addTrack (song, "Piano");
    session.load (std::move (song));
    if (settings != nullptr)
    {
        if (const juce::File font (settings->getValue ("soundFont")); font.existsAsFile())
            session.loadSoundFont (font);
        // Unsaved work from a crash comes first, then the demo style offer, which hands over to the SoundFont offer.
        autosaver = std::make_unique<autosave::Autosaver> (toPath (autosaveFolder()));
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)] {
            if (safe != nullptr)
                safe->offerRecovery ([safe] {
                    if (safe != nullptr)
                        safe->findOrOfferDemoStyles();
                });
        });
    }

    updateTitle();
    pollLiveState();
    setSize (1440, 900);
    startTimerHz (4);
}

MainComponent::~MainComponent()
{
    if (autosaver != nullptr)
        autosaver->discard(); // a clean quit: nothing to recover
    SessionTools::listDeviceFiles = nullptr;
    SessionTools::saveDeviceFile = nullptr;
    session.saveUserSounds = nullptr;
    MidiAiService::onActivity = nullptr;
    aiPanel.reset();
    midiAi.reset();
    if (soundEditorWindow != nullptr)
        delete soundEditorWindow.getComponent();
    aiChat.reset();
    aiService.reset();
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
    updateTimelineScrollBars();
    commands.commandStatusChanged();
    const bool editorOpen = session.editedClip() != 0 && (view == viewTimeline || view == viewClips);
    if (editorOpen != pianoRoll.isVisible())
        resized();
}

void MainComponent::zoomHorizontal (double factor)
{
    switch (view)
    {
        case viewTimeline: arrangement.zoom (factor, static_cast<float> (arrangement.getWidth()) * 0.5f); break;
        case viewScore: score.zoomBy (factor); break;
        case viewBeat: beat.zoomBy (factor); break;
        case viewClips: clipGrid.zoomHorizontally (factor); break;
    }
}

void MainComponent::zoomVertical (double factor)
{
    switch (view)
    {
        case viewTimeline: arrangement.zoomVertically (factor); break;
        case viewScore: score.zoomBy (factor); break;
        case viewBeat: beat.zoomBy (factor); break;
        case viewClips: clipGrid.zoomVertically (factor); break;
    }
}

void MainComponent::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &timelineHScroll)
        geometry.scrollTicks = std::max (0.0, newRangeStart);
    else if (bar == &timelineVScroll)
        geometry.scrollY = std::max (0, juce::roundToInt (newRangeStart));
    else
        return;
    geometry.changed();
}

void MainComponent::updateTimelineScrollBars()
{
    const int viewWidth = arrangement.getWidth();
    const int viewHeight = arrangement.getHeight();
    if (viewWidth <= 0 || viewHeight <= 0)
        return;

    const auto& song = session.song();
    const double pixelsPerTick = std::max (1.0e-9, geometry.pixelsPerTick);
    const double visibleTicks = std::max (1.0, static_cast<double> (viewWidth) / pixelsPerTick);
    const double barTicks = std::max (1.0, static_cast<double> (song.timeSignature.ticksPerBar()));

    double endTicks = static_cast<double> (song.contentEnd());
    if (song.loop.isValid())
        endTicks = std::max (endTicks, static_cast<double> (song.loop.end));
    endTicks = std::max (endTicks, session.position());

    // Leave half a screen after the song, so new clips can be put past its end.
    const double tailTicks = std::max (barTicks * 2.0, visibleTicks * 0.5);
    const double totalTicks = std::max (visibleTicks, std::ceil ((endTicks + tailTicks) / barTicks) * barTicks);
    const double maxStartTicks = std::max (0.0, totalTicks - visibleTicks);
    geometry.scrollTicks = std::clamp (geometry.scrollTicks, 0.0, maxStartTicks);
    timelineHScroll.setRangeLimits (0.0, totalTicks, juce::dontSendNotification);
    timelineHScroll.setCurrentRange (geometry.scrollTicks, visibleTicks, juce::dontSendNotification);

    const int viewport = std::max (1, viewHeight - geometry.rulerHeight);
    const int trackCount = static_cast<int> (song.tracks.size());
    const int contentHeight = std::max (viewport, geometry.contentHeight (trackCount));
    geometry.scrollY = std::clamp (geometry.scrollY, 0, contentHeight - viewport);
    timelineVScroll.setRangeLimits (0.0, static_cast<double> (contentHeight), juce::dontSendNotification);
    timelineVScroll.setCurrentRange (static_cast<double> (geometry.scrollY), static_cast<double> (viewport), juce::dontSendNotification);
}

bool MainComponent::openFile (const juce::File& file, bool showErrors)
{
    try
    {
        if (isStyleFile (file))
        {
            session.load (style_convert::toSong (style_file::load (toPath (file))));
            session.doc().markSaved();
            showView (viewClips); // a style lives in the Loops view
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
    session.setDrumsOnly (index == viewBeat); // the Drums view plays only its track
    if (index == viewMap)
        mapView.refresh();
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
    paintResizeHandle (g, false, getLocalBounds(), isMouseOverOrDragging());
}

void MainComponent::WidthResizeBar::paint (juce::Graphics& g)
{
    paintResizeHandle (g, true, getLocalBounds(), isMouseOverOrDragging());
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
    score.refresh();
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
    if (view == viewScore)
        score.refresh();
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
    tickAutosave();

    // Fast only while something moves, so an idle app uses almost no CPU.
    const bool busy = live.playing || live.midiActiveRecently (juce::Time::getMillisecondCounterHiRes(), mixerShown ? 4000.0 : 1000.0);
    const int hz = busy ? 60 : 4;
    if (getTimerInterval() != 1000 / hz)
        startTimerHz (hz);
}

void MainComponent::paint (juce::Graphics& g)
{
    // Flush panels: one flat surface, with a 1 px seam where two panels meet.
    const auto& t = currentTheme();
    g.fillAll (t.colour ("background"));
    const auto panel = t.colour ("panel");
    g.setColour (panel);
    g.fillRect (mainCard);
    if (pianoRoll.isVisible())
        g.fillRect (editorCard);
    if (mixerShown)
        g.fillRect (mixerCard);
    g.fillRect (sideCard);

    g.setColour (t.colour ("line"));
    g.fillRect (sideCard.getX(), sideCard.getY(), 1, sideCard.getHeight());
    if (pianoRoll.isVisible())
        g.fillRect (editorCard.getX(), editorCard.getY(), editorCard.getWidth(), 1);
    if (mixerShown)
        g.fillRect (mixerCard.getX(), mixerCard.getY(), mixerCard.getWidth(), 1);
    if (aiPanelShown && aiPanel != nullptr && aiPanel->isVisible())
        g.fillRect (sideCard.getX(), aiPanel->getY(), sideCard.getWidth(), 1);
    if (trackList.isVisible())
        g.fillRect (trackList.getRight(), mainCard.getY(), 1, mainCard.getHeight() - kScrollBar);
}

void MainComponent::resized()
{
    const auto& t = currentTheme();
    auto r = getLocalBounds();
    header.setBounds (r.removeFromTop (static_cast<int> (t.metric ("headerHeight", 64.0f))));
    statusBar.setBounds (r.removeFromBottom (static_cast<int> (t.metric ("statusHeight", 28.0f))));

    // Panels sit flush. Each resize handle straddles the seam it moves, half on each side.
    sideCard = r.removeFromRight (sidePanelWidth);
    auto side = sideCard.withTrimmedLeft (1);
    sideResizer.setBounds (sideCard.getX() - kHandle / 2, sideCard.getY(), kHandle, sideCard.getHeight());
    aiBar.setVisible (aiPanelShown && aiPanel != nullptr);
    if (aiPanelShown && aiPanel != nullptr)
    {
        const int most = juce::jmax (kAiPanelMinHeight, side.getHeight() - kInspectorMinHeight);
        const int aiHeight = juce::jlimit (kAiPanelMinHeight, most, aiPanelHeight);
        aiPanel->setBounds (side.removeFromBottom (aiHeight).withTrimmedTop (1));
        aiBar.setBounds (side.getX(), side.getBottom() - kHandle / 2, side.getWidth(), kHandle);
        aiPanel->setVisible (true);
    }
    else if (aiPanel != nullptr)
        aiPanel->setVisible (false);
    inspectorViewport.setBounds (side);
    inspector.setSize (juce::jmax (200, side.getWidth() - 10), 640);

    // The mixer panel sits under every view. It never takes more than the view can give.
    mixer.setVisible (mixerShown);
    mixerBar.setVisible (mixerShown);
    header.mixerButton.setToggleState (mixerShown, juce::dontSendNotification);
    if (mixerShown)
    {
        const int most = juce::jmax (kMixerMinHeight, r.getHeight() - kViewMinHeight);
        mixerCard = r.removeFromBottom (juce::jlimit (kMixerMinHeight, most, mixerHeight));
        mixerBar.setBounds (mixerCard.getX(), mixerCard.getY() - kHandle / 2, mixerCard.getWidth(), kHandle);
        mixer.setBounds (mixerCard.withTrimmedTop (1));
    }

    const bool editorOpen = session.editedClip() != 0 && (view == viewTimeline || view == viewClips);
    pianoRoll.setVisible (editorOpen);
    if (editorOpen)
    {
        editorCard = r.removeFromBottom (juce::jlimit (juce::jmin (240, r.getHeight() / 2), 520, r.getHeight() * 45 / 100));
        pianoRoll.setBounds (editorCard.withTrimmedTop (1));
    }
    mainCard = r;

    trackList.setVisible (view == viewTimeline);
    arrangement.setVisible (view == viewTimeline);
    leftResizer.setVisible (view == viewTimeline);
    timelineHScroll.setVisible (view == viewTimeline);
    timelineVScroll.setVisible (view == viewTimeline);

    auto card = mainCard;
    auto hBarArea = card.removeFromBottom (kScrollBar);
    auto vBarArea = card.removeFromRight (kScrollBar);
    const int sidebar = juce::jlimit (kLeftPanelMin, juce::jmax (kLeftPanelMin, card.getWidth() - 260), leftPanelWidth);
    auto leftPanel = card.removeFromLeft (sidebar);
    trackList.setBounds (leftPanel);
    arrangement.setBounds (card.withTrimmedLeft (1));
    leftResizer.setBounds (leftPanel.getRight() - kHandle / 2, leftPanel.getY(), kHandle, leftPanel.getHeight());
    timelineHScroll.setBounds (hBarArea);
    timelineVScroll.setBounds (vBarArea.withTrimmedTop (geometry.rulerHeight));
    updateTimelineScrollBars();
    // Handles go on top of the panels they straddle.
    for (auto* handle : std::initializer_list<juce::Component*> { &leftResizer, &mixerBar, &sideResizer, &aiBar })
        if (handle->isVisible())
            handle->toFront (false);
    score.setVisible (view == viewScore);
    score.setBounds (mainCard);
    beat.setVisible (view == viewBeat);
    beat.setBounds (mainCard);
    clipGrid.setVisible (view == viewClips);
    clipGrid.setBounds (mainCard);
    mapView.setVisible (view == viewMap);
    mapView.setBounds (mainCard);
    if (kidsView != nullptr)
        kidsView->setBounds (getLocalBounds());
    repaint();
}

void MainComponent::setKidsMode (bool on, bool previewPlaying)
{
    session.setKidsMode (on);
    if (on && previewPlaying && kidsView != nullptr)
        kidsView->setPreview (true);
}

void MainComponent::kidsModeChanged()
{
    if (settings != nullptr)
    {
        settings->setValue ("kidsPauseAfter", session.kidsModeSettings().pauseAfterSeconds);
        settings->setValue ("kidsKeysSound", session.kidsModeSettings().keysSound);
    }
    if (session.kidsMode())
    {
        if (kidsView != nullptr && ! kidsView->isVisible())
            kidsView->setVisible (true);
        if (kidsView == nullptr)
        {
            kidsView = std::make_unique<KidsView> (session);
            kidsView->setBounds (getLocalBounds());
            addAndMakeVisible (*kidsView);
            kidsView->toFront (true);
        }
        kidsView->refresh();
    }
    else if (kidsView != nullptr)
    {
        // The Exit button is inside the view: delete it after the click is done.
        kidsView->setVisible (false);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] {
            if (safe != nullptr && ! safe->session.kidsMode() && safe->kidsView != nullptr)
            {
                safe->removeChildComponent (safe->kidsView.get());
                safe->kidsView.reset();
            }
        });
        grabKeyboardFocus();
    }
    commands.commandStatusChanged();
}

bool MainComponent::editorHasFocus() const
{
    const auto* focused = juce::Component::getCurrentlyFocusedComponent();
    return pianoRoll.isVisible() && focused != nullptr && (focused == &pianoRoll || pianoRoll.isParentOf (focused));
}

void MainComponent::setAiAllowed (bool allowed)
{
    aiService.reset();
    if (allowed)
    {
        aiService = std::make_unique<McpService> (session);
        if (! aiService->isRunning())
        {
            aiService.reset();
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "AI assistants",
                                                    "The app could not open its connection for AI assistants. Another program may use the port.");
            allowed = false;
        }
    }
    if (settings != nullptr)
        settings->setValue ("allowAi", allowed);
    commands.commandStatusChanged();
}

void MainComponent::copyAiSetup()
{
    const auto helperFile = McpService::helperProgram();
    if (! helperFile.existsAsFile())
    {
        showError ("AI assistant setup",
                   "The arranger-mcp helper program is missing next to the app. Reinstall the app package, then try again.");
        return;
    }
    const auto helper = helperFile.getFullPathName();
    const auto escaped = helper.replace ("\\", "\\\\").replace ("\"", "\\\"");
    const juce::String text = "{\n  \"mcpServers\": {\n    \"arranger-midi-daw\": {\n      \"command\": \"" + escaped + "\"\n    }\n  }\n}\n";
    juce::SystemClipboard::copyTextToClipboard (text);
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "AI assistant setup copied",
                                            "For Claude Desktop: open Settings > Developer > Edit Config, paste this into claude_desktop_config.json "
                                            "(merge it with what is there), save, and start Claude Desktop again.\n\n"
                                            "For Claude Code: run  claude mcp add arranger-midi-daw \"" + helper + "\"\n\n"
                                            "For Codex: run  codex mcp add arranger-midi-daw -- \"" + helper + "\"\n\n"
                                            + (aiService != nullptr ? juce::String() : juce::String ("Also select Options > Allow AI Assistants.\n\n")) + text);
}

void MainComponent::chooseStyleFolder()
{
    const auto current = SessionTools::styleFolder();
    chooser = std::make_unique<juce::FileChooser> ("Choose the folder with your styles", current.isDirectory() ? current : juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
        const auto folder = fc.getResult();
        if (! folder.isDirectory())
            return;
        SessionTools::setStyleFolder (folder);
        if (settings != nullptr)
            settings->setValue ("styleFolder", folder.getFullPathName());
    });
}

void MainComponent::ensureAiPanel()
{
    if (! aiChat)
        aiChat = std::make_unique<AssistantChat> (session, settings, [this] {
            if (aiService == nullptr || ! aiService->isRunning())
                setAiAllowed (true);
            return aiService != nullptr && aiService->isRunning();
        });
    if (! aiPanel)
    {
        aiPanel = std::make_unique<AiPanel> (session, *midiAi, *aiChat);
        aiPanel->onClose = [this] { showAiPanel (false); };
        aiPanel->onOpenStyle = [this] (const juce::File& file) { confirmDiscard ([this, file] { openFile (file, true); }); };
        aiPanel->onAssistantReply = [this] (const juce::String& text)
        {
            if (speechService != nullptr)
                speechService->speak (text);
        };
        aiPanel->onMic = [this]
        {
            if (speechService == nullptr)
                return;
            if (speechService->isListening())
            {
                speechService->stopListening();
                return;
            }
            speechService->setEnabled (true);
            speechService->setTakesVoiceInput (true);
            speechService->startListening();
        };
        aiPanel->onSpeakToggle = [this] (bool on)
        {
            if (speechService == nullptr)
                return;
            speechService->setEnabled (true);
            speechService->setSpeaksReplies (on);
        };
        aiPanel->setSpeechState (speechService != nullptr && speechService->isListening(),
                                 speechService != nullptr && speechService->isEnabled() && speechService->speaksReplies());
        addChildComponent (*aiPanel);
        aiPanel->setVisible (false);
    }
}

void MainComponent::showAiPanel (bool show)
{
    if (show)
        ensureAiPanel();
    aiPanelShown = show;
    if (aiPanel != nullptr)
        aiPanel->setVisible (show);
    header.aiButton.setToggleState (show, juce::dontSendNotification);
    resized();
    repaint();
    commands.commandStatusChanged();
}

void MainComponent::setSidePanelWidth (int width)
{
    const int most = juce::jmax (280, juce::jmin (560, getWidth() - 360));
    sidePanelWidth = juce::jlimit (280, most, width);
    if (settings != nullptr)
        settings->setValue ("sidePanelWidth", sidePanelWidth);
    resized();
    repaint();
}

void MainComponent::setLeftPanelWidth (int width)
{
    const int most = juce::jmax (kLeftPanelMin, juce::jmin (520, getWidth() - 360));
    leftPanelWidth = juce::jlimit (kLeftPanelMin, most, width);
    if (settings != nullptr)
        settings->setValue ("leftPanelWidth", leftPanelWidth);
    resized();
    repaint();
}

void MainComponent::setAiPanelHeight (int height)
{
    const int most = juce::jmax (kAiPanelMinHeight, juce::jmin (600, getHeight() - 140));
    aiPanelHeight = juce::jlimit (kAiPanelMinHeight, most, height);
    if (settings != nullptr)
        settings->setValue ("aiPanelHeight", aiPanelHeight);
    resized();
    repaint();
}

void MainComponent::toggleAiChat()
{
    if (aiPanelShown && aiPanel != nullptr && aiPanel->chatSelected())
    {
        showAiPanel (false);
        return;
    }
    ensureAiPanel();
    aiPanel->showChat();
    showAiPanel (true);
}

void MainComponent::openAiChat (const juce::String& message)
{
    ensureAiPanel();
    aiPanel->showChat();
    showAiPanel (true);
    if (message.isNotEmpty())
        aiPanel->sendChatMessage (message);
}

ClipId MainComponent::selectedAudioClip() const
{
    for (const auto id : session.selectedClips())
        if (edit::findAudioClip (session.song(), id) != nullptr)
            return id;
    return 0;
}

void MainComponent::showAnalysis (ClipId clip, bool start)
{
    if (analysisWindow != nullptr)
        delete analysisWindow.getComponent(); // a new one for this clip
    struct Window final : juce::DocumentWindow
    {
        Window() : juce::DocumentWindow ("Analyse Audio", currentTheme().colour ("panel"), juce::DocumentWindow::closeButton) {}
        void closeButtonPressed() override { delete this; }
    };
    auto* view = new AnalysisView (session, clip);
    view->onAskAi = [this] (const juce::String& message) { openAiChat (message); };
    view->onCheckTranscription = [this] { showAiTool ("transcription_questions"); };
    auto* window = new Window();
    window->setUsingNativeTitleBar (true);
    window->setContentOwned (view, true);
    window->setResizable (true, false);
    window->setResizeLimits (480, 520, 2000, 2000);
    window->centreAroundComponent (this, window->getWidth(), window->getHeight());
    window->setVisible (true);
    analysisWindow = window;
    if (start)
        view->analyse();
}

void MainComponent::chooseArrangeGoal (const juce::String& goal, int fromBar, int toBar)
{
    ensureAiPanel();
    aiPanel->chooseArrangeGoal (goal, fromBar, toBar);
}

void MainComponent::showAiTool (const juce::String& tool, bool start)
{
    ensureAiPanel();
    aiPanel->showTool (tool);
    showAiPanel (true);
    if (tool == "arrangement_doctor" || tool == "hardware_fit" || tool == "chords" || tool == "style_from_audio" || tool == "transcription_questions")
        setAiPanelHeight (std::max (aiPanelHeight, 600)); // proposals need room
    if (start)
        aiPanel->startTool();
}

void MainComponent::reharmonizeAt (Tick at)
{
    showAiTool ("chords");
    aiPanel->reharmonizeAt (at);
}

void MainComponent::copyOrCut (bool cut)
{
    // The view in front gets it: Score, the piano roll (when it has the keys), Beat, the Clip grid, else the Timeline.
    if (view == viewScore)
        score.copySelected (cut);
    else if (editorHasFocus())
        pianoRoll.copySelected (cut);
    else if (view == viewBeat)
        beat.copyPattern (cut);
    else if (view == viewClips)
        clipGrid.copySlot (cut);
    else
        arrangement.copySelected (cut);
    commands.commandStatusChanged();
}

void MainComponent::paste()
{
    const auto kind = session.clipboard.kind;
    if (view == viewScore)
        score.paste();
    else if (editorHasFocus() && kind == clipboard::Content::Kind::notes)
        pianoRoll.paste();
    else if (view == viewBeat)
        beat.pastePattern();
    else if (view == viewClips)
        clipGrid.pasteSlot();
    else
    {
        auto track = session.selectedTrack();
        if (edit::findTrack (session.song(), track) == nullptr && ! session.song().tracks.empty())
            track = session.song().tracks.front().id;
        arrangement.paste (track, static_cast<Tick> (session.position()));
    }
}

// ---- Commands ---------------------------------------------------------------
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& ids)
{
    for (int id = cmdNew; id <= cmdCheckTranscription; ++id)
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
        case cmdOpenStyle: info.setInfo ("Open Style in Loops...", "Open a Yamaha style file in the Loops view, where you edit its sections and parts", "Style", 0); break;
        case cmdSaveStyle: info.setInfo ("Save as Style File...", "Save the Loops view as a Yamaha style file (.sty)", "Style", 0); info.setActive (song.style.has_value()); break;
        case cmdMakeStyle: info.setInfo ("Make Loops a Style", "Turn the tracks of the Loops view into style parts that follow chords", "Style", 0); info.setActive (! song.style.has_value()); break;
        case cmdStyleFolder:
            info.setInfo ("Choose Style Folder...", "The folder (and its subfolders) where the AI assistant looks for styles by name", "Style", 0);
            break;
        case cmdFillFromStyle: info.setInfo ("Fill Timeline from a Style...", "Write a section of a Yamaha style onto new Timeline tracks, for a number of bars and with your chords", "Style", 0); break;
        case cmdSectionFromLoop:
            info.setInfo ("Make a Style Section from the Loop Range...", "Copy the notes in the loop range of the Timeline to a section of the style", "Style", 0);
            info.setActive (song.loop.isValid());
            break;
        case cmdImportMidi: info.setInfo ("Import MIDI File...", "Open a Standard MIDI File", "File", 0); info.addDefaultKeypress ('i', cmd | shift); break;
        case cmdExportMidi: info.setInfo ("Export MIDI File...", "Save the song as a Standard MIDI File", "File", 0); info.addDefaultKeypress ('e', cmd | shift); break;
        case cmdImportMusicXml: info.setInfo ("Import MusicXML File...", "Open a score from a notation program", "File", 0); break;
        case cmdImportAudio: info.setInfo ("Import Audio File...", "Put an audio file on an audio track, at the playhead", "File", 0); break;
        case cmdImportRecorder: info.setInfo ("Import L6max Recording...", "Put each channel of a Zoom LiveTrak L6max recording folder on its own audio track", "File", 0); break;
        case cmdExportMusicXml: info.setInfo ("Export MusicXML File...", "Save the score for a notation program", "File", 0); break;
        case cmdExportPsr: info.setInfo ("Export Song for PSR-S970...", "Save a MIDI file that plays on the voices of the Yamaha PSR-S970", "File", 0); break;
        case cmdExportAudio: info.setInfo ("Export Audio File...", "Play the song and save the sound as a WAV file", "File", 0); break;
        case cmdSoundEditor:
            info.setInfo ("Synth Sound Editor...", "Change the sound of a hardware synth (Roland D-05, Novation XioSynth...) with knobs on the screen", "Track", 0);
            info.addDefaultKeypress ('e', cmd | shift);
            break;
        case cmdAnalyseAudio:
            info.setInfo ("Analyse Audio...", "Take the selected audio clip apart: stems, a MIDI track per stem, chords, instruments and lyrics", "Track", 0);
            info.setActive (selectedAudioClip() != 0);
            break;
        case cmdAnalysisTools: info.setInfo ("Audio Analysis Tools...", "Install the tools for stems, notes and instruments, and the Whisper model for lyrics", "Options", 0); break;
        case cmdAiHumanize: info.setInfo ("Humanize...", "Make a part sound played by a person (Magenta Groove for drums, or simple)", "AI", 0); break;
        case cmdAiDrumify: info.setInfo ("Drumify...", "Make a drum part from the rhythm of a track (Magenta)", "AI", 0); break;
        case cmdAiContinue: info.setInfo ("Continue with AI...", "Let the AI go on from the bars before, or fill a gap (Magenta or the Anticipatory Music Transformer)", "AI", 0); break;
        case cmdAiWritePart: info.setInfo ("Write a Part...", "Write a new part that fits the other tracks, for example a bass line (Anticipatory Music Transformer)", "AI", 0); break;
        case cmdAiMelody: info.setInfo ("Melody over Chords...", "Write a melody over the chord symbols (Magenta)", "AI", 0); break;
        case cmdArrangementDoctor: info.setInfo ("Arrangement Doctor...", "Propose small, explained edits for a goal (a chorus that stands out, room for the melody, clearer busy bars, less repetition) and compare them by ear", "AI", 0); break;
        case cmdHardwareFit: info.setInfo ("Fit to My Synths...", "Check that the song fits the voices, parts and note range of your synths, and repair what doesn't", "AI", 0); break;
        case cmdSmartChords: info.setInfo ("Smart Chords...", "Type chords with next-chord ideas, play chord pads for the key, write the chords you play, and reharmonize a bar", "AI", 0);
            info.addDefaultKeypress ('k', cmd | shift); break;
        case cmdKidsMode:
            info.setInfo ("Kids Mode", "Pretend play: any key on a keyboard plays the song, and it pauses when the keys stop", "Transport", 0);
            info.setTicked (session.kidsMode());
            info.addDefaultKeypress ('k', cmd | juce::ModifierKeys::altModifier);
            break;
        case cmdCheckTranscription: info.setInfo ("Check the Transcription...", "After Analyse Audio: listen to the few places where the bass note or the chord is not sure, and choose (A/B)", "Audio", 0); break;
        case cmdStyleFromAudio: info.setInfo ("Style from Audio...", "Make a PSR style from a recording: sections and parts from its stems, refined until it plays like the recording", "Style", 0); break;
        case cmdAiTools: info.setInfo ("Install AI Music Tools...", "Install Magenta and the Anticipatory Music Transformer on this computer", "AI", 0); break;
        case cmdChooseSoundFont: info.setInfo ("Choose SoundFont...", "Load an SF2 file for the built-in instrument", "File", 0); break;
        case cmdDownloadSoundFont:
            info.setInfo (fontDownload != nullptr ? "Downloading GM SoundFont..." : "Download GM SoundFont...",
                          "Download the free GeneralUser GS sound set for the built-in instrument", "File", 0);
            info.setActive (fontDownload == nullptr);
            break;
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
        case cmdCut: info.setInfo ("Cut", "Copy the selected notes, clips or automation points, then delete them", "Edit", 0); info.addDefaultKeypress ('x', cmd); break;
        case cmdCopy: info.setInfo ("Copy", "Copy the selected notes, clips or automation points", "Edit", 0); info.addDefaultKeypress ('c', cmd); break;
        case cmdPaste:
            info.setInfo ("Paste", "Paste at the playhead on the selected track", "Edit", 0);
            info.addDefaultKeypress ('v', cmd);
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
        case cmdShowBeat: info.setInfo ("Drums", "Step sequencer for the selected track", "View", 0); info.addDefaultKeypress ('4', cmd); info.setTicked (view == viewBeat); break;
        case cmdShowClips: info.setInfo ("Loops", "Loops and scenes: start loops live", "View", 0); info.addDefaultKeypress ('5', cmd); info.setTicked (view == viewClips); break;
        case cmdShowMap: info.setInfo ("MIDI Map", "Instruments by app controls: choose the controller knob, button or pad for each cell", "View", 0); info.addDefaultKeypress ('6', cmd); info.setTicked (view == viewMap); break;
        case cmdCloseEditor: info.setInfo ("Close Note Editor", "Hide the piano roll", "View", 0); info.addDefaultKeypress (KP::escapeKey, 0); info.setActive (session.editedClip() != 0); break;
        case cmdAudioSettings: info.setInfo ("Audio and MIDI Settings...", "Choose devices", "Options", 0); info.addDefaultKeypress (',', cmd); break;
        case cmdScanPlugins: info.setInfo ("Scan for Plugins...", "Find the VST3 and Audio Unit plugins on this computer", "Options", 0); info.setActive (pluginHost != nullptr); break;
        case cmdMasterEffects: info.setInfo ("Master Effects...", "Effect plugins on the whole mix", "Options", 0); info.setActive (pluginHost != nullptr); break;
        case cmdAllowAi:
            info.setInfo ("Allow AI Assistants", "Let an AI assistant such as Claude Desktop read and change the song (only on this computer)", "Options", 0);
            info.setTicked (aiService != nullptr && aiService->isRunning());
            break;
        case cmdCopyAiSetup:
            info.setInfo ("Copy AI Assistant Setup", "Copy the settings that connect Claude Desktop or Claude Code to this app", "Options", 0);
            break;
        case cmdAiChat:
            info.setInfo ("AI Chat", "Show or hide the AI chat: tell the AI what to make or change, in plain words", "Options", 0);
            info.setTicked (aiPanelShown && aiPanel != nullptr && aiPanel->chatSelected());
            info.addDefaultKeypress ('j', cmd);
            break;
        case cmdShortcuts: info.setInfo ("Keyboard Shortcuts...", "Change the keys of the menu commands", "Options", 0); break;
        case cmdSpeech: info.setInfo ("Speech...", "Speak AI replies and use the microphone for voice input", "Options", 0); break;
        case cmdAbout: info.setInfo ("About Lazy Arranger Midi DAW", "Show the app version", "Options", 0); break;
        case cmdCheckForUpdates: info.setInfo ("Check for Updates...", "Look for a newer version on the public release page", "Options", 0); break;
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
        case cmdStyleFolder: chooseStyleFolder(); return true;
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
        case cmdExportPsr: exportPsr(); return true;
        case cmdExportAudio: exportAudio(); return true;
        case cmdSoundEditor: showSoundEditor(); return true;
        case cmdAnalyseAudio: showAnalysis (selectedAudioClip()); return true;
        case cmdAnalysisTools: showAnalysis (selectedAudioClip()); return true;
        case cmdAiHumanize: showAiTool ("ai_humanize"); return true;
        case cmdAiDrumify: showAiTool ("ai_drumify"); return true;
        case cmdAiContinue: showAiTool ("ai_continue"); return true;
        case cmdAiWritePart: showAiTool ("ai_write_part"); return true;
        case cmdAiMelody: showAiTool ("ai_melody"); return true;
        case cmdAiTools: showAiTool ("install"); return true;
        case cmdArrangementDoctor: showAiTool ("arrangement_doctor"); return true;
        case cmdHardwareFit: showAiTool ("hardware_fit"); return true;
        case cmdSmartChords: showAiTool ("chords"); return true;
        case cmdCheckTranscription: showAiTool ("transcription_questions"); return true;
        case cmdKidsMode: setKidsMode (! session.kidsMode()); return true;
        case cmdStyleFromAudio:
            showAiTool ("style_from_audio");
            aiPanel->chooseStyleClip (selectedAudioClip(), false);
            return true;
        case cmdImportAudio: importAudio(); return true;
        case cmdImportRecorder: importRecorder(); return true;
        case cmdChooseSoundFont: chooseSoundFont(); return true;
        case cmdDownloadSoundFont: downloadGmSoundFont(); return true;
        case cmdUndo: session.undo(); return true;
        case cmdRedo: session.redo(); return true;
        case cmdDelete:
            if (view == viewScore)
                score.deleteSelected();
            else if (editorHasFocus())
                pianoRoll.deleteSelected();
            else if (view == viewTimeline && arrangement.hasSelectedPoints())
                arrangement.deletePoints();
            else
                arrangement.deleteSelected();
            return true;
        case cmdCut: copyOrCut (true); return true;
        case cmdCopy: copyOrCut (false); return true;
        case cmdPaste: paste(); return true;
        case cmdDuplicate: arrangement.duplicateSelected(); return true;
        case cmdSplit: arrangement.splitAtPlayhead(); return true;
        case cmdSelectAll:
            if (view == viewScore)
                score.selectAll();
            else if (editorHasFocus())
                pianoRoll.selectAll();
            else if (view != viewTimeline || ! arrangement.selectAllPoints())
            {
                std::set<ClipId> all;
                for (const auto& t : session.song().tracks)
                {
                    for (const auto& c : t.clips)
                        all.insert (c->id);
                    for (const auto& a : t.audio)
                        all.insert (a->id);
                }
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
        case cmdShowMap: showView (viewMap); return true;
        case cmdCloseEditor: session.openInEditor (0); return true;
        case cmdAudioSettings: showAudioSettings(); return true;
        case cmdShortcuts: showShortcuts(); return true;
        case cmdSpeech: showSpeechSettings(); return true;
        case cmdAbout: showAbout(); return true;
        case cmdCheckForUpdates: checkForUpdates (false); return true;
        case cmdAllowAi: setAiAllowed (aiService == nullptr || ! aiService->isRunning()); return true;
        case cmdCopyAiSetup: copyAiSetup(); return true;
        case cmdAiChat: toggleAiChat(); return true;
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

juce::StringArray MainComponent::getMenuBarNames() { return { "File", "Edit", "Track", "Style", "Transport", "View", "Options", "AI" }; }

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
            add ({ cmdNew, cmdOpen, 0, cmdSave, cmdSaveAs, 0, cmdImportMidi, cmdExportMidi, cmdImportMusicXml, cmdExportMusicXml, cmdImportAudio, cmdImportRecorder, cmdExportAudio, 0, cmdFillFromStyle, 0, cmdExportPsr, 0, cmdChooseSoundFont, cmdDownloadSoundFont });
#if ! JUCE_MAC
            m.addSeparator();
            m.addCommandItem (&commands, juce::StandardApplicationCommandIDs::quit);
#endif
            break;
        case 1: add ({ cmdUndo, cmdRedo, 0, cmdCut, cmdCopy, cmdPaste, cmdDelete, cmdDuplicate, cmdSplit, cmdSelectAll, 0, cmdQuantise, cmdQuantiseOnRecord }); break;
        case 2: add ({ cmdAddTrack, cmdAddBus, 0, cmdSoundEditor, 0, cmdAnalyseAudio, cmdCheckTranscription }); break;
        case 3: add ({ cmdOpenStyle, cmdSaveStyle, 0, cmdFillFromStyle, 0, cmdMakeStyle, cmdSectionFromLoop, 0, cmdStyleFromAudio, 0, cmdStyleFolder }); break;
        case 4: add ({ cmdPlayStop, cmdGoToStart, cmdRecord, cmdLoop, cmdToggleMetronome, 0, cmdKidsMode }); break;
        case 5: add ({ cmdShowTimeline, cmdShowMixer, cmdShowBeat, cmdShowClips, cmdShowMap, cmdCloseEditor, 0, cmdDarkTheme, cmdLightTheme }); break;
        case 6: add ({ cmdAudioSettings, cmdShortcuts, 0, cmdSpeech, cmdDeviceFolder, 0, cmdScanPlugins, cmdMasterEffects, 0, cmdAiChat, cmdAllowAi, cmdCopyAiSetup, 0, cmdAnalysisTools, 0, cmdCheckForUpdates, cmdAbout }); break;
        case 7: add ({ cmdAiChat, 0, cmdAiHumanize, cmdAiDrumify, cmdAiContinue, cmdAiWritePart, cmdAiMelody, 0, cmdArrangementDoctor, cmdHardwareFit, cmdSmartChords, cmdStyleFromAudio, cmdCheckTranscription, 0, cmdAiTools }); break;
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
        chooser = std::make_unique<juce::FileChooser> ("Open style file", styleStartFolder(), "*.sty;*.STY;*.prs;*.PRS;*.sst;*.SST;*.pst;*.PST;*.pcs;*.PCS;*.bcs;*.BCS;*.fps;*.FPS");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) {
                                  if (fc.getResult() != juce::File())
                                      openFile (fc.getResult(), true);
                              });
    });
}

void MainComponent::chooseStyleToFill()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a style file", styleStartFolder(), "*.sty;*.STY;*.prs;*.PRS;*.sst;*.SST;*.pst;*.PST;*.pcs;*.PCS;*.bcs;*.BCS;*.fps;*.FPS");
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

    auto* w = new juce::AlertWindow ("Fill Timeline from a Style",
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
            showError ("Can't fill the Timeline", "The section " + section + " has no notes.");
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
    session.edit ("Fill Timeline from a Style", [&] (Song& song) {
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
    chooser = std::make_unique<juce::FileChooser> ("Save as style file", start, "*.sty;*.STY");
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
                                  showError ("Can't import audio", session.sampleProblem (file.getFullPathName().toStdString()));
                          });
}

void MainComponent::importRecorder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose an L6max recording folder (RECORDER > yymmdd_hhmmss)", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc) {
                              const auto folder = fc.getResult();
                              if (folder == juce::File())
                                  return;
                              juce::MouseCursor::showWaitCursor();
                              const auto problem = importRecorderFolder (folder);
                              juce::MouseCursor::hideWaitCursor();
                              if (problem.isNotEmpty())
                                  showError ("Can't import the L6max recording", problem);
                              else
                                  showView (viewTimeline);
                          });
}

juce::String MainComponent::importRecorderFolder (const juce::File& folder)
{
    std::vector<std::string> names;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*"))
        names.push_back (f.getFileName().toStdString());
    const auto tracks = recorder_import::zoomL6Project (names);
    if (tracks.empty())
        return "No L6max recording files (TRACK01_01.wav ... MASTER_01.wav) are in " + folder.getFullPathName() + ".";

    // Read every file first, so that a broken file stops the import before the song changes.
    struct Part
    {
        std::string path, name;
        double seconds = 0.0;
    };
    std::vector<std::vector<Part>> parts;
    for (const auto& t : tracks)
    {
        auto& list = parts.emplace_back();
        for (const auto& file : t.files)
        {
            const auto path = folder.getChildFile (juce::String::fromUTF8 (file.c_str())).getFullPathName().toStdString();
            const auto sample = session.sampleFor (path);
            if (sample == nullptr || sample->frames() == 0)
                return juce::String::fromUTF8 (file.c_str()) + ": " + session.sampleProblem (path);
            list.push_back ({ path, juce::File (path).getFileNameWithoutExtension().toStdString(), sample->seconds() });
        }
    }

    TrackId firstTrack = 0;
    session.edit ("Import L6max Recording", [&] (Song& s) {
        int after = static_cast<int> (s.tracks.size()) - 1;
        for (std::size_t i = 0; i < tracks.size(); ++i)
        {
            auto& track = edit::addAudioTrack (s, tracks[i].name, after);
            ++after;
            const auto id = track.id;
            if (firstTrack == 0)
                firstTrack = id;
            // The recorder's own mix is there to compare with: it starts muted.
            if (tracks[i].channel == 0)
                track.mixer.mute = true;
            // Files after the first continue the same recording (the recorder splits at 2 GB).
            double at = 0.0;
            for (const auto& p : parts[i])
            {
                AudioClip clip;
                clip.name = p.name;
                clip.file = p.path;
                clip.start = std::max<Tick> (0, static_cast<Tick> (std::llround (s.tempo.tickAtSeconds (at))));
                clip.length = std::max<Tick> (1, static_cast<Tick> (std::llround (s.tempo.tickAtSeconds (at + p.seconds))) - clip.start);
                at += p.seconds;
                edit::addAudioClip (s, id, std::move (clip));
            }
        }
    });
    if (firstTrack != 0)
        session.selectTrack (firstTrack);
    return {};
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

void MainComponent::exportPsr()
{
    psr_export::Plan plan;
    try
    {
        plan = psr_export::plan (session.song(), session.deviceLibrary());
    }
    catch (const std::exception& error)
    {
        showError ("Can't export for the PSR-S970", juce::String::fromUTF8 (error.what()));
        return;
    }
    if (plan.parts.empty())
    {
        showError ("Nothing to export", "No track has notes on the Timeline.");
        return;
    }

    juce::String text;
    for (const auto& p : plan.parts)
        text << juce::String::fromUTF8 (session.song().tracks[p.track].name.c_str()) << ": "
             << juce::String::fromUTF8 (p.voice.name.c_str()) << " (channel " << p.channel << ", " << juce::String::fromUTF8 (p.how.c_str()) << ")\n";
    for (const auto& s : plan.skipped)
        text << "Not in the file: " << juce::String::fromUTF8 (s.c_str()) << "\n";
    text << "\nTo use another voice, play the track on the PSR-S970 and choose the voice there. "
            "The music starts at bar 2: the keyboard gets its voices in bar 1.\n"
            "Copy the file to a USB stick and play it with the keyboard's Song player.";

    auto* window = new juce::AlertWindow ("Export Song for PSR-S970", text, juce::MessageBoxIconType::NoIcon, this);
    window->addButton ("Save...", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, plan] (int result) {
        if (result != 1)
            return;
        const auto base = session.file() != juce::File() ? session.file().getParentDirectory() : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        const auto start = base.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (session.song().title.c_str())) + " PSR.mid");
        chooser = std::make_unique<juce::FileChooser> ("Export song for PSR-S970", start, "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, plan] (const juce::FileChooser& fc) {
                                  if (fc.getResult() == juce::File())
                                      return;
                                  const auto bytes = psr_export::write (session.song(), plan);
                                  const auto file = fc.getResult().withFileExtension ("mid");
                                  if (! file.replaceWithData (bytes.data(), bytes.size()))
                                      showError ("Can't export for the PSR-S970", "Can't write " + file.getFullPathName());
                              });
    }), true);
}

void MainComponent::exportAudio()
{
    const auto base = session.file() != juce::File() ? session.file().getParentDirectory() : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto start = base.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (session.song().title.c_str())) + ".wav");
    chooser = std::make_unique<juce::FileChooser> ("Export audio file", start, "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              if (fc.getResult() == juce::File())
                                  return;
                              try
                              {
                                  juce::MouseCursor::showWaitCursor();
                                  session.renderToWav (fc.getResult().withFileExtension ("wav"));
                                  juce::MouseCursor::hideWaitCursor();
                              }
                              catch (const std::exception& error)
                              {
                                  juce::MouseCursor::hideWaitCursor();
                                  showError ("Can't export audio file", juce::String::fromUTF8 (error.what()));
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

void MainComponent::useSoundFont (const juce::File& f)
{
    session.loadSoundFont (f);
    if (settings != nullptr)
        settings->setValue ("soundFont", f.getFullPathName());
}

void MainComponent::findOrOfferDemoStyles()
{
    const auto thenSounds = [safe = juce::Component::SafePointer (this)]
    {
        if (safe != nullptr)
            safe->findOrOfferGmSoundFont();
    };

    if (settings == nullptr)
    {
        thenSounds();
        return;
    }
    if (settings->getBoolValue ("demoStylesOffered", false))
    {
        thenSounds();
        return;
    }

    const auto configured = settings->getValue ("styleFolder");
    if (configured.isNotEmpty() && juce::File (configured).isDirectory())
    {
        thenSounds();
        return;
    }

    const auto zip = findDemoStylesZip();
    if (! zip.existsAsFile())
    {
        thenSounds();
        return;
    }

    settings->setValue ("demoStylesOffered", true);
    const auto target = defaultStylesFolder();
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
        juce::MessageBoxIconType::QuestionIcon, "Demo styles",
        "The app found ArrangerMidiDaw-Demo-Styles.zip next to the program.\n\n"
        "Unpack 540 demo styles to:\n" + target.getFullPathName() + "\n\n"
        "The style folder will be set to this folder, so the AI assistant can find the styles by name.",
        "Unpack", "Not Now", this);
    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer (this), zip, thenSounds] (int result) {
        if (safe == nullptr)
            return;
        if (result == 1)
            safe->installDemoStyles (zip);
        thenSounds();
    });
}

void MainComponent::installDemoStyles (const juce::File& zipFile)
{
    const auto folder = defaultStylesFolder();
    if (folder.createDirectory().failed())
    {
        showError ("Demo styles", "Could not create " + folder.getFullPathName());
        return;
    }

    juce::ZipFile zip (zipFile);
    if (zip.getNumEntries() == 0)
    {
        showError ("Demo styles", "The demo style pack is empty or damaged.");
        return;
    }

    const auto result = zip.uncompressTo (folder, true);
    if (result.failed() || ! folder.getChildFile ("ArrangerMidiDaw-Demo-Styles").isDirectory())
    {
        showError ("Demo styles", "Could not unpack the demo styles to " + folder.getFullPathName()
                                      + (result.failed() ? "\n" + result.getErrorMessage() : ""));
        return;
    }

    SessionTools::setStyleFolder (folder);
    if (settings != nullptr)
        settings->setValue ("styleFolder", folder.getFullPathName());
}

void MainComponent::findOrOfferGmSoundFont()
{
    if (! session.song().soundFont.empty())
        return; // a project brought its own
    if (const auto found = gmsoundfont::findInstalled(); found.existsAsFile())
    {
        useSoundFont (found);
        return;
    }
    if (settings == nullptr || settings->getBoolValue ("gmSoundFontOffered"))
        return; // asked once; File > Download GM SoundFont stays available
    settings->setValue ("gmSoundFontOffered", true);

    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
        juce::MessageBoxIconType::QuestionIcon, "Download sounds?",
        "The built-in instrument needs a General MIDI SoundFont to make sound.\n\n"
        "Download GeneralUser GS now? It is free and about 32 MB.\n\n"
        "You can do this later with File > Download GM SoundFont, or use your own SF2 file with File > Choose SoundFont.",
        "Download", "Not Now", this);
    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer (this)] (int result) {
        if (safe != nullptr && result == 1)
            safe->downloadGmSoundFont();
    });
}

void MainComponent::downloadGmSoundFont()
{
    if (fontDownload != nullptr)
        return;
    if (const auto f = gmsoundfont::downloadedFile(); f.existsAsFile() && gmsoundfont::looksLikeSoundFont (f))
    {
        useSoundFont (f);
        return;
    }
    const auto before = session.soundFontStatus();
    session.setSoundFontStatus ("Downloading GeneralUser GS (32 MB)...");
    fontDownload = std::make_unique<gmsoundfont::Download> ([safe = juce::Component::SafePointer (this), before] (const juce::File& f, const juce::String& error) {
        if (safe == nullptr)
            return;
        safe->fontDownload.reset();
        safe->commands.commandStatusChanged();
        if (f.existsAsFile())
            safe->useSoundFont (f);
        else
        {
            safe->session.setSoundFontStatus (before);
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Download GM SoundFont", error);
        }
    });
    commands.commandStatusChanged();
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

// ---- Autosave and crash recovery ---------------------------------------------
juce::File MainComponent::autosaveFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")
#endif
        .getChildFile ("Arranger MIDI DAW").getChildFile ("Autosave");
}

void MainComponent::tickAutosave()
{
    if (autosaver == nullptr || previewMode)
        return;
    // Once a second is enough; the timer runs at up to 60 Hz.
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - lastAutosaveTickMs < 1000.0)
        return;
    lastAutosaveTickMs = now;
    const auto original = session.file() != juce::File() ? session.file().getFullPathName().toStdString() : std::string();
    autosaver->tick (session.doc(), original, autosave::nowMs());
}

void MainComponent::offerRecovery (std::function<void()> then)
{
    if (autosaver == nullptr)
    {
        then();
        return;
    }
    const auto orphans = autosave::findOrphans (toPath (autosaveFolder()), autosave::nowMs());
    if (orphans.empty())
    {
        then();
        return;
    }

    const auto entry = orphans.front();
    const auto name = ! entry.original.empty() ? juce::File (juce::String::fromUTF8 (entry.original.c_str())).getFileName()
                                               : (entry.title.empty() ? juce::String ("a new song") : juce::String::fromUTF8 (entry.title.c_str()));
    const auto when = juce::Time (entry.savedAtMs).toString (true, true, false);
    auto text = "The app closed before your changes to \"" + name + "\" were saved.\n\nAutosaved copy: " + when + ".";
    if (orphans.size() > 1)
        text << "\n\n" << juce::String (orphans.size() - 1) << " older copies wait for the next start.";

    auto options = juce::MessageBoxOptions::makeOptionsYesNoCancel (juce::MessageBoxIconType::QuestionIcon, "Recover unsaved work?", text,
                                                                    "Recover", "Discard", "Later", this);
    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer (this), entry, then] (int result) {
        if (safe == nullptr)
            return;
        if (result == 1) // Recover
        {
            try
            {
                auto song = autosave::recover (entry);
                const juce::File original (juce::String::fromUTF8 (entry.original.c_str()));
                safe->session.load (std::move (song), entry.original.empty() ? juce::File() : original);
                safe->session.doc().markUnsaved(); // in no file yet: Save keeps it
                safe->updateTitle();
                autosave::remove (entry);
                const auto path = entry.original;
                safe->autosaver->saveNow (safe->session.song(), safe->session.doc().revision(), path, autosave::nowMs());
            }
            catch (const std::exception& error)
            {
                showError ("Can't recover the song", juce::String::fromUTF8 (error.what())
                                                                + "\n\nThe copy stays in " + autosaveFolder().getFullPathName());
            }
            then();
        }
        else if (result == 2) // Discard: offer the next one, if any
        {
            autosave::remove (entry);
            safe->offerRecovery (then);
        }
        else // Later: keep the copies for the next start
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

    std::vector<std::pair<juce::String, SoundAddOn>> addOns;

    // A file is a controller page when it has a "controller:" line, a user sound list with
    // "soundsFor:", a device profile otherwise.
    const auto add = [&] (const juce::String& file, const juce::String& text) {
        try
        {
            if (device_profile::isSoundAddOn (text.toStdString()))
                addOns.push_back ({ file, device_profile::parseAddOn (text.toStdString()) });
            else if (text.startsWith ("controller:") || text.contains ("\ncontroller:"))
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
    for (const auto& [file, a] : addOns)
        if (! library.addSounds (a))
            problems.add (file + ": there is no device profile \"" + juce::String (a.profileId) + "\"");

    session.setDeviceLibrary (std::move (library));
    session.setControllers (std::move (controllers));
    mapView.refresh();
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

void MainComponent::saveUserSounds (const SoundAddOn& extra)
{
    const auto folder = deviceFolder();
    folder.createDirectory();
    const auto file = folder.getChildFile (juce::String (extra.profileId) + ".sounds.yaml");
    SoundAddOn all;
    all.profileId = extra.profileId;
    if (file.existsAsFile())
        all = device_profile::parseAddOn (file.loadFileAsString().toStdString());
    all.merge (extra.groups);
    if (! file.replaceWithText (juce::String::fromUTF8 (device_profile::writeAddOn (all).c_str())))
        throw std::runtime_error ("could not write " + file.getFullPathName().toStdString());
    loadDeviceProfiles();
}

std::unique_ptr<juce::Component> MainComponent::makeSoundEditor (const std::string& wanted, int page)
{
    auto id = wanted;
    if (id.empty())
    {
        // The selected track's synth, else the first one plugged in.
        if (const auto* t = edit::findTrack (session.song(), session.selectedTrack()); t != nullptr && t->output.kind == TrackOutput::Kind::midiPort)
            if (const auto& p = session.profileFor (*t); ! p.editor.empty())
                id = p.id;
        if (id.empty())
            if (const auto list = SoundEditorView::editableDevices (session); ! list.empty())
                id = list.front()->id;
    }
    auto view = std::make_unique<SoundEditorView> (session, id);
    view->showPage (page);
    return view;
}

void MainComponent::showSoundEditor()
{
    if (soundEditorWindow == nullptr)
    {
        struct Window final : juce::DocumentWindow
        {
            Window() : juce::DocumentWindow ("Sound Editor", currentTheme().colour ("panel"), juce::DocumentWindow::allButtons) {}
            void closeButtonPressed() override { delete this; }
        };
        auto* window = new Window();
        window->setUsingNativeTitleBar (true);
        window->setContentOwned (makeSoundEditor ({}).release(), true);
        window->setResizable (true, false);
        window->setResizeLimits (640, 420, 3000, 2000);
        window->centreWithSize (window->getWidth(), window->getHeight());
        window->setVisible (true);
        if (auto* view = dynamic_cast<SoundEditorView*> (window->getContentComponent()))
            window->setName (view->getName().isNotEmpty() ? view->getName() : "Sound Editor");
        soundEditorWindow = window;
    }
    else if (auto* view = dynamic_cast<SoundEditorView*> (soundEditorWindow->getContentComponent()))
    {
        // Follow the selected track's synth.
        if (const auto* t = edit::findTrack (session.song(), session.selectedTrack()); t != nullptr && t->output.kind == TrackOutput::Kind::midiPort)
            if (const auto& p = session.profileFor (*t); ! p.editor.empty())
                view->showDevice (p.id);
    }
    soundEditorWindow->toFront (true);
}

void MainComponent::startAutomaticUpdateCheck()
{
    if (automaticUpdateCheckStarted || settings == nullptr)
        return;

    automaticUpdateCheckStarted = true;
    juce::Timer::callAfterDelay (3500, [safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe != nullptr)
            safe->checkForUpdates (true);
    });
}

void MainComponent::showAbout()
{
    const auto version = update::currentVersion();
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
        juce::MessageBoxIconType::InfoIcon,
        "About Lazy Arranger Midi DAW",
        "Lazy Arranger Midi DAW\n"
        "Version " + (version.isNotEmpty() ? version : juce::String ("unknown")) + "\n\n"
        "MIDI arranger DAW with AI assist.\n"
        "Windows, macOS and Linux\n\n"
        "https://github.com/nhth-mibu/lazy-arranger-midi-daw-releases",
        "Website", "Close", this);

    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<MainComponent> (this)] (int result) {
        if (safe != nullptr && result == 1)
            safe->openReleasePage ("https://github.com/nhth-mibu/lazy-arranger-midi-daw-releases");
    });
}

void MainComponent::checkForUpdates (bool automatic)
{
    if (settings == nullptr && automatic)
        return;

    if (updateCheckInFlight)
    {
        if (! automatic)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Check for Updates",
                                                    "The app is already checking for an update.", juce::String(), this);
        return;
    }

    updateCheckInFlight = true;
    if (updateChecker == nullptr)
        updateChecker = std::make_unique<update::Checker>();

    updateChecker->check ([safe = juce::Component::SafePointer<MainComponent> (this), automatic]
                          (std::optional<update::Release> release, juce::String error) {
        if (safe == nullptr)
            return;

        safe->updateCheckInFlight = false;

        if (! release.has_value())
        {
            if (! automatic)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Check for Updates",
                                                        error.isNotEmpty() ? error : "The app could not read the latest release.",
                                                        juce::String(), safe.getComponent());
            return;
        }

        const auto current = update::currentVersion();
        if (! arranger::version::isNewer (current.toStdString(), release->version.toStdString()))
        {
            if (! automatic)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                        "Check for Updates",
                                                        "You have the latest version (" + current + ").",
                                                        juce::String(), safe.getComponent());
            return;
        }

        if (automatic && safe->settings != nullptr)
        {
            const auto skipped = safe->settings->getValue ("skippedUpdateVersion");
            if (skipped.isNotEmpty()
                && ! arranger::version::isNewer (skipped.toStdString(), release->version.toStdString()))
                return;
        }

        safe->showUpdateAvailable (*release);
    });
}

void MainComponent::showUpdateAvailable (const update::Release& release)
{
    auto options = juce::MessageBoxOptions::makeOptionsYesNoCancel (
        juce::MessageBoxIconType::InfoIcon,
        "Update available",
        "Lazy Arranger Midi DAW " + release.version + " is available.\n\n"
        "You have " + update::currentVersion() + ".\n\n"
        "Download the new version from the public release page.",
        "Download", "Later", "Skip This Version", this);

    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<MainComponent> (this), release] (int result) {
        if (safe == nullptr)
            return;

        if (result == 1)
            safe->openReleasePage (release.url);
        else if (result == 3 && safe->settings != nullptr)
            safe->settings->setValue ("skippedUpdateVersion", release.version);
    });
}

void MainComponent::openReleasePage (const juce::String& url)
{
    if (url.startsWith ("https://") && juce::URL (url).launchInDefaultBrowser())
        return;

    juce::SystemClipboard::copyTextToClipboard (url);
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Download",
                                            "Open this page in a browser:\n\n" + url + "\n\nThe link is copied to the clipboard.",
                                            juce::String(), this);
}

void MainComponent::showSpeechSettings()
{
    if (speechService == nullptr)
        return;
    if (speechWindow != nullptr)
        delete speechWindow.getComponent();

    auto view = std::make_unique<SpeechView> (*speechService);
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Speech";
    options.dialogBackgroundColour = currentTheme().colour ("panel");
    options.content.setOwned (view.release());
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    speechWindow = options.launchAsync();
    if (speechWindow != nullptr)
        speechWindow->toFront (true);
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

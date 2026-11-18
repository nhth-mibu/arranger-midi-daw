#include "AudioAnalysis.h"
#include "StyleFromAudio.h"
#include "ui/AnalysisView.h"
#include "ui/MainComponent.h"
#include "ui/AiToolsView.h"
#include "ui/SlicerView.h"
#include "ui/StyleRuleEditor.h"

#include "arranger/core/ChordInput.h"
#include "arranger/core/MusicXml.h"
#include "arranger/core/ProjectFile.h"
#include "arranger/core/Style.h"

#include <BinaryData.h>

#include <iostream>

namespace
{

/** Command-line options. --screenshot renders the window to a PNG and quits (used by CI). */
struct Options
{
    juce::File screenshot;
    juce::File project;
    bool lightTheme = false;
    int width = 1600, height = 1000;
    double positionTicks = 0.0;
    int view = 0;
    int trackHeight = 0; ///< with --screenshot: the height of the Timeline's track lanes
    juce::File fillStyle;      ///< fill tracks from this style file before the screenshot
    juce::String fillChords = "C Am F G";
    int fillBars = 8;
    int gridFromTimeline = 0;  ///< fill the Clip grid from the Timeline, this many bars per scene
    juce::File gridAudio;      ///< put this WAV file in the Clip grid as a loop, on a new audio track
    int mixer = 0; ///< height of the mixer panel in a screenshot; 0 hides it
    bool openEditor = false;
    bool speech = false; ///< open the Speech window (screenshots)
    bool rules = false; ///< screenshot the chord rule editor of the first style part
    juce::File exportStyle; ///< write the project as a .sty file and quit
    juce::File exportMusicXml; ///< write the score as a MusicXML file and quit
    juce::File exportPdf;   ///< write the score of the selected track as a PDF and quit
    bool scoreMarks = false; ///< put sample chord symbols, lyrics and marks on the selected track's score
    juce::String scoreKeys; ///< type these keys into the Score view (for screenshots of note input)
    juce::String track;     ///< select the track with this name first
    bool scanPlugins = false;  ///< scan for plugins, print the list and quit
    juce::File pluginFolder;   ///< with --scan-plugins: one more folder to look in
    juce::File render;         ///< play the project to this WAV file and quit
    double renderSeconds = 0.0;
    juce::String setParam;     ///< with --render: "Name=0.2" sets this control (0 to 1) on every effect plugin first
    juce::File saveProject;    ///< with --render: save the project (with the plugins' settings) to this file
    juce::String audioToMidi; ///< with --audio: "melody" or "drums" converts the new audio clip to MIDI
    juce::File audio; ///< put this WAV file on a new audio track before the screenshot
    juce::String analyse;     ///< with --audio: "all" or a list such as "chords,lyrics" analyses the new clip (Track > Analyse Audio) and prints what it found
    juce::String language;    ///< with --analyse: the language of the lyrics
    bool analysisView = false; ///< screenshot the Analyse Audio window instead of the main window
    juce::String automation; ///< with --track: show this line ("volume", "pan", "reverb", "chorus" or "plugin") with sample points on the track
    juce::String findChords;  ///< in the Score view: find chords ("bar", "half" or "beat") before the screenshot
    bool guitarChords = false; ///< in the Score view: show guitar chord boxes
    bool allowAi = false;     ///< switch on Options > Allow AI Assistants at start
    juce::String aiChat;      ///< open the AI chat and send this message
    bool chat = false;        ///< open the AI chat without sending a message
    juce::File exportAudio;   ///< after opening, save the song's sound here (as File > Export Audio File does)
    juce::File styleFolder;   ///< where the AI assistant looks for styles
    juce::String aiAssistant; ///< the assistant for the AI chat (claude-code, codex, gemini, api-anthropic, api-deepseek, api-openai)
    bool buses = false;       ///< add two sample buses (a group and a reverb return) before the screenshot
    bool shortcuts = false;   ///< screenshot the Keyboard Shortcuts window instead of the main window
    juce::String soundEditor; ///< screenshot the sound editor of this device profile id instead
    juce::String installAi;   ///< "magenta" or "all": install the AI music tools, print what happens and quit
    juce::String arrangeGoal; ///< with --ai-dock: the Arrangement Doctor goal, and "--arrange-bars 9-16"
    int arrangeFrom = 0, arrangeTo = 0;
    juce::String typeChords;  ///< write these chords to the chord lane from bar 1 ("Am F | C G", roman numerals too)
    int reharmBar = 0;        ///< with --ai-tool chords: reharmonize the chord in this bar (1-based)
    juce::String aiTool;      ///< screenshot the window of this AI tool (ai_humanize, ai_write_part, install...) instead
    bool aiDock = false;      ///< with --ai-tool: show the tool in the right AI panel instead of a separate window
    int soundPage = 0;        ///< the page of the sound editor
    bool pluginPanel = false; ///< screenshot the plugin list of the selected track instead of the main window
    juce::File slice; ///< screenshot the slicer with this loop instead of the main window
    bool styleFromAudio = false; ///< with --audio: make a style from the clip (the song's MIDI tracks stand in for its analysis), print it
    juce::File styleFromAudioOut; ///< and save it here

    static Options parse (const juce::StringArray& args)
    {
        Options o;
        for (int i = 0; i < args.size(); ++i)
        {
            const auto& a = args[i];
            const auto next = [&] { return i + 1 < args.size() ? args[++i] : juce::String(); };
            if (a == "--screenshot") o.screenshot = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            else if (a == "--theme") o.lightTheme = next() == "light";
            else if (a == "--size")
            {
                const auto s = next();
                o.width = s.upToFirstOccurrenceOf ("x", false, false).getIntValue();
                o.height = s.fromFirstOccurrenceOf ("x", false, false).getIntValue();
            }
            else if (a == "--position") o.positionTicks = next().getDoubleValue();
            else if (a == "--track-height") o.trackHeight = next().getIntValue();
            else if (a == "--view") { const auto v = next(); o.view = v == "score" ? 1 : (v == "beat" ? 2 : (v == "clips" ? 3 : (v == "map" ? 4 : 0))); if (v == "mixer") o.mixer = 1000; }
            else if (a == "--mixer") { o.mixer = 280; }
            else if (a == "--grid-audio") { o.gridAudio = juce::File::getCurrentWorkingDirectory().getChildFile (next()); }
            else if (a == "--fill-style") { o.fillStyle = juce::File::getCurrentWorkingDirectory().getChildFile (next()); }
            else if (a == "--fill-chords") { o.fillChords = next(); }
            else if (a == "--fill-bars") { o.fillBars = next().getIntValue(); }
            else if (a == "--grid-from-timeline") { o.gridFromTimeline = next().getIntValue(); }
            else if (a == "--mixer-height") { o.mixer = next().getIntValue(); }
            else if (a == "--editor") o.openEditor = true;
            else if (a == "--speech") o.speech = true;
            else if (a == "--rules") o.rules = true;
            else if (a == "--track") o.track = next();
            else if (a == "--score-keys") o.scoreKeys = next();
            else if (a == "--score-marks") o.scoreMarks = true;
            else if (a == "--export-musicxml") o.exportMusicXml = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            else if (a == "--export-pdf") o.exportPdf = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            else if (a == "--export-style") o.exportStyle = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            else if (a == "--audio-to-midi") o.audioToMidi = next();
            else if (a == "--analyse") o.analyse = next();
            else if (a == "--language") o.language = next();
            else if (a == "--analysis-view") o.analysisView = true;
            else if (a == "--automation") o.automation = next();
            else if (a == "--plugin-panel") o.pluginPanel = true;
            else if (a == "--shortcuts") o.shortcuts = true;
            else if (a == "--sound-editor") o.soundEditor = next();
            else if (a == "--ai-tool") o.aiTool = next();
            else if (a == "--ai-dock") o.aiDock = true;
            else if (a == "--type-chords") o.typeChords = next();
            else if (a == "--reharm-bar") o.reharmBar = next().getIntValue();
            else if (a == "--style-from-audio")
            {
                o.styleFromAudio = true;
                if (i + 1 < args.size() && ! args[i + 1].startsWith ("--"))
                    o.styleFromAudioOut = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            }
            else if (a == "--arrange-goal") o.arrangeGoal = next();
            else if (a == "--arrange-bars") { const auto b = next(); o.arrangeFrom = b.upToFirstOccurrenceOf ("-", false, false).getIntValue(); o.arrangeTo = b.fromFirstOccurrenceOf ("-", false, false).getIntValue(); }
            else if (a == "--install-ai-tools") o.installAi = next();
            else if (a == "--sound-page") o.soundPage = next().getIntValue();
            else if (a == "--buses") o.buses = true;
            else if (a == "--allow-ai") o.allowAi = true;
            else if (a == "--ai-chat") o.aiChat = next();
            else if (a == "--chat") o.chat = true;
            else if (a == "--export-audio") o.exportAudio = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            else if (a == "--ai-assistant") o.aiAssistant = next();
            else if (a == "--style-folder") o.styleFolder = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            else if (a == "--find-chords") o.findChords = next();
            else if (a == "--guitar-chords") o.guitarChords = true;
            else if (a == "--scan-plugins") { o.scanPlugins = true; if (i + 1 < args.size() && ! args[i + 1].startsWith ("--")) o.pluginFolder = juce::File::getCurrentWorkingDirectory().getChildFile (next()); }
            else if (a == "--render") o.render = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            else if (a == "--set-param") o.setParam = next();
            else if (a == "--save-project") o.saveProject = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            else if (a == "--seconds") o.renderSeconds = next().getDoubleValue();
            else if (a == "--audio") o.audio = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            else if (a == "--slice") o.slice = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            else if (a.endsWith (arranger::project_file::kExtension) || a.endsWithIgnoreCase (".mid") || a.endsWithIgnoreCase (".midi") || a.endsWithIgnoreCase (".musicxml") || a.endsWithIgnoreCase (".mxl") || arranger::style_file::hasStyleExtension (a.toStdString())) o.project = juce::File::getCurrentWorkingDirectory().getChildFile (a);
        }
        return o;
    }
};

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (arranger::ui::MainComponent* content)
        : DocumentWindow ("Lazy Arranger Midi DAW", arranger::ui::currentTheme().colour ("background"), allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (true, true);
        setResizeLimits (1100, 680, 10000, 10000);
        // The title bar and taskbar icon on Windows and Linux (macOS uses the bundle's icon).
        setIcon (juce::ImageCache::getFromMemory (BinaryData::appicon256_png, BinaryData::appicon256_pngSize));

        content->onTitleChanged = [this] (const juce::String& t) { setName (t + "  \xe2\x80\x94  Lazy Arranger Midi DAW"); };

#if JUCE_MAC
        juce::PopupMenu appMenu;
        appMenu.addCommandItem (&content->commandManager(), arranger::ui::MainComponent::cmdAbout);
        appMenu.addCommandItem (&content->commandManager(), arranger::ui::MainComponent::cmdCheckForUpdates);
        juce::MenuBarModel::setMacMainMenu (content, &appMenu);
#else
        setMenuBar (content);
#endif
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    ~MainWindow() override
    {
#if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (nullptr);
#else
        setMenuBar (nullptr);
#endif
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

} // namespace

class ArrangerApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Lazy Arranger Midi DAW"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    /** A start with an option (--screenshot, --scan-plugin...) is a job of its own, next to an open app. */
    bool moreThanOneInstanceAllowed() override { return getCommandLineParameters().contains ("--"); }

    std::unique_ptr<arranger::MidiAiService> aiInstall;

    void initialise (const juce::String&) override
    {
        // The child side of a plugin scan: look at one plugin file, write what it holds, quit.
        if (const auto args = getCommandLineParameterArray(); args.size() >= 4 && args[0] == "--scan-plugin")
        {
            setApplicationReturnValue (arranger::PluginHost::runScanChild ({ args[1], args[2], args[3] }));
            quit();
            return;
        }

        const auto options = Options::parse (getCommandLineParameterArray());
        plugins.load();

        if (options.scanPlugins)
        {
            // Headless scan: the standard folders and, if given, one more folder.
            juce::FileSearchPath extra;
            if (options.pluginFolder != juce::File())
                extra.add (options.pluginFolder);
            int failed = 0;
            for (const auto& entry : plugins.filesToScan (extra))
                failed += plugins.scanFile (entry) ? 0 : 1;
            plugins.save();
            for (const auto& list : { plugins.instruments(), plugins.effects() })
                for (const auto& d : list)
                    std::cout << (d.isInstrument ? "instrument" : "effect") << " | " << d.name << " | " << d.pluginFormatName << " | " << d.createIdentifierString() << std::endl;
            std::cout << plugins.pluginCount() << " plugins, " << failed << " files failed" << std::endl;
            quit();
            return;
        }

        if (options.installAi.isNotEmpty())
        {
            // Headless: the same install as AI > Install AI Music Tools.
            aiInstall = std::make_unique<arranger::MidiAiService>();
            const auto error = aiInstall->install (options.installAi == "all", [] (const juce::String& line) { std::cout << line << std::endl; },
                                                   [this] (const juce::String& e) {
                                                       std::cout << (e.isEmpty() ? juce::String ("Installed. ") + arranger::MidiAiService::statusText() : e) << std::endl;
                                                       setApplicationReturnValue (e.isEmpty() ? 0 : 1);
                                                       quit();
                                                   });
            if (error.isNotEmpty())
            {
                std::cout << error << std::endl;
                quit();
            }
            return;
        }

        if (options.render != juce::File())
        {
            juce::MessageManager::callAsync ([this, options] { setApplicationReturnValue (renderAudio (options) ? 0 : 1); quit(); });
            return;
        }

        if (options.exportStyle != juce::File())
        {
            // Headless: turn a project's Clip grid into a Yamaha style file.
            int result = 1;
            try
            {
                auto song = arranger::project_file::load (options.project.getFullPathName().toStdString());
                arranger::style_convert::makeStyle (song);
                arranger::style_file::save (arranger::style_convert::fromSong (song), options.exportStyle.getFullPathName().toStdString());
                std::cout << "Wrote " << options.exportStyle.getFullPathName() << std::endl;
                result = 0;
            }
            catch (const std::exception& e)
            {
                std::cerr << e.what() << std::endl;
            }
            setApplicationReturnValue (result);
            quit();
            return;
        }

        if (options.exportPdf != juce::File() || options.exportMusicXml != juce::File())
        {
            juce::MessageManager::callAsync ([this, options] { setApplicationReturnValue (writePdf (options) ? 0 : 1); quit(); });
            return;
        }

        if (options.screenshot != juce::File() && (options.shortcuts || options.soundEditor.isNotEmpty()))
        {
            // The Keyboard Shortcuts window or a sound editor. Lists fill a moment after they show, so the picture waits.
            devices = std::make_unique<arranger::platform::JuceDeviceHost> (engine);
            auto content = std::make_shared<arranger::ui::MainComponent> (engine, *devices, nullptr, &plugins);
            content->setThemeDark (! options.lightTheme);
            std::shared_ptr<juce::Component> editor (options.shortcuts ? content->makeShortcutEditor().release()
                                                                       : content->makeSoundEditor (options.soundEditor.toStdString(), options.soundPage).release());
            editor->setLookAndFeel (&content->getLookAndFeel());
            editor->addToDesktop (0);
            editor->setVisible (true);
            juce::Timer::callAfterDelay (500, [this, content, editor, file = options.screenshot] {
                juce::Image whole (juce::Image::ARGB, editor->getWidth() * 2, editor->getHeight() * 2, true);
                {
                    juce::Graphics g (whole);
                    g.fillAll (arranger::ui::currentTheme().colour ("panel"));
                    g.drawImageAt (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f), 0, 0);
                }
                editor->setLookAndFeel (nullptr);
                file.getParentDirectory().createDirectory();
                file.deleteFile();
                juce::FileOutputStream out (file);
                juce::PNGImageFormat png;
                const bool ok = out.openedOk() && png.writeImageToStream (whole, out);
                std::cout << (ok ? "Wrote " : "Failed to write ") << file.getFullPathName() << std::endl;
                setApplicationReturnValue (ok ? 0 : 1);
                quit();
            });
            return;
        }

        if (options.screenshot != juce::File())
        {
            juce::MessageManager::callAsync ([this, options] { setApplicationReturnValue (takeScreenshot (options) ? 0 : 1); quit(); });
            return;
        }

        juce::PropertiesFile::Options p;
        p.applicationName = "Arranger MIDI DAW";
        p.filenameSuffix = ".settings";
        p.folderName = "Arranger MIDI DAW";
        p.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (p);
        auto* settings = properties.getUserSettings();

        devices = std::make_unique<arranger::platform::JuceDeviceHost> (engine);
        const auto savedDevices = settings->getXmlValue ("audioDevice");
        devices->start (savedDevices.get(), true);

        auto* content = new arranger::ui::MainComponent (engine, *devices, settings, &plugins);
        content->commandManager().registerAllCommandsForTarget (this);
        content->restoreShortcuts();
        window = std::make_unique<MainWindow> (content);
        mainComponent = content;
        content->startAutomaticUpdateCheck();
        if (options.allowAi)
            content->setAiAllowed (true);
        if (options.speech)
            content->openSpeechSettings();

        if (options.project != juce::File())
            content->openFile (options.project, true);
        else if (const juce::File last (settings->getValue ("lastProject")); last.existsAsFile())
            content->openFile (last, false);

        if (options.styleFolder != juce::File())
        {
            settings->setValue ("styleFolder", options.styleFolder.getFullPathName());
            arranger::SessionTools::setStyleFolder (options.styleFolder);
        }
        if (options.aiAssistant.isNotEmpty())
            settings->setValue ("aiChoice", options.aiAssistant);
        if (options.aiChat.isNotEmpty())
            content->openAiChat (options.aiChat);
        else if (options.chat)
            content->openAiChat();
        if (options.aiDock)
            content->showAiTool (options.aiTool.isNotEmpty() ? options.aiTool : juce::String ("ai_humanize"));
        if (options.exportAudio != juce::File())
        {
            // The SoundFont loads in the background first: try again until it is ready.
            auto attempt = std::make_shared<std::function<void (int)>>();
            *attempt = [content, file = options.exportAudio, attempt] (int left) {
                try
                {
                    const double seconds = content->getSession().renderToWav (file);
                    std::cout << "Wrote " << file.getFullPathName() << " (" << seconds << " s)" << std::endl;
                }
                catch (const std::exception& e)
                {
                    if (left > 0 && juce::String (e.what()).contains ("still loading"))
                        juce::Timer::callAfterDelay (1000, [attempt, left] { (*attempt) (left - 1); });
                    else
                        std::cerr << e.what() << std::endl;
                }
            };
            juce::Timer::callAfterDelay (1000, [attempt] { (*attempt) (120); });
        }
    }

    void shutdown() override
    {
        aiInstall.reset();
        if (devices != nullptr)
            if (auto state = devices->saveState())
                properties.getUserSettings()->setValue ("audioDevice", state.get());
        properties.saveIfNeeded();
        window = nullptr;
        devices = nullptr;
        // The audio has stopped: free the plugins now, while their formats are still here.
        engine.setRacks (nullptr);
        engine.collectGarbage();
    }

    void systemRequestedQuit() override
    {
        if (mainComponent != nullptr)
            mainComponent->confirmDiscard ([] { juce::JUCEApplication::quit(); });
        else
            quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        const auto o = Options::parse (juce::StringArray::fromTokens (commandLine, true));
        if (mainComponent != nullptr && o.project != juce::File())
            mainComponent->confirmDiscard ([this, o] { mainComponent->openFile (o.project, true); });
    }

private:
    static void selectTrack (arranger::Session& session, const juce::String& name)
    {
        for (const auto& t : session.song().tracks)
            if (name.isNotEmpty() && juce::String::fromUTF8 (t.name.c_str()).equalsIgnoreCase (name))
                session.selectTrack (t.id);
    }

    /** Writes the score of the project's selected track to a PDF, with no window. */
    bool writePdf (const Options& o)
    {
        devices = std::make_unique<arranger::platform::JuceDeviceHost> (engine);
        arranger::ui::MainComponent content (engine, *devices, nullptr, &plugins);
        if (! content.openFile (o.project, false))
        {
            std::cerr << "Can't open " << o.project.getFullPathName() << std::endl;
            return false;
        }
        if (o.exportMusicXml != juce::File())
        {
            try
            {
                arranger::musicxml::save (content.getSession().song(), o.exportMusicXml.getFullPathName().toStdString());
                std::cout << "Wrote " << o.exportMusicXml.getFullPathName() << std::endl;
            }
            catch (const std::exception& e)
            {
                std::cerr << e.what() << std::endl;
                return false;
            }
        }
        if (o.exportPdf == juce::File())
            return true;
        selectTrack (content.getSession(), o.track);
        arranger::ui::ScoreView score (content.getSession());
        if (o.track.isNotEmpty()) // one named track: its part; no name: the full score
            score.setShownTracks (arranger::ui::ScoreView::Tracks::selected);
        if (! score.exportPdf (o.exportPdf))
            return false;
        std::cout << "Wrote " << o.exportPdf.getFullPathName() << std::endl;
        return true;
    }

    /** Analyses the selected audio clip (as Track > Analyse Audio does, but waiting for it) and prints what it found. */
    static bool analyseAudio (arranger::ui::MainComponent& content, const Options& o)
    {
        auto& session = content.getSession();
        arranger::ClipId clip = 0;
        for (const auto id : session.selectedClips())
            if (arranger::edit::findAudioClip (session.song(), id) != nullptr)
                clip = id;
        if (clip == 0)
        {
            std::cerr << "--analyse needs an audio clip (--audio)" << std::endl;
            return false;
        }
        const auto all = o.analyse == "all";
        const auto wants = [&] (const char* what) { return all || o.analyse.containsIgnoreCase (what); };
        arranger::AnalysisService::Request r;
        r.clip = clip;
        r.stems = wants ("stems");
        r.notes = wants ("notes");
        r.chords = wants ("chords");
        r.instruments = wants ("instruments");
        r.lyrics = wants ("lyrics");
        if (o.language.isNotEmpty())
            r.language = o.language;
        juce::String lastStep;
        const auto error = session.analysis().runNow (r, [&] (double p, const juce::String& step) {
            if (step != lastStep)
                std::cout << juce::roundToInt (p * 100.0) << "% " << step << std::endl;
            lastStep = step;
        });
        const auto st = session.analysis().status();
        for (const auto& w : st.warnings)
            std::cout << "Warning: " << w << std::endl;
        if (error.isNotEmpty())
        {
            std::cerr << "Analysis failed: " << error << std::endl;
            return false;
        }
        std::cout << "Stem tracks: " << st.applied.audioTracks.size() << ", MIDI tracks: " << st.applied.midiTracks.size() << std::endl;
        for (const auto& t : session.song().tracks)
            if (! t.clips.empty())
                std::cout << "  " << t.name << ": " << t.clips.front()->notes.size() << " notes, " << t.sound.name << std::endl;
        for (const auto& [stem, heard] : st.instruments)
            std::cout << "Instrument in " << stem << ": " << heard << std::endl;
        std::cout << "Chords:";
        for (const auto& c : st.chords)
            std::cout << " " << c;
        std::cout << std::endl;
        if (! st.lyricLines.empty())
            std::cout << "Lyrics (" << st.language << "):" << std::endl;
        for (const auto& l : st.lyricLines)
            std::cout << "  " << l << std::endl;
        return true;
    }

    /**
        Style from Audio on the selected audio clip, as the AI panel does it, but
        waiting for it. The song's MIDI tracks stand in for the analysis when the
        clip has none (so it runs without the Python tools). Prints the result.
    */
    static bool styleFromAudio (arranger::ui::MainComponent& content, const Options& o)
    {
        auto& session = content.getSession();
        arranger::ClipId clip = 0;
        for (const auto id : session.selectedClips())
            if (arranger::edit::findAudioClip (session.song(), id) != nullptr)
                clip = id;
        if (clip == 0)
        {
            std::cerr << "--style-from-audio needs an audio clip (--audio)" << std::endl;
            return false;
        }
        const auto file = arranger::edit::findAudioClip (session.song(), clip)->file;
        auto result = session.analysis().resultFor (clip);
        if (! result)
            result = arranger::style_from_audio::resultFromSong (session.song(), file);
        arranger::StyleFromAudioService::Request r;
        r.clip = clip;
        auto& service = session.styleFromAudio();
        const auto error = service.makeNow (r, *result);
        if (error.isNotEmpty())
        {
            std::cerr << "Style from Audio failed: " << error << std::endl;
            return false;
        }
        const auto& m = *service.status().made;
        std::cout << "Style \"" << m.style.name << "\": " << m.grid.bpm << " BPM, bar 1 at " << m.grid.firstDownbeat << " s, " << m.plan.bars << " bars" << std::endl;
        std::cout << "Match " << juce::roundToInt (m.best.total * 100) << "% (before refining " << juce::roundToInt (m.first.total * 100) << "%, " << m.tried << " versions compared)" << std::endl;
        for (const auto& sec : m.best.sections)
        {
            std::cout << "  " << sec.section << ": bars " << arranger::style_from_audio::barsOf (m.plan, sec.section);
            if (sec.bars > 0)
            {
                std::cout << ", " << juce::roundToInt (sec.total * 100) << "%:";
                for (const auto& p : sec.parts)
                    std::cout << " " << p.name << " " << juce::roundToInt (p.total * 100) << "%";
            }
            std::cout << std::endl;
        }
        for (const auto& step : m.steps)
            std::cout << "  kept: " << step.change << std::endl;
        for (const auto& w : m.warnings)
            std::cout << "Warning: " << w << std::endl;
        if (o.styleFromAudioOut != juce::File())
        {
            auto out = o.styleFromAudioOut;
            const auto saveError = service.save (out);
            if (saveError.isNotEmpty())
            {
                std::cerr << saveError << std::endl;
                return false;
            }
            std::cout << "Wrote " << out.getFullPathName() << std::endl;
        }
        return true;
    }

    /** Renders the main window off-screen to a PNG at 2x, with no audio device. */
    bool takeScreenshot (const Options& o)
    {
        devices = std::make_unique<arranger::platform::JuceDeviceHost> (engine);
        arranger::ui::MainComponent content (engine, *devices, nullptr, &plugins);
        content.setThemeDark (! o.lightTheme);
        if (o.project != juce::File() && ! content.openFile (o.project, false))
        {
            std::cerr << "Can't open " << o.project.getFullPathName() << std::endl;
            return false;
        }
        if (o.audio != juce::File() && ! content.timeline().addAudioFile (o.audio, 0, 0))
        {
            std::cerr << "Can't read " << o.audio.getFullPathName() << std::endl;
            return false;
        }
        if (o.audioToMidi.isNotEmpty())
        {
            const auto clips = content.getSession().selectedClips();
            if (clips.empty() || ! content.timeline().convertToMidi (*clips.begin(), o.audioToMidi == "drums"))
                std::cerr << "No notes found" << std::endl;
        }
        if (o.analyse.isNotEmpty() && ! analyseAudio (content, o))
            return false;
        if (o.styleFromAudio && ! styleFromAudio (content, o))
            return false;
        if (o.fillStyle != juce::File() && ! content.fillFromStyle (o.fillStyle, "Main A", o.fillChords, o.fillBars, 1, false))
        {
            std::cerr << "Can't fill from " << o.fillStyle.getFullPathName() << std::endl;
            return false;
        }
        if (o.gridFromTimeline > 0)
        {
            const arranger::Tick length = content.getSession().song().timeSignature.ticksPerBar() * o.gridFromTimeline;
            content.getSession().edit ("Fill Clip Grid from Timeline", [length] (arranger::Song& s) { arranger::edit::slotsFromTimeline (s, length); });
        }
        if (o.gridAudio != juce::File())
        {
            if (! content.clipGridView().addAudioToSlot (o.gridAudio, 0, 0))
            {
                std::cerr << "Can't read " << o.gridAudio.getFullPathName() << std::endl;
                return false;
            }
            content.clipGridView().addAudioToSlot (o.gridAudio, content.getSession().selectedTrack(), 2);
        }
        selectTrack (content.getSession(), o.track);
        if (o.typeChords.isNotEmpty())
        {
            auto& session = content.getSession();
            const auto& song = session.song();
            const auto typed = arranger::chord_input::typeChords (o.typeChords.toStdString(), arranger::chord_input::songKey (song), song.timeSignature, 0);
            session.edit ("Type Chords", [&] (arranger::Song& s) { arranger::chord_input::writeChords (s, 0, typed.end, typed.marks, &session.deviceLibrary()); });
        }
        if (o.aiDock)
            content.showAiTool (o.aiTool.isNotEmpty() ? o.aiTool : juce::String ("ai_humanize"));
        if (o.aiDock && o.reharmBar > 0)
            content.reharmonizeAt (static_cast<arranger::Tick> (o.reharmBar - 1) * content.getSession().song().timeSignature.ticksPerBar());
        if (o.aiDock && o.arrangeGoal.isNotEmpty())
            content.chooseArrangeGoal (o.arrangeGoal, o.arrangeFrom, o.arrangeTo);
        if (o.automation.isNotEmpty())
        {
            auto& session = content.getSession();
            const auto id = session.selectedTrack();
            int pan = o.automation == "pan" ? 1 : (o.automation == "reverb" ? 2 : (o.automation == "chorus" ? 3 : 0));
            if (o.automation == "plugin") // the first control of the first effect of the track
                session.edit ("Automation", [&] (arranger::Song& s) {
                    std::string name = "Control 1";
                    if (const auto live = session.pluginAt (id, 0); live != nullptr && ! live->plugin().getParameters().isEmpty())
                        name = live->plugin().getParameters()[0]->getName (48).toStdString();
                    pan = std::max (0, arranger::edit::addControlLine (s, id, 0, 0, name));
                });
            const arranger::Tick bar = session.song().timeSignature.ticksPerBar();
            session.edit ("Automation", [&] (arranger::Song& s) {
                const float values[] = { 40.0f, 100.0f, 100.0f, 70.0f, 110.0f, 0.0f };
                const arranger::Tick bars[] = { 0, 2, 4, 5, 6, 8 };
                for (int i = 0; i < 6; ++i)
                    arranger::edit::setAutomationPoint (s, id, pan, { bars[i] * bar, values[i] });
            });
            content.timelineGeometry().automationShown[id] = pan + 1;
        }
        if (o.buses)
        {
            content.getSession().edit ("Buses", [] (arranger::Song& s) {
                const auto group = arranger::edit::addBus (s, "Rhythm").id;
                const auto verb = arranger::edit::addBus (s, "Reverb").id;
                for (auto& t : s.tracks)
                {
                    if (t.name == "Drums" || t.name == "Bass")
                        arranger::edit::setTrackBus (s, t.id, group);
                    else
                        arranger::edit::setSend (s, t.id, verb, 70);
                }
                s.buses[0].volume = 90;
                s.buses[1].volume = 80;
            });
        }
        if (o.track.isNotEmpty())
            content.scoreView().setShownTracks (arranger::ui::ScoreView::Tracks::selected);
        content.setSize (o.width, o.height);
        if (o.trackHeight > 0)
            content.timelineGeometry().trackHeight = juce::jlimit (arranger::ui::TimelineGeometry::minTrackHeight,
                                                                  arranger::ui::TimelineGeometry::maxTrackHeight, o.trackHeight);
        content.setPreviewPosition (o.positionTicks, o.positionTicks > 0.0);
        content.setMixerShown (o.mixer > 0);
        if (o.mixer > 0)
            content.setMixerHeight (o.mixer);
        content.showView (o.view);
        if (o.openEditor)
            content.openFirstClipInEditor();
        if (o.speech)
            juce::Timer::callAfterDelay (700, [safe = juce::Component::SafePointer<arranger::ui::MainComponent> (&content)] {
                if (safe != nullptr)
                    safe->openSpeechSettings();
            });
        if (o.scoreMarks)
        {
            using Kind = arranger::ScoreMark::Kind;
            auto& score = content.scoreView();
            score.selectAll();
            score.addMarkAtSelection (Kind::lyric, "Late at night the ci-ty sleeps and I am wide a-wake");
            score.addMarkAtSelection (Kind::chord, "Cm7");
            score.addMarkAtSelection (Kind::dynamic, "mp");
            score.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
        }
        if (o.findChords.isNotEmpty())
        {
            const auto& time = content.getSession().song().timeSignature;
            content.scoreView().findChords (o.findChords == "beat" ? time.ticksPerBeat() : (o.findChords == "half" ? time.ticksPerBar() / 2 : time.ticksPerBar()));
        }
        if (o.guitarChords)
            content.scoreView().setGuitarChords (true);
        for (const auto c : o.scoreKeys)
            content.scoreView().keyPressed (juce::KeyPress (static_cast<int> (c), juce::ModifierKeys(), c));

        juce::Image image;
        if (o.analysisView)
        {
            arranger::ClipId clip = 0;
            for (const auto& t : content.getSession().song().tracks)
                if (! t.audio.empty() && clip == 0)
                    clip = t.audio.front()->id;
            arranger::ui::AnalysisView view (content.getSession(), clip);
            view.onAskAi = [] (const juce::String&) {};
            view.setLookAndFeel (&content.getLookAndFeel());
            view.setSize (560, 640);
            image = view.createComponentSnapshot (view.getLocalBounds(), true, 2.0f);
            view.setLookAndFeel (nullptr);
        }
        else if (o.pluginPanel)
        {
            arranger::ui::PluginRackPanel panel (content.getSession(), content.pluginWindowList(), content.getSession().selectedTrack());
            panel.setLookAndFeel (&content.getLookAndFeel());
            panel.setOpaque (false);
            juce::Image whole (juce::Image::ARGB, panel.getWidth() * 2, panel.getHeight() * 2, true);
            {
                juce::Graphics g (whole);
                g.fillAll (arranger::ui::currentTheme().colour ("panel"));
                g.drawImageAt (panel.createComponentSnapshot (panel.getLocalBounds(), true, 2.0f), 0, 0);
            }
            image = whole;
            panel.setLookAndFeel (nullptr);
        }
        else if (o.slice != juce::File())
        {
            arranger::ui::SlicerView slicer (content.getSession(), content.getSession().selectedTrack(), o.slice);
            slicer.setLookAndFeel (&content.getLookAndFeel());
            slicer.setSize (1100, 440);
            image = slicer.createComponentSnapshot (slicer.getLocalBounds(), true, 2.0f);
            slicer.setLookAndFeel (nullptr);
        }
        else if (o.aiTool.isNotEmpty() && ! o.aiDock)
        {
            arranger::MidiAiService service;
            arranger::ui::AiToolsView view (content.getSession(), service, o.aiTool);
            view.setLookAndFeel (&content.getLookAndFeel());
            image = view.createComponentSnapshot (view.getLocalBounds(), true, 2.0f);
            view.setLookAndFeel (nullptr);
        }
        else if (o.rules)
        {
            arranger::TrackId part = 0;
            for (const auto& t : content.getSession().song().tracks)
                if (t.styleChannel > 10 && part == 0)
                    part = t.id;
            arranger::ui::StyleRuleEditor editor (content.getSession(), part, 0);
            editor.setLookAndFeel (&content.getLookAndFeel());
            image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
            editor.setLookAndFeel (nullptr);
        }
        else
            image = content.createComponentSnapshot (content.getLocalBounds(), true, 2.0f);
        o.screenshot.getParentDirectory().createDirectory();
        o.screenshot.deleteFile();
        juce::FileOutputStream out (o.screenshot);
        juce::PNGImageFormat png;
        const bool ok = out.openedOk() && png.writeImageToStream (image, out);
        std::cout << (ok ? "Wrote " : "Failed to write ") << o.screenshot.getFullPathName() << std::endl;
        return ok;
    }

    /** Plays the project from the start with no audio device and writes the sound to a WAV file. */
    bool renderAudio (const Options& o)
    {
        devices = std::make_unique<arranger::platform::JuceDeviceHost> (engine);
        arranger::ui::MainComponent content (engine, *devices, nullptr, &plugins);
        if (! content.openFile (o.project, false))
        {
            std::cerr << "Can't open " << o.project.getFullPathName() << std::endl;
            return false;
        }
        constexpr int kBlock = 512;
        constexpr double kRate = 48000.0;
        engine.prepare (kRate, kBlock);
        auto& session = content.getSession();
        session.edit ("", [] (arranger::Song& s) { s.metronome.enabled = false; s.loop.enabled = false; });
        if (o.setParam.isNotEmpty())
        {
            const auto name = o.setParam.upToFirstOccurrenceOf ("=", false, false);
            const auto value = o.setParam.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
            for (const auto& t : session.song().tracks)
                for (int i = 0; i < static_cast<int> (t.effects.size()); ++i)
                    if (const auto p = session.pluginAt (t.id, i))
                        for (auto* param : p->plugin().getParameters())
                            if (param->getName (64) == name)
                                param->setValueNotifyingHost (value);
        }
        if (o.saveProject != juce::File())
        {
            session.capturePluginStates();
            arranger::project_file::save (session.song(), o.saveProject.getFullPathName().toStdString());
        }
        const auto& song = session.song();
        const double seconds = o.renderSeconds > 0.0 ? o.renderSeconds : song.tempo.secondsAt (static_cast<double> (song.contentEnd())) + 2.0;
        arranger::Sample out;
        out.sampleRate = kRate;
        out.channels.assign (2, {});
        std::vector<float> left (kBlock), right (kBlock);
        float* channels[] = { left.data(), right.data() };
        arranger::MidiEventBuffer midiIn (16), midiOut (4096);
        engine.locate (0);
        engine.play();
        for (double t = 0.0; t < seconds; t += kBlock / kRate)
        {
            arranger::AudioBlock block { channels, 2, kBlock };
            midiOut.clear();
            engine.process (block, midiIn, midiOut);
            out.channels[0].insert (out.channels[0].end(), left.begin(), left.end());
            out.channels[1].insert (out.channels[1].end(), right.begin(), right.end());
        }
        engine.stop();
        try
        {
            arranger::wav::write (out, o.render.getFullPathName().toStdString(), 24);
        }
        catch (const std::exception& e)
        {
            std::cerr << e.what() << std::endl;
            return false;
        }
        float peak = 0.0f;
        for (const auto v : out.channels[0])
            peak = std::max (peak, std::abs (v));
        std::cout << "Wrote " << o.render.getFullPathName() << " (" << seconds << " s, peak " << peak << ")" << std::endl;
        return true;
    }

    arranger::PluginHost plugins; ///< before the engine: the engine's plugins go first
    arranger::Engine engine;
    std::unique_ptr<arranger::platform::JuceDeviceHost> devices;
    juce::ApplicationProperties properties;
    std::unique_ptr<MainWindow> window;
    arranger::ui::MainComponent* mainComponent = nullptr;
};

START_JUCE_APPLICATION (ArrangerApplication)

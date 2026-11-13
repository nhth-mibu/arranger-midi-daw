#include "ui/MainComponent.h"
#include "ui/SlicerView.h"
#include "ui/StyleRuleEditor.h"

#include "arranger/core/MusicXml.h"
#include "arranger/core/ProjectFile.h"
#include "arranger/core/Style.h"

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
    bool openEditor = false;
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
    juce::String automation; ///< with --track: show this line ("volume" or "pan") with sample points on the track
    bool pluginPanel = false; ///< screenshot the plugin list of the selected track instead of the main window
    juce::File slice; ///< screenshot the slicer with this loop instead of the main window

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
            else if (a == "--view") { const auto v = next(); o.view = v == "mixer" ? 1 : (v == "score" ? 2 : (v == "beat" ? 3 : (v == "clips" ? 4 : 0))); }
            else if (a == "--editor") o.openEditor = true;
            else if (a == "--rules") o.rules = true;
            else if (a == "--track") o.track = next();
            else if (a == "--score-keys") o.scoreKeys = next();
            else if (a == "--score-marks") o.scoreMarks = true;
            else if (a == "--export-musicxml") o.exportMusicXml = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            else if (a == "--export-pdf") o.exportPdf = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            else if (a == "--export-style") o.exportStyle = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            else if (a == "--audio-to-midi") o.audioToMidi = next();
            else if (a == "--automation") o.automation = next();
            else if (a == "--plugin-panel") o.pluginPanel = true;
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
        : DocumentWindow ("Arranger MIDI DAW", arranger::ui::currentTheme().colour ("background"), allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (true, true);
        setResizeLimits (1100, 680, 10000, 10000);

        content->onTitleChanged = [this] (const juce::String& t) { setName (t + "  \xe2\x80\x94  Arranger MIDI DAW"); };

#if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (content);
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
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    /** A start with an option (--screenshot, --scan-plugin...) is a job of its own, next to an open app. */
    bool moreThanOneInstanceAllowed() override { return getCommandLineParameters().contains ("--"); }

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
        window = std::make_unique<MainWindow> (content);
        mainComponent = content;

        if (options.project != juce::File())
            content->openFile (options.project, true);
        else if (const juce::File last (settings->getValue ("lastProject")); last.existsAsFile())
            content->openFile (last, false);
    }

    void shutdown() override
    {
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
        if (! score.exportPdf (o.exportPdf))
            return false;
        std::cout << "Wrote " << o.exportPdf.getFullPathName() << std::endl;
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
        selectTrack (content.getSession(), o.track);
        if (o.automation.isNotEmpty())
        {
            auto& session = content.getSession();
            const auto id = session.selectedTrack();
            const bool pan = o.automation == "pan";
            const arranger::Tick bar = session.song().timeSignature.ticksPerBar();
            session.edit ("Automation", [&] (arranger::Song& s) {
                const float values[] = { 40.0f, 100.0f, 100.0f, 70.0f, 110.0f, 0.0f };
                const arranger::Tick bars[] = { 0, 2, 4, 5, 6, 8 };
                for (int i = 0; i < 6; ++i)
                    arranger::edit::setAutomationPoint (s, id, pan, { bars[i] * bar, values[i] });
            });
            content.timelineGeometry().automationShown[id] = pan ? 2 : 1;
        }
        content.setSize (o.width, o.height);
        content.setPreviewPosition (o.positionTicks, o.positionTicks > 0.0);
        content.showView (o.view);
        if (o.openEditor)
            content.openFirstClipInEditor();
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
        for (const auto c : o.scoreKeys)
            content.scoreView().keyPressed (juce::KeyPress (static_cast<int> (c), juce::ModifierKeys(), c));

        juce::Image image;
        if (o.pluginPanel)
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

#include "ui/MainComponent.h"
#include "ui/SlicerView.h"

#include "arranger/core/ProjectFile.h"

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
            else if (a == "--view") { const auto v = next(); o.view = v == "mixer" ? 1 : (v == "beat" ? 3 : 0); }
            else if (a == "--editor") o.openEditor = true;
            else if (a == "--slice") o.slice = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            else if (a.endsWith (arranger::project_file::kExtension) || a.endsWithIgnoreCase (".mid") || a.endsWithIgnoreCase (".midi")) o.project = juce::File::getCurrentWorkingDirectory().getChildFile (a);
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
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        const auto options = Options::parse (getCommandLineParameterArray());

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

        auto* content = new arranger::ui::MainComponent (engine, *devices, settings);
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
    /** Renders the main window off-screen to a PNG at 2x, with no audio device. */
    bool takeScreenshot (const Options& o)
    {
        devices = std::make_unique<arranger::platform::JuceDeviceHost> (engine);
        arranger::ui::MainComponent content (engine, *devices, nullptr);
        content.setThemeDark (! o.lightTheme);
        if (o.project != juce::File() && ! content.openFile (o.project, false))
        {
            std::cerr << "Can't open " << o.project.getFullPathName() << std::endl;
            return false;
        }
        content.setSize (o.width, o.height);
        content.setPreviewPosition (o.positionTicks, o.positionTicks > 0.0);
        content.showView (o.view);
        if (o.openEditor)
            content.openFirstClipInEditor();

        juce::Image image;
        if (o.slice != juce::File())
        {
            arranger::ui::SlicerView slicer (content.getSession(), content.getSession().selectedTrack(), o.slice);
            slicer.setLookAndFeel (&content.getLookAndFeel());
            slicer.setSize (1100, 440);
            image = slicer.createComponentSnapshot (slicer.getLocalBounds(), true, 2.0f);
            slicer.setLookAndFeel (nullptr);
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

    arranger::Engine engine;
    std::unique_ptr<arranger::platform::JuceDeviceHost> devices;
    juce::ApplicationProperties properties;
    std::unique_ptr<MainWindow> window;
    arranger::ui::MainComponent* mainComponent = nullptr;
};

START_JUCE_APPLICATION (ArrangerApplication)

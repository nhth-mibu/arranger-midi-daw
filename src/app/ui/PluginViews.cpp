#include "PluginViews.h"

#include "arranger/core/SongEdit.h"

namespace arranger::ui
{

namespace
{
    juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
    constexpr int kRow = 34, kHeader = 40, kWidth = 340;
}

// ---- windows -------------------------------------------------------------------------
class PluginWindows::Window final : public juce::DocumentWindow
{
public:
    Window (PluginWindows& o, std::string k, std::shared_ptr<HostedProcessor> p, const juce::String& title)
        : juce::DocumentWindow (title, currentTheme().colour ("panel"), juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          owner (o), key (std::move (k)), processor (std::move (p))
    {
        setUsingNativeTitleBar (true);
        auto& plugin = processor->plugin();
        // The plugin's own window, or a list of its controls when it has none.
        juce::AudioProcessorEditor* editor = plugin.hasEditor() ? plugin.createEditorIfNeeded() : nullptr;
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (plugin);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    ~Window() override { clearContentComponent(); }

    void closeButtonPressed() override
    {
        // Deletes this window; the settings changed in it go into the song.
        auto& o = owner;
        const auto k = key;
        juce::MessageManager::callAsync ([&o, k] {
            o.windows.erase (k);
            o.session.capturePluginStates();
        });
    }

    PluginWindows& owner;
    const std::string key;
    const std::shared_ptr<HostedProcessor> processor;
};

PluginWindows::PluginWindows (Session& s) : session (s) { session.addChangeListener (this); }

PluginWindows::~PluginWindows()
{
    session.removeChangeListener (this);
    windows.clear();
}

void PluginWindows::show (TrackId track, int index)
{
    const auto processor = session.pluginAt (track, index);
    if (processor == nullptr)
        return;
    const auto key = std::to_string (track) + "/" + std::to_string (index);
    if (auto it = windows.find (key); it != windows.end() && it->second->processor == processor)
    {
        it->second->toFront (true);
        return;
    }
    const auto* t = edit::findTrack (session.song(), track);
    const auto* b = edit::findBus (session.song(), track);
    const auto title = processor->plugin().getName() + " - " + (t != nullptr ? utf8 (t->name) : (b != nullptr ? utf8 (b->name) : juce::String ("Master")));
    windows[key] = std::make_unique<Window> (*this, key, processor, title);
}

void PluginWindows::closeAll() { windows.clear(); }

void PluginWindows::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // A window goes when its plugin is no longer in its slot.
    for (auto it = windows.begin(); it != windows.end();)
    {
        const auto slash = it->first.find ('/');
        const auto track = static_cast<TrackId> (std::stoul (it->first.substr (0, slash)));
        const int index = std::stoi (it->first.substr (slash + 1));
        if (session.pluginAt (track, index) != it->second->processor)
            it = windows.erase (it);
        else
            ++it;
    }
}

// ---- rack panel ----------------------------------------------------------------------
PluginRackPanel::PluginRackPanel (Session& s, PluginWindows& w, TrackId t) : session (s), windows (w), track (t)
{
    addAndMakeVisible (addButton);
    addButton.onClick = [this] {
        showPluginMenu (session, false, &addButton, [this] (const PluginSlot& slot) {
            session.capturePluginStates();
            editEffects ("Add Effect", [&] (std::vector<PluginSlot>& list) { list.push_back (slot); });
            if (const auto* list = effects())
                windows.show (track, static_cast<int> (list->size()) - 1);
        }, onScan);
    };
    session.addChangeListener (this);
    rebuild();
}

PluginRackPanel::~PluginRackPanel() { session.removeChangeListener (this); }

const std::vector<PluginSlot>* PluginRackPanel::effects() const
{
    if (track == 0)
        return &session.song().master.effects;
    if (const auto* b = edit::findBus (session.song(), track))
        return &b->effects;
    const auto* t = edit::findTrack (session.song(), track);
    return t != nullptr ? &t->effects : nullptr;
}

void PluginRackPanel::editEffects (const char* name, const std::function<void (std::vector<PluginSlot>&)>& fn)
{
    const auto id = track;
    session.edit (name, [&] (Song& s) {
        if (id == 0)
            fn (s.master.effects);
        else if (auto* b = edit::findBus (s, id))
            fn (b->effects);
        else if (auto* t = edit::findTrack (s, id))
            fn (t->effects);
    });
}

int PluginRackPanel::rowCount() const
{
    const auto* list = effects();
    return (hasInstrument ? 1 : 0) + (list != nullptr ? static_cast<int> (list->size()) : 0);
}

juce::Rectangle<int> PluginRackPanel::rowBounds (int row) const
{
    return { 12, kHeader + row * kRow, getWidth() - 24, kRow - 4 };
}

void PluginRackPanel::rebuild()
{
    const auto* t = edit::findTrack (session.song(), track);
    hasInstrument = t != nullptr && t->output.kind == TrackOutput::Kind::plugin;
    const bool allowed = track == 0 || edit::findBus (session.song(), track) != nullptr || (t != nullptr && Session::canHaveEffects (*t));
    addButton.setEnabled (allowed);
    setSize (kWidth, kHeader + rowCount() * kRow + (allowed ? 48 : 96));
    resized();
    repaint();
}

void PluginRackPanel::changeListenerCallback (juce::ChangeBroadcaster*) { rebuild(); }

void PluginRackPanel::resized()
{
    addButton.setBounds (12, kHeader + rowCount() * kRow + 4, getWidth() - 24, 30);
}

void PluginRackPanel::paint (juce::Graphics& g)
{
    const auto& th = currentTheme();
    const auto* t = edit::findTrack (session.song(), track);
    g.setColour (th.colour ("textMuted"));
    g.setFont (th.font (11.0f, true));
    const auto* bus = edit::findBus (session.song(), track);
    g.drawText (track == 0 ? juce::String ("MASTER EFFECTS") : (bus != nullptr ? "BUS EFFECTS  " + utf8 (bus->name).toUpperCase() : "PLUGINS  " + (t != nullptr ? utf8 (t->name).toUpperCase() : juce::String())),
                12, 8, getWidth() - 24, 24, juce::Justification::centredLeft, true);

    const auto* list = effects();
    for (int row = 0; row < rowCount(); ++row)
    {
        const bool instrument = hasInstrument && row == 0;
        const int index = instrument ? -1 : row - (hasInstrument ? 1 : 0);
        const PluginSlot* slot = instrument ? &t->instrument : &(*list)[static_cast<std::size_t> (index)];
        const auto r = rowBounds (row).toFloat();
        const auto problem = slot->empty() ? juce::String() : session.pluginProblem (track, index);
        g.setColour (th.colour ("line"));
        g.fillRoundedRectangle (r, 6.0f);

        // Power dot: on, or off when bypassed.
        const auto dot = juce::Rectangle<float> (r.getX() + 10.0f, r.getCentreY() - 5.0f, 10.0f, 10.0f);
        if (! instrument)
        {
            g.setColour (slot->bypass ? th.colour ("textDim") : th.colour ("ok"));
            if (slot->bypass)
                g.drawEllipse (dot, 1.5f);
            else
                g.fillEllipse (dot);
        }
        g.setFont (th.font (13.0f, instrument));
        g.setColour (problem.isNotEmpty() ? th.colour ("warning") : (slot->bypass ? th.colour ("textDim") : th.colour ("text")));
        auto name = slot->empty() ? juce::String ("Choose an instrument...") : utf8 (slot->name);
        if (problem.isNotEmpty())
            name += "  (not loaded)";
        g.drawText (name, r.withTrimmedLeft (30.0f).withTrimmedRight (instrument ? 8.0f : 78.0f), juce::Justification::centredLeft, true);

        if (! instrument)
        {
            g.setColour (th.colour ("textMuted"));
            g.setFont (th.font (13.0f));
            const char* const marks[] = { "\xe2\x86\x91", "\xe2\x86\x93", "\xc3\x97" }; // up, down, remove
            for (int b = 0; b < 3; ++b)
                g.drawText (juce::String::fromUTF8 (marks[b]), juce::Rectangle<float> (r.getRight() - 76.0f + static_cast<float> (b) * 25.0f, r.getY(), 24.0f, r.getHeight()), juce::Justification::centred, false);
        }
    }

    if (! addButton.isEnabled())
    {
        g.setColour (th.colour ("textDim"));
        g.setFont (th.font (12.0f));
        g.drawFittedText ("This track plays through a MIDI port. Its sound is made in your synth, so effects in the app do not change it.",
                          12, kHeader + rowCount() * kRow + 40, getWidth() - 24, 52, juce::Justification::topLeft, 3);
    }
}

void PluginRackPanel::mouseUp (const juce::MouseEvent& e)
{
    for (int row = 0; row < rowCount(); ++row)
    {
        const auto r = rowBounds (row);
        if (! r.contains (e.getPosition()))
            continue;
        if (hasInstrument && row == 0)
        {
            const auto* t = edit::findTrack (session.song(), track);
            if (t != nullptr && ! t->instrument.empty() && ! e.mods.isPopupMenu())
            {
                windows.show (track, -1);
                return;
            }
            showPluginMenu (session, true, this, [this] (const PluginSlot& slot) {
                const auto id = track;
                session.edit ("Choose Instrument", [&] (Song& s) {
                    if (auto* tr = edit::findTrack (s, id))
                        tr->instrument = slot;
                });
            }, onScan);
            return;
        }
        const auto index = static_cast<std::size_t> (row - (hasInstrument ? 1 : 0));
        const int button = (e.x - (r.getRight() - 76)) / 25;
        if (e.x < r.getX() + 28)
            editEffects ("Bypass Effect", [&] (std::vector<PluginSlot>& list) { if (index < list.size()) list[index].bypass = ! list[index].bypass; });
        else if (e.x >= r.getRight() - 76)
        {
            // The order changes: keep the settings of each plugin with its slot.
            session.capturePluginStates();
            if (button == 2)
                editEffects ("Remove Effect", [&] (std::vector<PluginSlot>& list) { if (index < list.size()) list.erase (list.begin() + static_cast<std::ptrdiff_t> (index)); });
            else if (button == 0)
                editEffects ("Move Effect", [&] (std::vector<PluginSlot>& list) { if (index > 0 && index < list.size()) std::swap (list[index], list[index - 1]); });
            else
                editEffects ("Move Effect", [&] (std::vector<PluginSlot>& list) { if (index + 1 < list.size()) std::swap (list[index], list[index + 1]); });
        }
        else
            windows.show (track, static_cast<int> (index));
        return;
    }
}

void PluginRackPanel::showPluginMenu (Session& session, bool instruments, juce::Component* target, std::function<void (const PluginSlot&)> chosen, std::function<void()> scan)
{
    auto* host = session.pluginHost();
    const auto list = host == nullptr ? std::vector<juce::PluginDescription>() : (instruments ? host->instruments() : host->effects());
    juce::PopupMenu m;
    // One submenu per maker, in alphabetical order.
    std::map<juce::String, juce::PopupMenu> makers;
    for (std::size_t i = 0; i < list.size(); ++i)
        makers[list[i].manufacturerName.isEmpty() ? juce::String ("Other") : list[i].manufacturerName]
            .addItem (static_cast<int> (i) + 1, list[i].name + "  (" + list[i].pluginFormatName + ")");
    for (auto& [maker, sub] : makers)
        m.addSubMenu (maker, sub);
    if (list.empty())
        m.addItem (-1, instruments ? "No instrument plugins found" : "No effect plugins found", false);
    m.addSeparator();
    m.addItem (100000, "Scan for Plugins...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [list, chosen, scan] (int r) {
        if (r == 100000)
        {
            if (scan)
                scan();
        }
        else if (r > 0 && static_cast<std::size_t> (r) <= list.size() && chosen)
            chosen (PluginHost::slotFor (list[static_cast<std::size_t> (r) - 1]));
    });
}

// ---- scan ----------------------------------------------------------------------------
namespace
{
    class ScanJob final : public juce::ThreadWithProgressWindow
    {
    public:
        ScanJob (PluginHost& h, std::function<void (juce::String)> d)
            : juce::ThreadWithProgressWindow ("Scan for plugins", true, true), host (h), done (std::move (d)) {}

        void run() override
        {
            const auto files = host.filesToScan();
            for (int i = 0; i < files.size() && ! threadShouldExit(); ++i)
            {
                setProgress (static_cast<double> (i) / static_cast<double> (files.size()));
                setStatusMessage (juce::File (files[i].fromFirstOccurrenceOf ("\n", false, false)).getFileName());
                if (! host.scanFile (files[i]))
                    ++failed;
                ++scanned;
            }
        }

        void threadComplete (bool) override
        {
            host.save();
            juce::String summary = juce::String (host.pluginCount()) + " plugins are ready to use.";
            if (failed > 0)
                summary += "\n" + juce::String (failed) + " files did not load. The app skips them from now on.";
            if (scanned == 0)
                summary = "No new plugin files found. " + summary;
            const auto callback = done;
            delete this;
            if (callback)
                callback (summary);
        }

        PluginHost& host;
        std::function<void (juce::String)> done;
        int scanned = 0, failed = 0;
    };
}

void scanPlugins (PluginHost& host, std::function<void (juce::String)> done)
{
    host.removeMissing();
    (new ScanJob (host, std::move (done)))->launchThread();
}

} // namespace arranger::ui

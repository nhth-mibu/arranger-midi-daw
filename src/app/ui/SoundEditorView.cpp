#include "SoundEditorView.h"

namespace arranger::ui
{

namespace
{
    constexpr int kCellW = 104, kCellH = 112, kHeader = 88, kPagesW = 168;

    std::string keyOf (const SynthPage& page, const SynthParameter& p) { return page.name + "/" + p.name; }
}

// ---- Knob: a ring that is dragged up and down --------------------------------------------------

class SoundEditorView::Knob final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (int)> onChange;
    int value = 0, minimum = 0, maximum = 127, fallback = 0;

    void setValue (int v)
    {
        v = std::clamp (v, minimum, maximum);
        if (v != value)
        {
            value = v;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& theme = currentTheme();
        const auto area = getLocalBounds().toFloat().reduced (4.0f);
        const float size = std::min (area.getWidth(), area.getHeight());
        const auto box = juce::Rectangle<float> (size, size).withCentre (area.getCentre());
        const float start = juce::MathConstants<float>::pi * 1.25f, end = juce::MathConstants<float>::pi * 2.75f;
        const float span = static_cast<float> (std::max (1, maximum - minimum));
        const float at = start + (end - start) * static_cast<float> (value - minimum) / span;
        const float r = size * 0.5f - 3.0f;

        g.setColour (theme.colour ("panelRaised"));
        g.fillEllipse (box.reduced (7.0f));
        juce::Path track, fill;
        track.addCentredArc (box.getCentreX(), box.getCentreY(), r, r, 0.0f, start, end, true);
        fill.addCentredArc (box.getCentreX(), box.getCentreY(), r, r, 0.0f, start, at, true);
        const juce::PathStrokeType stroke (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (theme.colour ("lineStrong"));
        g.strokePath (track, stroke);
        g.setColour (theme.colour ("accent"));
        g.strokePath (fill, stroke);
        const auto tip = box.getCentre().getPointOnCircumference (r - 9.0f, at);
        g.setColour (theme.colour ("text"));
        g.drawLine ({ box.getCentre().getPointOnCircumference (r * 0.25f, at), tip }, 2.0f);
    }

    void mouseDown (const juce::MouseEvent&) override { dragFrom = value; }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float range = static_cast<float> (maximum - minimum);
        const float pixels = e.mods.isShiftDown() ? 600.0f : 160.0f; // Shift for fine steps
        change (dragFrom + juce::roundToInt (-static_cast<float> (e.getDistanceFromDragStartY()) * range / pixels));
    }

    void mouseDoubleClick (const juce::MouseEvent&) override { change (fallback); }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const float dy = std::abs (w.deltaY) > 0.0f ? w.deltaY : w.deltaX;
        if (dy != 0.0f)
            change (value + (dy > 0.0f ? 1 : -1) * std::max (1, (maximum - minimum) / 64));
    }

private:
    void change (int v)
    {
        v = std::clamp (v, minimum, maximum);
        if (v == value)
            return;
        value = v;
        repaint();
        if (onChange)
            onChange (v);
    }

    int dragFrom = 0;
};

// ---- Cell: one setting: name, knob or list, value -----------------------------------------------

class SoundEditorView::Cell final : public juce::Component
{
public:
    Cell (const SynthParameter& p, std::function<void (int)> changed) : param (p), onChange (std::move (changed))
    {
        if (param.isChoice())
        {
            for (std::size_t i = 0; i < param.choices.size(); ++i)
                list.addItem (param.choices[i], static_cast<int> (i) + 1);
            list.onChange = [this] {
                const int i = list.getSelectedItemIndex();
                if (i >= 0 && ! updating)
                    onChange (param.choiceValue (static_cast<std::size_t> (i)));
            };
            addAndMakeVisible (list);
        }
        else
        {
            knob.minimum = param.min;
            knob.maximum = param.max;
            knob.fallback = param.defaultValue;
            knob.setTooltip ("Drag up or down. Shift: fine. Double-click: default.");
            knob.onChange = [this] (int v) {
                value = v;
                repaint();
                onChange (v);
            };
            addAndMakeVisible (knob);
        }
    }

    void setValue (int v)
    {
        value = v;
        updating = true;
        if (param.isChoice())
            list.setSelectedItemIndex (std::max (0, param.choiceIndex (v)), juce::dontSendNotification);
        else
            knob.setValue (v);
        updating = false;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& theme = currentTheme();
        auto r = getLocalBounds().reduced (4);
        g.setColour (theme.colour ("panelRaised").withAlpha (0.5f));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (theme.colour ("textMuted"));
        g.setFont (theme.font (11.5f));
        g.drawFittedText (param.name, r.removeFromTop (30).reduced (6, 2), juce::Justification::centred, 2, 0.9f);
        if (! param.isChoice())
        {
            g.setColour (theme.colour ("text"));
            g.setFont (theme.font (12.5f, true));
            g.drawText (param.display (value), r.removeFromBottom (22), juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        r.removeFromTop (26);
        if (param.isChoice())
            list.setBounds (r.withSizeKeepingCentre (r.getWidth(), 26));
        else
            knob.setBounds (r.withTrimmedBottom (18));
    }

    const SynthParameter& param;

private:
    std::function<void (int)> onChange;
    Knob knob;
    juce::ComboBox list;
    int value = 0;
    bool updating = false;
};

// ---- the view ------------------------------------------------------------------------------------

std::vector<const DeviceProfile*> SoundEditorView::editableDevices (const Session& session)
{
    std::vector<const DeviceProfile*> on, off;
    for (const auto& p : session.deviceLibrary().all())
        if (! p.editor.empty())
            (session.outputFor (p).empty() ? off : on).push_back (&p);
    on.insert (on.end(), off.begin(), off.end());
    return on;
}

SoundEditorView::SoundEditorView (Session& s, const std::string& id) : session (s)
{
    for (int ch = 1; ch <= 16; ++ch)
        channel.addItem ("Channel " + juce::String (ch), ch);
    channel.onChange = [this] {
        session.setSynthChannel (profileId, channel.getSelectedId());
        listener.reset();
        updateStatus();
    };
    device.onChange = [this] {
        const auto list = editableDevices (session);
        const int i = device.getSelectedItemIndex();
        if (i >= 0 && i < static_cast<int> (list.size()) && list[static_cast<std::size_t> (i)]->id != profileId)
            showDevice (list[static_cast<std::size_t> (i)]->id);
    };
    readButton.setTooltip ("Ask the synth for the sound that plays now and show its settings");
    readButton.onClick = [this] {
        const auto* p = profile();
        if (p == nullptr)
            return;
        const auto port = session.outputFor (*p);
        if (port.empty())
            return updateStatus();
        readDump.clear();
        collectingDump = true;
        dumpUntil = juce::Time::getMillisecondCounterHiRes() + 3000.0;
        for (const auto& m : synth_edit::readRequests (editor, session.synthChannel (profileId)))
            session.deviceHost().sendNow (port, m);
        status.setText ("Asked the " + juce::String (p->name) + " for its settings...", juce::dontSendNotification);
    };
    namesButton.setTooltip ("Read the names of the sounds stored in the synth and add them to its sound list");
    sendNamesButton.setTooltip ("Write the patch and tone names to the temporary patch. Save the patch on the synth to keep them.");
    sendNamesButton.onClick = [this] {
        const auto* p = profile();
        if (p == nullptr)
            return;
        const auto port = session.outputFor (*p);
        if (port.empty())
            return updateStatus();
        int sent = 0;
        for (std::size_t i = 0; i < p->editor.nameFields.size() && i < static_cast<std::size_t> (nameEditors.size()); ++i)
        {
            const auto text = nameEditors[static_cast<int> (i)]->getText().toStdString();
            if (text.empty())
                continue; // an empty field means "leave this name alone"
            ++sent;
            for (const auto& m : synth_edit::nameMessages (p->editor, p->editor.nameFields[i], text, session.synthChannel (profileId)))
            {
                session.deviceHost().sendNow (port, m);
                juce::Thread::sleep (20); // the D-05 needs a gap between SysEx messages
            }
        }
        status.setText (sent == 0 ? "Type a name first. Empty fields are not changed."
                                  : "Names sent to the temporary patch. Save the patch on the synth to keep them.",
                        juce::dontSendNotification);
    };
    addChildComponent (sendNamesButton);
    saveDumpButton.setTooltip ("Save the SysEx dump from Read from Synth as a .syx file");
    saveDumpButton.onClick = [this] { saveDump(); };
    loadDumpButton.setTooltip ("Send every message in a .syx file to the synth's temporary patch");
    loadDumpButton.onClick = [this] { loadDump(); };
    addChildComponent (saveDumpButton);
    addChildComponent (loadDumpButton);
    namesButton.onClick = [this] {
        const auto problem = session.readSoundNames (profileId, [safe = juce::Component::SafePointer<SoundEditorView> (this)] (const juce::String& report) {
            if (safe != nullptr)
                safe->status.setText (report, juce::dontSendNotification);
        });
        status.setText (problem.isNotEmpty() ? problem : "Reading sound names. The synth changes banks while it reads.", juce::dontSendNotification);
    };
    sendAllButton.setTooltip ("Send every setting of this page to the synth");
    sendAllButton.onClick = [this] {
        const auto* p = profile();
        if (p == nullptr || page >= static_cast<int> (p->editor.pages.size()))
            return;
        const auto& pg = p->editor.pages[static_cast<std::size_t> (page)];
        for (const auto& param : pg.params)
            session.sendSynthParameter (*p, pg, param, session.synthValue (profileId, keyOf (pg, param)).value_or (param.defaultValue));
    };

    for (auto* c : std::initializer_list<juce::Component*> { &device, &channel, &readButton, &namesButton, &sendAllButton, &sendNamesButton, &status, &viewport })
        addAndMakeVisible (c);
    status.setColour (juce::Label::textColourId, currentTheme().colour ("textMuted"));
    status.setFont (currentTheme().font (12.0f));
    status.setMinimumHorizontalScale (0.8f);
    viewport.setViewedComponent (&grid, false);
    viewport.setScrollBarsShown (true, false);

    watcher = session.watchMidi ([this] (const std::string& port, const std::vector<std::uint8_t>& bytes) { fromSynth (port, bytes); });
    session.addChangeListener (this);
    showDevice (id);
    setSize (kPagesW + 6 * kCellW + 40, 640);
    startTimerHz (10);
}

SoundEditorView::~SoundEditorView()
{
    session.removeChangeListener (this);
    session.unwatchMidi (watcher);
}

const DeviceProfile* SoundEditorView::profile() const
{
    return session.deviceLibrary().byId (profileId);
}

void SoundEditorView::showDevice (const std::string& id)
{
    profileId = id;
    page = 0;
    const auto* p = profile();
    editor = p != nullptr ? p->editor : SynthEditor();
    listener.reset();

    device.clear (juce::dontSendNotification);
    const auto list = editableDevices (session);
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        device.addItem (juce::String (list[i]->name), static_cast<int> (i) + 1);
        if (list[i]->id == id)
            device.setSelectedItemIndex (static_cast<int> (i), juce::dontSendNotification);
    }
    channel.setSelectedId (session.synthChannel (id), juce::dontSendNotification);
    readButton.setVisible (! editor.sysex.read.empty());
    namesButton.setVisible (editor.names.has_value());
    saveDumpButton.setVisible (! editor.sysex.read.empty());
    loadDumpButton.setVisible (! editor.sysex.write.empty());

    pageButtons.clear();
    const auto pageCount = editor.pages.size() + (editor.nameFields.empty() ? 0 : 1);
    for (std::size_t i = 0; i < pageCount; ++i)
    {
        const auto name = i < editor.pages.size() ? editor.pages[i].name : std::string ("Names");
        auto* b = pageButtons.add (new juce::TextButton (name));
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { showPage (static_cast<int> (i)); };
        addAndMakeVisible (b);
    }
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName ((p != nullptr ? juce::String (p->name) + " " : juce::String()) + "Sound Editor");
    updateStatus();
    showPage (0);
    resized();
}

void SoundEditorView::showPage (int index)
{
    const int pageCount = static_cast<int> (editor.pages.size()) + (editor.nameFields.empty() ? 0 : 1);
    page = std::clamp (index, 0, std::max (0, pageCount - 1));
    for (int i = 0; i < pageButtons.size(); ++i)
        pageButtons[i]->setToggleState (i == page, juce::dontSendNotification);
    rebuildPage();
}

void SoundEditorView::rebuildPage()
{
    cells.clear();
    nameLabels.clear();
    nameEditors.clear();
    if (page == static_cast<int> (editor.pages.size()) && ! editor.nameFields.empty())
    {
        sendNamesButton.setVisible (true);
        for (const auto& field : editor.nameFields)
        {
            auto* label = nameLabels.add (new juce::Label());
            label->setText (field.name, juce::dontSendNotification);
            label->setColour (juce::Label::textColourId, currentTheme().colour ("text"));
            grid.addAndMakeVisible (label);
            auto* edit = nameEditors.add (new juce::TextEditor());
            edit->setText ("", false);
            edit->setInputRestrictions (field.length);
            edit->setTooltip ("Type the name, then click Send Names. " + juce::String (field.length) + " characters.");
            edit->setColour (juce::TextEditor::backgroundColourId, currentTheme().colour ("panelRaised"));
            grid.addAndMakeVisible (edit);
        }
        resized();
        return;
    }
    sendNamesButton.setVisible (false);
    if (page >= static_cast<int> (editor.pages.size()))
        return;
    const auto pageIndex = static_cast<std::size_t> (page);
    for (std::size_t i = 0; i < editor.pages[pageIndex].params.size(); ++i)
    {
        auto* cell = cells.add (new Cell (editor.pages[pageIndex].params[i], [this, pageIndex, i] (int v) {
            if (const auto* p = profile(); p != nullptr && pageIndex < p->editor.pages.size() && i < p->editor.pages[pageIndex].params.size())
            {
                const auto& pg = p->editor.pages[pageIndex];
                if (! session.sendSynthParameter (*p, pg, pg.params[i], v))
                    updateStatus();
                shownVersion = session.synthValuesVersion();
            }
        }));
        grid.addAndMakeVisible (cell);
    }
    refreshValues();
    resized();
}

void SoundEditorView::refreshValues()
{
    shownVersion = session.synthValuesVersion();
    if (page >= static_cast<int> (editor.pages.size()))
        return;
    const auto& pg = editor.pages[static_cast<std::size_t> (page)];
    for (auto* cell : cells)
        cell->setValue (session.synthValue (profileId, keyOf (pg, cell->param)).value_or (cell->param.defaultValue));
}

void SoundEditorView::saveDump()
{
    if (readDump.empty())
    {
        status.setText ("Click Read from Synth first. Save Dump... writes the answers it collected.", juce::dontSendNotification);
        return;
    }
    dumpChooser = std::make_unique<juce::FileChooser> ("Save patch dump", juce::File(), "*.syx");
    dumpChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<SoundEditorView> (this)] (const juce::FileChooser& fc) {
        if (safe == nullptr)
            return;
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        try
        {
            const auto data = sysex_file::encode (safe->readDump);
            if (! file.replaceWithData (data.data(), data.size()))
                throw std::runtime_error ("the file could not be written");
            safe->status.setText ("Saved " + juce::String (static_cast<int> (safe->readDump.size())) + " SysEx messages to "
                                      + file.getFileName() + ".",
                                  juce::dontSendNotification);
        }
        catch (const std::exception& e)
        {
            safe->status.setText (juce::String ("Can't save the dump: ") + e.what(), juce::dontSendNotification);
        }
    });
}

void SoundEditorView::loadDump()
{
    const auto* p = profile();
    if (p == nullptr || editor.sysex.write.empty())
        return;
    dumpChooser = std::make_unique<juce::FileChooser> ("Load patch dump", juce::File(), "*.syx");
    dumpChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<SoundEditorView> (this)] (const juce::FileChooser& fc) {
        if (safe == nullptr)
            return;
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        const auto* profile = safe->profile();
        if (profile == nullptr)
            return;
        const auto port = safe->session.outputFor (*profile);
        if (port.empty())
            return safe->updateStatus();
        try
        {
            juce::MemoryBlock block;
            if (! file.loadFileAsData (block))
                throw std::runtime_error ("the file could not be read");
            const std::string bytes (static_cast<const char*> (block.getData()), block.getSize());
            const auto messages = sysex_file::parse (bytes);
            for (const auto& m : messages)
            {
                safe->session.deviceHost().sendNow (port, m);
                juce::Thread::sleep (20);
            }
            safe->status.setText ("Sent " + juce::String (static_cast<int> (messages.size()))
                                      + " messages to the temporary patch. Save the patch on the synth to keep it.",
                                  juce::dontSendNotification);
        }
        catch (const std::exception& e)
        {
            safe->status.setText (juce::String ("Can't load the dump: ") + e.what(), juce::dontSendNotification);
        }
    });
}

void SoundEditorView::updateStatus()
{
    const auto* p = profile();
    if (p == nullptr)
    {
        status.setText ("No synth with a sound editor.", juce::dontSendNotification);
        return;
    }
    if (session.isReadingSoundNames())
        return;
    const auto port = session.outputFor (*p);
    status.setText (port.empty() ? "Not plugged in. Changes are kept: Send All sends a page later."
                                 : "Plugged in: " + juce::String (port) + ". Changes go to the synth at once.",
                    juce::dontSendNotification);
}

void SoundEditorView::fromSynth (const std::string& port, const std::vector<std::uint8_t>& bytes)
{
    const auto* p = profile();
    if (p == nullptr || ! session.isDevicePort (port, profileId))
        return;
    if (collectingDump && juce::Time::getMillisecondCounterHiRes() <= dumpUntil
        && ! bytes.empty() && bytes.front() == 0xf0 && bytes.back() == 0xf7)
        readDump.push_back (bytes);
    const int ch = session.synthChannel (profileId);
    if (listener == nullptr || listenerChannel != ch)
    {
        listener = std::make_unique<synth_edit::Listener> (editor, ch);
        listenerChannel = ch;
    }
    for (const auto& c : listener->take (bytes))
    {
        const auto& pg = editor.pages[c.page];
        session.rememberSynthValue (profileId, keyOf (pg, pg.params[c.param]), c.value);
    }
}

void SoundEditorView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The profiles were loaded again (a new file, or sound names were added).
    if (const auto* p = profile(); p != nullptr && p->editor.parameterCount() != editor.parameterCount())
        showDevice (profileId);
}

void SoundEditorView::timerCallback()
{
    if (collectingDump && juce::Time::getMillisecondCounterHiRes() > dumpUntil)
    {
        collectingDump = false;
        status.setText (readDump.empty() ? "No SysEx answer came back. Check the synth's MIDI input and Exclusive setting."
                                         : "Read " + juce::String (static_cast<int> (readDump.size())) + " SysEx messages. Save Dump... writes them to a .syx file.",
                        juce::dontSendNotification);
    }
    if (session.synthValuesVersion() != shownVersion)
        refreshValues();
    if (session.isReadingSoundNames())
        status.setText ("Reading sound names... " + juce::String (juce::roundToInt (session.soundNameProgress() * 100.0)) + "%", juce::dontSendNotification);
}

void SoundEditorView::paint (juce::Graphics& g)
{
    const auto& theme = currentTheme();
    g.fillAll (theme.colour ("panel"));
    g.setColour (theme.colour ("background"));
    g.fillRect (getLocalBounds().withTrimmedTop (kHeader).withWidth (kPagesW));
    g.setColour (theme.colour ("line"));
    g.drawHorizontalLine (kHeader, 0.0f, static_cast<float> (getWidth()));
    if (const auto* p = profile())
    {
        g.setColour (theme.colour ("text"));
        g.setFont (theme.font (18.0f, true));
        g.drawText (juce::String (p->name), 16, 10, std::max (100, device.getX() - 24), 30, juce::Justification::centredLeft);
    }
    if (editor.empty())
    {
        g.setColour (theme.colour ("textMuted"));
        g.setFont (theme.font (13.0f));
        g.drawFittedText ("No synth has a sound editor yet. Add an editor: part to its device profile (see docs/device-profiles.md).",
                          getLocalBounds().withTrimmedTop (kHeader).reduced (40), juce::Justification::centred, 3);
    }
}

void SoundEditorView::resized()
{
    // Row 1: name (painted), device, channel. Row 2: status, then the buttons on the right.
    auto top = getLocalBounds().removeFromTop (kHeader).reduced (16, 10);
    auto row1 = top.removeFromTop (30);
    channel.setBounds (row1.removeFromRight (120));
    row1.removeFromRight (8);
    device.setBounds (row1.removeFromRight (200));
    top.removeFromTop (8);
    auto row2 = top.removeFromTop (28);
    for (auto* b : { &loadDumpButton, &saveDumpButton, &sendNamesButton, &sendAllButton, &namesButton, &readButton })
        if (b->isVisible())
        {
            b->setBounds (row2.removeFromRight (b->getBestWidthForHeight (28) + 20));
            row2.removeFromRight (6);
        }
    status.setBounds (row2);

    auto body = getLocalBounds().withTrimmedTop (kHeader + 1);
    auto pages = body.removeFromLeft (kPagesW).reduced (10, 12);
    for (auto* b : pageButtons)
    {
        b->setBounds (pages.removeFromTop (30));
        pages.removeFromTop (4);
    }
    viewport.setBounds (body.reduced (8));
    if (page == static_cast<int> (editor.pages.size()) && ! editor.nameFields.empty())
    {
        const int width = std::max (300, viewport.getWidth() - viewport.getScrollBarThickness());
        grid.setSize (width, 40 + static_cast<int> (nameEditors.size()) * 38);
        for (int i = 0; i < nameEditors.size(); ++i)
        {
            const int y = 8 + i * 38;
            nameLabels[i]->setBounds (8, y, 180, 28);
            nameEditors[i]->setBounds (196, y, width - 208, 28);
        }
        return;
    }
    const int columns = std::max (1, (viewport.getWidth() - viewport.getScrollBarThickness()) / kCellW);
    const int rows = (cells.size() + columns - 1) / columns;
    grid.setSize (columns * kCellW, rows * kCellH);
    for (int i = 0; i < cells.size(); ++i)
        cells[i]->setBounds ((i % columns) * kCellW, (i / columns) * kCellH, kCellW, kCellH);
}

} // namespace arranger::ui

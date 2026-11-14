#pragma once

#include "Theme.h"
#include "../Session.h"

namespace arranger::ui
{

/**
    The sound editor of a hardware synth (Roland D-05, Novation XioSynth...).
    Built from the `editor:` part of the synth's device profile: one page per
    part of the sound, a knob or a list for each setting. Each change goes
    to the synth at once; what the synth sends back moves the knobs.
*/
class SoundEditorView final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    SoundEditorView (Session&, const std::string& profileId);
    ~SoundEditorView() override;

    /** The device profiles that have an editor, the plugged-in ones first. */
    static std::vector<const DeviceProfile*> editableDevices (const Session&);

    void showDevice (const std::string& profileId);
    void showPage (int index);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Knob;
    class Cell;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuildPage();
    void refreshValues();
    void updateStatus();
    void fromSynth (const std::string& port, const std::vector<std::uint8_t>& bytes);
    const DeviceProfile* profile() const;

    Session& session;
    std::string profileId;
    int page = 0;
    int watcher = 0;
    int shownVersion = -1;
    SynthEditor editor;  ///< a copy: the library can be loaded again while the window is open
    std::unique_ptr<synth_edit::Listener> listener;
    int listenerChannel = 0;

    juce::ComboBox device, channel;
    juce::TextButton readButton { "Read from Synth" }, namesButton { "Get Sound Names" }, sendAllButton { "Send All" };
    juce::Label status;
    juce::OwnedArray<juce::TextButton> pageButtons;
    juce::Viewport viewport;
    juce::Component grid;
    juce::OwnedArray<Cell> cells;
};

} // namespace arranger::ui

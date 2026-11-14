#pragma once

#include "Theme.h"
#include "../MidiAiService.h"
#include "../Session.h"

namespace arranger::ui
{

/**
    The AI menu's window: one AI music tool (humanize, drumify, continue,
    write a part, melody over chords) with its track, bars and settings, the
    progress, and the install of the tools when they are missing.
*/
class AiToolsView final : public juce::Component
{
public:
    /** `tool`: an ai:: tool name (ai_humanize, ai_drumify...) or "install". */
    AiToolsView (Session&, MidiAiService&, const juce::String& tool);
    ~AiToolsView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Starts at once (tests and screenshots). */
    void go();

    /** The window title for a tool. */
    static juce::String titleFor (const juce::String& tool);

private:
    void refresh();
    void install();
    juce::String argumentsJson() const;
    juce::String neededHelper() const;
    void setStatus (const juce::String&);

    Session& session;
    MidiAiService& service;
    juce::String tool;
    std::vector<TrackId> trackIds; ///< by combo item id - 1
    juce::Label title, about, trackLabel, engineLabel, soundLabel, barsLabel, toLabel, variationLabel, status;
    juce::ComboBox track, engine, sound;
    juce::TextEditor fromBar, toBar, output;
    juce::Slider variation { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ToggleButton withAnticipation { "Also the Anticipatory Music Transformer (\"write a part\", about 2 GB)" };
    juce::TextButton goButton { "Make It" }, stopButton { "Stop" }, installButton { "Install AI Music Tools" };
    double progress = 0.0;
    juce::ProgressBar bar { progress };
    bool running = false;
};

} // namespace arranger::ui

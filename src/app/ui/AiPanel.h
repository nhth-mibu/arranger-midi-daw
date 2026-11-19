#pragma once

#include "AiToolsView.h"
#include "ArrangeView.h"
#include "ChatView.h"
#include "ChordsView.h"
#include "GrooveView.h"
#include "SongFormsView.h"
#include "StyleFromAudioView.h"

#include <memory>
#include <iterator>

namespace arranger::ui
{

/**
    The AI dock at the bottom of the right side panel: the chat and the AI music
    tools in one compact panel. The tool is chosen from a menu, so the panel does
    not need a separate window for each tool.
*/
class AiPanel final : public juce::Component
{
public:
    AiPanel (Session&, MidiAiService&, AssistantChat&);

    /** Called when the user closes the dock. */
    std::function<void()> onClose;

    void showChat();
    void showTool (const juce::String& tool);
    /** Starts the selected music tool at once (used by screenshots and the menu). */
    void startTool();
    /** See ArrangeView::choose. */
    void chooseArrangeGoal (const juce::String& goal, int fromBar, int toBar)
    {
        if (arrangeView)
            arrangeView->choose (goal, fromBar, toBar);
    }
    /** Shows the Smart Chords tool with other chords for the lane chord at a time. */
    void reharmonizeAt (Tick at)
    {
        showTool ("chords");
        if (chordsView)
            chordsView->reharmonizeAt (at);
    }
    /** Shows the Smart Chords tool, ready to type chords from a bar. */
    void typeChordsAt (int bar, const juce::String& text = {})
    {
        showTool ("chords");
        if (chordsView)
            chordsView->typeAt (bar, text);
    }
    /** Style from Audio: chooses the recording (0: keep), and starts when `start` is true. */
    void chooseStyleClip (ClipId clip, bool start)
    {
        if (styleView)
        {
            if (clip != 0)
                styleView->chooseClip (clip);
            if (start)
                styleView->start();
        }
    }
    /** Groove: takes a groove from words (screenshots and the menu). */
    void grooveFromWords (const juce::String& words)
    {
        showTool ("groove");
        if (grooveView)
            grooveView->takeFromWords (words);
    }
    /** Song Forms: makes a form from section names. */
    void makeSongForm (const juce::String& name, const juce::String& parts)
    {
        showTool ("song_forms");
        if (formsView)
            formsView->makeForm (name, parts);
    }
    /** Opens a style file made by Style from Audio. */
    std::function<void (const juce::File&)> onOpenStyle;
    void sendChatMessage (const juce::String&);
    void setChatInput (const juce::String&);
    void setSpeechState (bool recording, bool speakReplies);

    std::function<void (const juce::String&)> onAssistantReply;
    std::function<void()> onMic;
    std::function<void (bool)> onSpeakToggle;

    bool chatSelected() const noexcept { return current == "chat"; }
    juce::String selectedTool() const { return current; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void select (const juce::String& tool);
    void refreshToolBox();

    Session& session;
    MidiAiService& midiAi;
    AssistantChat& chat;

    juce::ComboBox toolBox;
    juce::TextButton closeButton { "x" };
    std::unique_ptr<ChatView> chatView;
    std::unique_ptr<AiToolsView> toolsView;
    std::unique_ptr<ArrangeView> arrangeView;
    std::unique_ptr<ChordsView> chordsView;
    std::unique_ptr<StyleFromAudioView> styleView;
    std::unique_ptr<GrooveView> grooveView;
    std::unique_ptr<SongFormsView> formsView;
    juce::String current = "chat";
    bool updatingBox = false;
    bool microphoneRecording = false, speakingReplies = false;
};

} // namespace arranger::ui

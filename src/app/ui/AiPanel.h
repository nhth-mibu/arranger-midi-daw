#pragma once

#include "AiToolsView.h"
#include "ChatView.h"

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
    void sendChatMessage (const juce::String&);
    void setChatInput (const juce::String&);
    void setVoiceAvailable (bool);
    void setMicRecording (bool);

    std::function<void (const juce::String&)> onAssistantReply;
    std::function<void()> onMic;

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
    juce::String current = "chat";
    bool updatingBox = false;
    bool voiceAvailable = false;
};

} // namespace arranger::ui

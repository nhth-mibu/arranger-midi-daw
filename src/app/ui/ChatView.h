#pragma once

#include "HeaderBar.h"
#include "Theme.h"
#include "../AssistantChat.h"

namespace arranger::ui
{

/**
    The AI chat box: choose an assistant, type what you want ("add a bass line
    on C Am F G for 8 bars"), and the assistant changes the song. Each change
    is one undo step.
*/
class ChatView final : public juce::Component
{
public:
    explicit ChatView (AssistantChat&);
    ~ChatView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Types a message and sends it (also for tests and screenshots). */
    void sendMessage (const juce::String&);
    /** Puts words into the input box without sending them. */
    void setInputText (const juce::String&);
    /** Lights the chat's speech buttons from the global speech settings. */
    void setSpeechState (bool recording, bool speakReplies);

    /** Called once for each complete assistant reply, after the history is updated. */
    std::function<void (const juce::String&)> onReply;
    /** The microphone button was pressed. */
    std::function<void()> onMic;
    /** The speaker button was pressed: true turns spoken replies on. */
    std::function<void (bool)> onSpeakToggle;

private:
    void send();
    void showSetup();
    void refreshChoices();
    void refreshButtons();
    void showConversation();
    void addLine (const juce::String& text, juce::Colour, bool bold);
    void onEvent (const assistant::Event&);

    AssistantChat& chat;
    juce::ComboBox assistantBox;
    juce::TextButton setupButton { "Set up..." }, newChatButton { "New chat" }, sendButton { "Send" };
    IconButton micButton { "Voice input", icons::Icon::microphone, "recording" };
    IconButton speakButton { "Speak replies", icons::Icon::speakingPerson };
    juce::TextEditor conversation, input;
    juce::Label status;
    std::unique_ptr<juce::AlertWindow> setupWindow;
    bool answerStarted = false;
};

} // namespace arranger::ui

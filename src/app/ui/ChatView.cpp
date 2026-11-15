#include "ChatView.h"

namespace arranger::ui
{

using assistant::Event;

namespace
{
    constexpr int kGap = 8;

    juce::String readableToolName (const juce::String& name)
    {
        return name.replaceCharacter ('_', ' ');
    }

    /** Assistants write Markdown; the chat shows plain text. */
    juce::String plainText (const juce::String& markdown)
    {
        juce::StringArray lines;
        lines.addLines (markdown);
        for (auto& l : lines)
        {
            l = l.replace ("**", "").replace ("__", "").replace ("`", "");
            if (l.trimStart().startsWithChar ('#'))
                l = l.trimStart().trimCharactersAtStart ("#").trimStart();
            if (l.startsWith ("- ") || l.startsWith ("* "))
                l = juce::String::fromUTF8 ("\xe2\x80\xa2 ") + l.substring (2);
        }
        return lines.joinIntoString ("\n");
    }
}

ChatView::ChatView (AssistantChat& c) : chat (c)
{
    const auto& theme = currentTheme();

    addAndMakeVisible (assistantBox);
    assistantBox.setTooltip ("The AI assistant to talk to");
    assistantBox.onChange = [this] {
        const auto choices = chat.choices();
        const int index = assistantBox.getSelectedItemIndex();
        if (juce::isPositiveAndBelow (index, choices.size()) && choices[index].id != chat.chosen())
        {
            chat.choose (choices[index].id);
            showConversation();
        }
    };

    addAndMakeVisible (setupButton);
    setupButton.setTooltip ("The program's path, or the API key, address and model");
    setupButton.onClick = [this] { showSetup(); };

    addAndMakeVisible (newChatButton);
    newChatButton.setTooltip ("Forget this conversation and start again");
    newChatButton.onClick = [this] {
        chat.newChat();
        showConversation();
        refreshButtons();
    };

    addAndMakeVisible (conversation);
    conversation.setMultiLine (true, true);
    conversation.setReadOnly (true);
    conversation.setCaretVisible (false);
    conversation.setScrollbarsShown (true);
    conversation.setColour (juce::TextEditor::backgroundColourId, theme.colour ("panelRaised"));
    conversation.setColour (juce::TextEditor::outlineColourId, theme.colour ("line"));

    addAndMakeVisible (input);
    input.setMultiLine (true, true);
    input.setReturnKeyStartsNewLine (false);
    input.setTextToShowWhenEmpty ("Tell the assistant what to do, for example: add a bass line on C Am F G for 8 bars", theme.colour ("textDim"));
    input.setFont (theme.font (15.0f));
    input.onReturnKey = [this] { send(); };

    addAndMakeVisible (sendButton);
    sendButton.onClick = [this] {
        if (chat.isBusy())
            chat.stop();
        else
            send();
    };

    addAndMakeVisible (status);
    status.setFont (theme.font (13.0f));
    status.setColour (juce::Label::textColourId, theme.colour ("textMuted"));

    chat.onIdle = [this] { refreshButtons(); };

    refreshChoices();
    showConversation();
    refreshButtons();
    setSize (520, 640);
}

ChatView::~ChatView()
{
    chat.onIdle = nullptr;
}

void ChatView::paint (juce::Graphics& g)
{
    g.fillAll (currentTheme().colour ("panel"));
}

void ChatView::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (30);
    newChatButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (kGap);
    setupButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (kGap);
    assistantBox.setBounds (top);
    r.removeFromTop (kGap);

    status.setBounds (r.removeFromBottom (22));
    auto bottom = r.removeFromBottom (72);
    sendButton.setBounds (bottom.removeFromRight (80).withSizeKeepingCentre (80, 32));
    bottom.removeFromRight (kGap);
    input.setBounds (bottom);
    r.removeFromBottom (kGap);
    conversation.setBounds (r);
}

void ChatView::refreshChoices()
{
    assistantBox.clear (juce::dontSendNotification);
    const auto choices = chat.choices();
    bool addedApiHeading = false;
    for (int i = 0; i < choices.size(); ++i)
    {
        if (i == 0)
            assistantBox.addSectionHeading ("With your subscription (a program on this computer)");
        if (choices[i].usesApiKey && ! addedApiHeading)
        {
            assistantBox.addSectionHeading ("With an API key");
            addedApiHeading = true;
        }
        assistantBox.addItem (choices[i].name, i + 1);
        if (choices[i].id == chat.chosen())
            assistantBox.setSelectedId (i + 1, juce::dontSendNotification);
    }
}

void ChatView::refreshButtons()
{
    const bool busy = chat.isBusy();
    sendButton.setButtonText (busy ? "Stop" : "Send");
    assistantBox.setEnabled (! busy);
    setupButton.setEnabled (! busy);
    if (! busy)
        status.setText (chat.chosenUsesApiKey() ? "Uses your API key. Each change is one undo step."
                                                : "Uses the program's own sign-in. Each change is one undo step.",
                        juce::dontSendNotification);
}

void ChatView::addLine (const juce::String& text, juce::Colour colour, bool bold)
{
    conversation.moveCaretToEnd();
    conversation.setFont (currentTheme().font (15.0f, bold));
    conversation.setColour (juce::TextEditor::textColourId, colour);
    conversation.insertTextAtCaret (text);
    conversation.moveCaretToEnd();
}

void ChatView::showConversation()
{
    conversation.clear();
    const auto& theme = currentTheme();
    if (chat.messages().empty())
    {
        addLine ("Ask for changes in plain words. For example:\n"
                 "  - Add a piano track and play C, Am, F, G for 4 bars\n"
                 "  - Make a rock beat for 8 bars\n"
                 "  - Find the chords and show guitar chords\n"
                 "  - Put the drums and bass in scene 2 of the Clip grid\n\n"
                 "Use Undo (Cmd+Z) to take back a change.\n",
                 theme.colour ("textMuted"), false);
        return;
    }
    for (const auto& m : chat.messages())
    {
        addLine (m.fromUser ? "You\n" : "Assistant\n", theme.colour (m.fromUser ? "accent" : "textMuted"), true);
        addLine ((m.fromUser ? juce::String::fromUTF8 (m.text.c_str()) : plainText (juce::String::fromUTF8 (m.text.c_str()))) + "\n\n", theme.colour ("text"), false);
    }
}

void ChatView::sendMessage (const juce::String& text)
{
    input.setText (text, false);
    send();
}

void ChatView::send()
{
    const auto text = input.getText().trim();
    if (text.isEmpty() || chat.isBusy())
        return;

    if (chat.messages().empty())
        conversation.clear();
    const auto& theme = currentTheme();
    addLine ("You\n", theme.colour ("accent"), true);
    addLine (text + "\n\n", theme.colour ("text"), false);
    input.clear();
    answerStarted = false;

    chat.send (text, [safe = juce::Component::SafePointer<ChatView> (this)] (const Event& e) {
        if (safe != nullptr)
            safe->onEvent (e);
    });
    if (chat.isBusy())
        status.setText ("Thinking...", juce::dontSendNotification);
    refreshButtons();
    if (chat.isBusy())
        status.setText ("Thinking...", juce::dontSendNotification);
}

void ChatView::onEvent (const Event& e)
{
    const auto& theme = currentTheme();
    const auto value = juce::String::fromUTF8 (e.value.c_str());
    switch (e.kind)
    {
        case Event::Kind::text:
            if (! answerStarted)
            {
                addLine ("Assistant\n", theme.colour ("textMuted"), true);
                answerStarted = true;
            }
            // A whole message gets a blank line after it; a program that writes plain text sends one line at a time.
            addLine (value.endsWithChar ('\n') ? plainText (value) + "\n" : plainText (value) + "\n\n", theme.colour ("text"), false);
            break;
        case Event::Kind::tool:
            status.setText ("Working: " + readableToolName (value) + "...", juce::dontSendNotification);
            break;
        case Event::Kind::session:
            break;
        case Event::Kind::done:
            break;
        case Event::Kind::error:
            addLine ((answerStarted ? "" : "Assistant\n"), theme.colour ("textMuted"), true);
            addLine (value + "\n\n", theme.colour ("recording"), false);
            break;
    }
}

void ChatView::showSetup()
{
    const auto id = chat.chosen();
    if (chat.chosenUsesApiKey())
    {
        const auto values = chat.apiValues (id);
        const bool anthropic = id == "api-anthropic";
        const bool deepSeek = id == "api-deepseek";
        setupWindow = std::make_unique<juce::AlertWindow> (anthropic ? "Anthropic API key"
                                                         : deepSeek ? "DeepSeek API key"
                                                                    : "AI service with an API key",
                                                           anthropic ? "Get a key at console.anthropic.com. Leave the address and the model empty for the usual ones (model: claude-opus-5-5)."
                                                         : deepSeek ? "Get a key at platform.deepseek.com. The address and the model are filled in for DeepSeek."
                                                                    : "Any service with an OpenAI-style chat API. Examples of addresses:\n"
                                                                      "OpenAI: https://api.openai.com/v1\n"
                                                                      "Gemini: https://generativelanguage.googleapis.com/v1beta/openai\n"
                                                                      "Ollama on this computer: http://localhost:11434/v1 (no key)",
                                                           juce::MessageBoxIconType::NoIcon, this);
        setupWindow->addTextEditor ("key", values.key, "API key", true);
        setupWindow->addTextEditor ("url", values.url, "Address (empty: the usual one)");
        setupWindow->addTextEditor ("model", values.model, anthropic ? "Model (empty: claude-opus-5-5)"
                                                                      : deepSeek ? "Model (empty: deepseek-chat)" : "Model");
    }
    else
    {
        setupWindow = std::make_unique<juce::AlertWindow> ("Assistant program",
                                                           chat.programHelp (id) + "\n\nIf the app can't find the program, type its full path.",
                                                           juce::MessageBoxIconType::NoIcon, this);
        setupWindow->addTextEditor ("command", chat.programCommand (id), "Program");
    }
    setupWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    setupWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    setupWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this, id] (int result) {
        if (result == 1 && setupWindow != nullptr)
        {
            if (chat.chosenUsesApiKey())
                chat.setApiValues (id, { setupWindow->getTextEditorContents ("url"), setupWindow->getTextEditorContents ("model"),
                                         setupWindow->getTextEditorContents ("key") });
            else
                chat.setProgramCommand (id, setupWindow->getTextEditorContents ("command"));
        }
        setupWindow.reset();
    }));
}

} // namespace arranger::ui

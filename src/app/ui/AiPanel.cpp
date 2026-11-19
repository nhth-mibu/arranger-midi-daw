#include "AiPanel.h"

#include "Panels.h"

namespace arranger::ui
{

namespace
{
    struct ToolItem
    {
        const char* id;
        const char* name;
    };

    constexpr ToolItem musicTools[] = {
        { "ai_humanize", "Humanize" },
        { "ai_drumify", "Drumify" },
        { "ai_continue", "Continue with AI" },
        { "ai_write_part", "Write a Part" },
        { "ai_melody", "Melody over Chords" },
        { "arrangement_doctor", "Arrangement Doctor" },
        { "hardware_fit", "Fit to My Synths" },
        { "groove", "Groove" },
        { "song_forms", "Song Forms" },
        { "chords", "Smart Chords" },
        { "style_from_audio", "Style from Audio" },
        { "transcription_questions", "Check the Transcription" },
        { "install", "Install AI Music Tools" },
    };
}

AiPanel::AiPanel (Session& s, MidiAiService& m, AssistantChat& c)
    : session (s), midiAi (m), chat (c)
{
    addAndMakeVisible (toolBox);
    toolBox.setTooltip ("Choose the AI chat or a music tool");
    toolBox.onChange = [this] {
        if (updatingBox)
            return;
        const int id = toolBox.getSelectedId();
        if (id == 1)
            showChat();
        else if (id >= 2 && id - 2 < static_cast<int> (std::size (musicTools)))
            showTool (musicTools[id - 2].id);
    };

    addAndMakeVisible (closeButton);
    closeButton.setTooltip ("Close the AI panel");
    closeButton.setColour (juce::TextButton::buttonColourId, currentTheme().colour ("panelRaised"));
    closeButton.setColour (juce::TextButton::textColourOffId, currentTheme().colour ("textMuted"));
    closeButton.onClick = [this] { if (onClose) onClose(); };

    refreshToolBox();
    showChat();
}

void AiPanel::refreshToolBox()
{
    updatingBox = true;
    toolBox.clear (juce::dontSendNotification);
    toolBox.addItem ("Chat", 1);
    toolBox.addSectionHeading ("Music tools");
    for (int i = 0; i < static_cast<int> (std::size (musicTools)); ++i)
        toolBox.addItem (musicTools[i].name, i + 2);
    toolBox.setSelectedId (1, juce::dontSendNotification);
    updatingBox = false;
}

void AiPanel::select (const juce::String& tool)
{
    current = tool;
    updatingBox = true;
    if (tool == "chat")
        toolBox.setSelectedId (1, juce::dontSendNotification);
    else
        for (int i = 0; i < static_cast<int> (std::size (musicTools)); ++i)
            if (tool == musicTools[i].id)
                toolBox.setSelectedId (i + 2, juce::dontSendNotification);
    updatingBox = false;
    resized();
}

void AiPanel::showChat()
{
    if (! chatView)
    {
        chatView = std::make_unique<ChatView> (chat);
        chatView->onReply = [this] (const juce::String& text) { if (onAssistantReply) onAssistantReply (text); };
        chatView->onMic = [this] { if (onMic) onMic(); };
        chatView->onSpeakToggle = [this] (bool on) { if (onSpeakToggle) onSpeakToggle (on); };
        chatView->setSpeechState (microphoneRecording, speakingReplies);
        addAndMakeVisible (*chatView);
    }
    toolsView.reset();
    arrangeView.reset();
    chordsView.reset();
    styleView.reset();
    grooveView.reset();
    formsView.reset();
    chatView->setVisible (true);
    select ("chat");
}

void AiPanel::showTool (const juce::String& tool)
{
    if (tool.isEmpty() || tool == "chat")
    {
        showChat();
        return;
    }
    if (chatView)
        chatView->setVisible (false);
    if (tool != "groove")
        grooveView.reset();
    if (tool != "song_forms")
        formsView.reset();
    if (tool == "groove" || tool == "song_forms")
    {
        toolsView.reset();
        arrangeView.reset();
        chordsView.reset();
        styleView.reset();
        if (tool == "groove" && ! grooveView)
        {
            grooveView = std::make_unique<GrooveView> (session);
            addAndMakeVisible (*grooveView);
        }
        if (tool == "song_forms" && ! formsView)
        {
            formsView = std::make_unique<SongFormsView> (session);
            addAndMakeVisible (*formsView);
        }
    }
    else if (tool == "chords")
    {
        if (current == "chords" && chordsView)
            return;
        toolsView.reset();
        arrangeView.reset();
        styleView.reset();
        chordsView = std::make_unique<ChordsView> (session);
        addAndMakeVisible (*chordsView);
    }
    else if (tool == "arrangement_doctor" || tool == "hardware_fit" || tool == "transcription_questions")
    {
        toolsView.reset();
        chordsView.reset();
        styleView.reset();
        arrangeView = std::make_unique<ArrangeView> (session, tool);
        addAndMakeVisible (*arrangeView);
    }
    else if (tool == "style_from_audio")
    {
        toolsView.reset();
        arrangeView.reset();
        chordsView.reset();
        if (! styleView)
        {
            styleView = std::make_unique<StyleFromAudioView> (session);
            styleView->onOpenStyle = [this] (const juce::File& f) { if (onOpenStyle) onOpenStyle (f); };
            addAndMakeVisible (*styleView);
        }
    }
    else
    {
        arrangeView.reset();
        chordsView.reset();
        styleView.reset();
        toolsView = std::make_unique<AiToolsView> (session, midiAi, tool);
        addAndMakeVisible (*toolsView);
    }
    select (tool);
}

void AiPanel::startTool()
{
    if (toolsView)
        toolsView->go();
}

void AiPanel::sendChatMessage (const juce::String& message)
{
    showChat();
    if (chatView && message.isNotEmpty())
        chatView->sendMessage (message);
}

void AiPanel::setChatInput (const juce::String& text)
{
    showChat();
    if (chatView != nullptr)
        chatView->setInputText (text);
}

void AiPanel::setSpeechState (bool recording, bool speakReplies)
{
    microphoneRecording = recording;
    speakingReplies = speakReplies;
    if (chatView != nullptr)
        chatView->setSpeechState (recording, speakReplies);
}

void AiPanel::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    const auto header = getLocalBounds().removeFromTop (38).toFloat();
    g.setColour (t.colour ("panelRaised"));
    g.fillRoundedRectangle (header.reduced (8.0f, 4.0f), 7.0f);

    // A small copper AI badge, like the header button.
    const auto badge = juce::Rectangle<float> (14.0f, 11.0f, 26.0f, 16.0f);
    g.setColour (t.colour ("accent"));
    g.fillRoundedRectangle (badge, 5.0f);
    g.setColour (t.colour ("accentText"));
    g.setFont (t.font (11.0f, true));
    g.drawText ("AI", badge, juce::Justification::centred);

    g.setColour (t.colour ("line"));
    g.fillRect (getLocalBounds().removeFromTop (38).removeFromBottom (1));
}

void AiPanel::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (38).reduced (10, 6);
    closeButton.setBounds (header.removeFromRight (26));
    header.removeFromRight (6);
    header.removeFromLeft (38); // the badge
    toolBox.setBounds (header);

    if (chatView)
        chatView->setBounds (r);
    if (toolsView)
        toolsView->setBounds (r);
    if (arrangeView)
        arrangeView->setBounds (r);
    if (chordsView)
        chordsView->setBounds (r);
    if (styleView)
        styleView->setBounds (r);
    if (grooveView)
        grooveView->setBounds (r);
    if (formsView)
        formsView->setBounds (r);
}

} // namespace arranger::ui

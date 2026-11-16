#include "SpeechView.h"

namespace arranger::ui
{

SpeechView::SpeechView (SpeechService& s) : speech (s)
{
    addAndMakeVisible (title);
    title.setText ("Speech", juce::dontSendNotification);
    title.setFont (currentTheme().font (20.0f, true));

    addAndMakeVisible (speechOn);
    addAndMakeVisible (speakReplies);
    addAndMakeVisible (voiceInput);
    addAndMakeVisible (engineBox);
    addAndMakeVisible (voiceBox);
    addAndMakeVisible (testButton);
    addAndMakeVisible (installButton);
    addAndMakeVisible (closeButton);
    addAndMakeVisible (status);
    addAndMakeVisible (hint);

    speechOn.setTooltip ("Use speech for the AI chat. This switch turns both speaking and listening off.");
    speakReplies.setTooltip ("Speak each complete AI reply once. There are no buttons on the messages.");
    voiceInput.setTooltip ("Show the microphone button in the AI chat. Hold it while you speak.");

    status.setFont (currentTheme().font (13.0f));
    status.setColour (juce::Label::textColourId, currentTheme().colour ("textMuted"));
    hint.setFont (currentTheme().font (12.0f));
    hint.setColour (juce::Label::textColourId, currentTheme().colour ("textMuted"));
    hint.setText ("Kitten and Kokoro download once, then work offline. Voice input uses the built-in Whisper model from Options > Audio Analysis Tools.", juce::dontSendNotification);

    engineBox.addItem ("System voice", 1);
    engineBox.addItem ("Kitten (small, offline)", 2);
    engineBox.addItem ("Kokoro (high quality, offline)", 3);
    engineBox.onChange = [this]
    {
        const auto e = engineBox.getSelectedId() == 2 ? SpeechService::Engine::kitten
                     : engineBox.getSelectedId() == 3 ? SpeechService::Engine::kokoro
                                                      : SpeechService::Engine::system;
        speech.setEngine (e);
        refresh();
    };
    voiceBox.onChange = [this] { speech.setVoice (juce::jmax (0, voiceBox.getSelectedId() - 1)); };
    installButton.onClick = [this]
    {
        if (installing)
            return;
        const auto e = speech.engine();
        if (e == SpeechService::Engine::system || speech.isEngineInstalled (e))
            return;
        installing = true;
        installButton.setEnabled (false);
        status.setText ("Downloading the voice. This can take a minute...", juce::dontSendNotification);
        auto safe = juce::Component::SafePointer<SpeechView> (this);
        speech.installEngine (e, [safe] (bool ok, const juce::String& message) {
            if (safe == nullptr)
                return;
            safe->installing = false;
            safe->installButton.setEnabled (true);
            safe->status.setText (message, juce::dontSendNotification);
            safe->refresh();
            if (ok)
                safe->speech.speak ("The voice is ready.");
        });
    };

    const auto update = [this]
    {
        speech.setEnabled (speechOn.getToggleState());
        speech.setSpeaksReplies (speakReplies.getToggleState());
        speech.setTakesVoiceInput (voiceInput.getToggleState());
        refresh();
    };
    speechOn.onClick = update;
    speakReplies.onClick = update;
    voiceInput.onClick = update;
    testButton.onClick = [this]
    {
        if (! speechOn.getToggleState())
        {
            speechOn.setToggleState (true, juce::dontSendNotification);
            speech.setEnabled (true);
        }
        speech.setSpeaksReplies (true);
        speakReplies.setToggleState (true, juce::dontSendNotification);
        speech.speak ("The Arranger assistant can speak. This is a test of the speech voice.");
    };
    closeButton.onClick = [this]
    {
        if (onClose)
            onClose();
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            delete window;
    };

    refresh();
    setSize (460, 390);
}

SpeechView::~SpeechView() = default;

void SpeechView::refresh()
{
    speechOn.setToggleState (speech.isEnabled(), juce::dontSendNotification);
    speakReplies.setToggleState (speech.speaksReplies(), juce::dontSendNotification);
    voiceInput.setToggleState (speech.takesVoiceInput(), juce::dontSendNotification);
    speakReplies.setEnabled (speech.isEnabled());
    voiceInput.setEnabled (speech.isEnabled());
    const auto chosen = speech.engine();
    testButton.setEnabled (speech.isEnabled() && ! installing && speech.isEngineInstalled (chosen));

    const auto e = chosen;
    engineBox.setSelectedId (e == SpeechService::Engine::kitten ? 2 : e == SpeechService::Engine::kokoro ? 3 : 1, juce::dontSendNotification);
    voiceBox.clear (juce::dontSendNotification);
    const auto voices = SpeechService::voiceNames (e);
    for (int i = 0; i < voices.size(); ++i)
        voiceBox.addItem (voices[i], i + 1);
    voiceBox.setSelectedId (juce::jlimit (1, voices.size(), speech.voice() + 1), juce::dontSendNotification);
    voiceBox.setEnabled (e != SpeechService::Engine::system && speech.isEngineInstalled (e) && ! installing);
    const bool installed = speech.isEngineInstalled (e);
    installButton.setVisible (e != SpeechService::Engine::system && ! installed);
    installButton.setEnabled (! installing);

    if (! installing)
        status.setText (speech.isEnabled()
                            ? (speech.isSpeaking() ? "Speaking..." : (e == SpeechService::Engine::system || installed ? "Speech is on." : "This voice is not installed yet."))
                            : "Speech is off.",
                        juce::dontSendNotification);
}

void SpeechView::paint (juce::Graphics& g)
{
    g.fillAll (currentTheme().colour ("panel"));
}

void SpeechView::resized()
{
    auto r = getLocalBounds().reduced (22);
    title.setBounds (r.removeFromTop (28));
    r.removeFromTop (12);
    speechOn.setBounds (r.removeFromTop (30));
    speakReplies.setBounds (r.removeFromTop (30));
    voiceInput.setBounds (r.removeFromTop (30));
    r.removeFromTop (8);
    auto engineRow = r.removeFromTop (28);
    engineBox.setBounds (engineRow.removeFromLeft (240));
    engineRow.removeFromLeft (8);
    installButton.setBounds (engineRow);
    r.removeFromTop (6);
    auto voiceRow = r.removeFromTop (28);
    voiceBox.setBounds (voiceRow.removeFromLeft (240));
    r.removeFromTop (6);
    status.setBounds (r.removeFromTop (22));
    r.removeFromTop (8);
    hint.setBounds (r.removeFromTop (44));
    auto buttons = r.removeFromBottom (34);
    closeButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    testButton.setBounds (buttons.removeFromRight (110));
}

} // namespace arranger::ui

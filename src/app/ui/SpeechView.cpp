#include "SpeechView.h"

namespace arranger::ui
{

SpeechView::SpeechView (SpeechService& s) : speech (s)
{
    addAndMakeVisible (title);
    title.setText ("Speech settings", juce::dontSendNotification);
    title.setFont (currentTheme().font (20.0f, true));

    addAndMakeVisible (engineBox);
    addAndMakeVisible (voiceBox);
    addAndMakeVisible (testButton);
    addAndMakeVisible (installButton);
    addAndMakeVisible (closeButton);
    addAndMakeVisible (status);
    addAndMakeVisible (hint);

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
    testButton.onClick = [this]
    {
        speech.setEnabled (true);
        speech.setSpeaksReplies (true);
        speech.speak ("This is a test of the selected speech voice.");
    };
    closeButton.onClick = [this]
    {
        if (onClose)
            onClose();
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            delete window;
    };

    status.setFont (currentTheme().font (13.0f));
    status.setColour (juce::Label::textColourId, currentTheme().colour ("textMuted"));
    hint.setFont (currentTheme().font (12.0f));
    hint.setColour (juce::Label::textColourId, currentTheme().colour ("textMuted"));
    hint.setText ("Use the microphone and speaker buttons in the AI chat to turn voice input and spoken replies on or off. "
                  "Kitten and Kokoro download once, then work offline. Voice input needs a Whisper model from Options > Audio Analysis Tools.",
                  juce::dontSendNotification);

    refresh();
    setSize (460, 300);
}

SpeechView::~SpeechView() = default;

void SpeechView::refresh()
{
    const auto e = speech.engine();
    engineBox.setSelectedId (e == SpeechService::Engine::kitten ? 2 : e == SpeechService::Engine::kokoro ? 3 : 1, juce::dontSendNotification);

    voiceBox.clear (juce::dontSendNotification);
    const auto voices = SpeechService::voiceNames (e);
    for (int i = 0; i < voices.size(); ++i)
        voiceBox.addItem (voices[i], i + 1);
    voiceBox.setSelectedId (juce::jlimit (1, voices.size(), speech.voice() + 1), juce::dontSendNotification);

    const bool installed = speech.isEngineInstalled (e);
    voiceBox.setEnabled (e != SpeechService::Engine::system && installed && ! installing);
    installButton.setVisible (e != SpeechService::Engine::system && ! installed);
    installButton.setEnabled (! installing);
    testButton.setEnabled (! installing && installed);
    if (! installing)
        status.setText (speech.isSpeaking() ? "Speaking..." : (installed ? "The voice is ready." : "This voice is not installed yet."),
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
    hint.setBounds (r.removeFromTop (54));
    auto buttons = r.removeFromBottom (34);
    closeButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    testButton.setBounds (buttons.removeFromRight (110));
}

} // namespace arranger::ui

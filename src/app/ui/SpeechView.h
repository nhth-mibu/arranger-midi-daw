#pragma once

#include "../Speech.h"
#include "Theme.h"

namespace arranger::ui
{

/** Voice settings: engine, voice, install and test. The chat has the on/off buttons. */
class SpeechView final : public juce::Component
{
public:
    explicit SpeechView (SpeechService&);
    ~SpeechView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onClose;

private:
    void refresh();

    SpeechService& speech;
    juce::ComboBox engineBox, voiceBox;
    juce::TextButton testButton { "Test voice" }, installButton { "Install voice" }, closeButton { "Close" };
    juce::Label title, status, hint;
    bool installing = false;
};

} // namespace arranger::ui

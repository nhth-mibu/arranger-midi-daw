#pragma once

#include "Theme.h"
#include "../AudioAnalysis.h"
#include "../Session.h"

namespace arranger::ui
{

/**
    Track > Analyse Audio: choose what to find in an audio clip (stems, notes,
    chords, instruments, lyrics), see the progress and the result, and set up
    the tools and the Whisper model.
*/
class AnalysisView final : public juce::Component, private juce::Timer
{
public:
    AnalysisView (Session&, ClipId);
    ~AnalysisView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Asks the AI chat to correct the lyrics; the main window sets it. */
    std::function<void (const juce::String& message)> onAskAi;

    /** Starts at once (for tests and screenshots). */
    void analyse();

private:
    void timerCallback() override;
    void refresh();
    void install();
    void downloadModel();
    void log (const juce::String&);
    juce::String resultText (const AnalysisService::Status&) const;

    Session& session;
    AnalysisService& service;
    ClipId clip;
    juce::Label title, toolsLine, modelLine, stepLine;
    juce::ToggleButton stems { "Stems: vocals, drums, bass, other (Demucs)" }, notes { "Notes: a MIDI track per stem (Basic Pitch)" },
        chords { "Chords: chord symbols on the score" }, instruments { "Instruments: name what plays (PANNs)" },
        lyrics { "Lyrics: write down the words (Whisper)" };
    juce::ComboBox language, model;
    juce::TextEditor prompt, output;
    juce::TextButton analyseButton { "Analyse" }, stopButton { "Stop" }, installButton { "Install Tools..." },
        downloadButton { "Download" }, askAiButton { "Fix Lyrics with AI" };
    double progress = 0.0;
    juce::ProgressBar bar { progress };
    juce::String installLog;
    double modelProgress = -1.0;
    AnalysisService::Status::State shownState = AnalysisService::Status::State::idle;
};

} // namespace arranger::ui

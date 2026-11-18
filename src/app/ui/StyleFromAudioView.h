#pragma once

#include "Theme.h"
#include "../Session.h"
#include "../StyleFromAudio.h"

#include <memory>
#include <vector>

namespace arranger::ui
{

/**
    The AI panel's Style from Audio: choose a recording, make a style, see
    how well each section and part matches the recording, play the recording
    against the style (A/B), and save the style file.
*/
class StyleFromAudioView final : public juce::Component
{
public:
    explicit StyleFromAudioView (Session&);
    ~StyleFromAudioView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Chooses the audio clip (0: keep the choice). */
    void chooseClip (ClipId);
    /** Starts making the style from the chosen clip. */
    void start();

    /** Opens a saved style in the Loops view (the main window asks to save the song first). */
    std::function<void (const juce::File&)> onOpenStyle;

private:
    class Results;

    void update();
    void fillClips();
    void play (style_from_audio::Listen);
    void save();

    Session& session;
    StyleFromAudioService& service;

    juce::Label title, about, clipLabel, tempoLabel, lengthLabel, status;
    juce::ComboBox clip, length, section;
    juce::TextEditor tempo;
    juce::ToggleButton vocals { "Vocal line as Phrase 1" };
    juce::TextButton makeButton { "Make Style" }, stopButton { "Stop" };
    juce::TextButton original { "Original" }, styleButton { "Style" }, both { "Both" }, quiet { "Stop" };
    juce::TextButton saveButton { "Save Style..." }, openButton { "Open in Loops" };
    std::vector<ClipId> clipIds;
    double progress = 0.0;
    juce::Rectangle<int> progressArea;
    int playing = -1; ///< Listen as int, -1: not playing

    juce::Viewport viewport;
    std::unique_ptr<Results> results;
    std::shared_ptr<const style_from_audio::Made> shown;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace arranger::ui

#pragma once

#include "Theme.h"
#include "../Session.h"
#include "../McpService.h"

#include "arranger/core/Groove.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <vector>

namespace arranger::ui
{

/**
    The AI panel's Groove tool: take the feel from a part, a file or a few
    words, put it on other parts with separate amounts for timing, accents
    and each drum, hear before and after, and apply it as one undo step.
    The match line shows how close the parts are to the groove.
*/
class GrooveView final : public juce::Component
{
public:
    explicit GrooveView (Session&);
    ~GrooveView() override;

    void resized() override;

    /** Takes a groove from words (as the Words source does). */
    void takeFromWords (const juce::String& words);
    /** Chooses a groove by name and the parts to put it on (0: all other tracks). */
    void choose (const juce::String& grooveName, TrackId target);

private:
    void refreshSources();
    void refreshLibrary (const std::string& select = {});
    void take();
    void updateMatch();
    void play (bool after);
    void stopPlaying();
    void apply();
    groove::ApplyOptions options() const;
    std::vector<TrackId> targets() const;
    nlohmann::json applyArgs() const;
    void takeWith (nlohmann::json args);
    const groove::Groove* current() const;
    void setStatus (const juce::String&);

    Session& session;
    std::unique_ptr<SessionTools> tools;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Label title, about, fromLabel, grooveLabel, onLabel, feel, match, status;
    juce::ComboBox source, grooveBox, target;
    juce::TextEditor words;
    juce::TextButton takeButton { "Take" }, beforeButton { "Play Before" }, afterButton { "Play After" }, stopButton { "Stop" }, applyButton { "Apply" };

    struct Amount
    {
        juce::Label label;
        juce::Slider slider;
    };
    Amount timing, accents, kick, snare, hats;

    std::vector<TrackId> sourceTracks; ///< by source item id - 1 (0 for the file and words items)
    std::vector<groove::Groove> library;
    std::vector<TrackId> targetTracks; ///< by target item id - 1 (0 = all other tracks)
    bool playing = false;
};

} // namespace arranger::ui

#pragma once

#include "Theme.h"
#include "../Session.h"

#include "arranger/core/Arranging.h"

#include <memory>
#include <vector>

namespace arranger::ui
{

class ProposalCard;
class TextBlock;

/**
    The AI panel's arranging help: the arrangement doctor (a goal, then small
    explained edits) or the hardware check (does the song fit my synths).
    Each proposal plays against the original (A/B, looping its bars) before
    the user takes one option as one undo step.
*/
class ArrangeView final : public juce::Component
{
public:
    /** `tool`: "arrangement_doctor" or "hardware_fit". */
    ArrangeView (Session&, const juce::String& tool);
    ~ArrangeView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Looks at the song again and shows the proposals. */
    void check();
    /** Chooses a goal by id (stand_out, clear_melody...) and bars (0: all), then checks. */
    void choose (const juce::String& goalId, int fromBar, int toBar);

    static juce::String titleFor (const juce::String& tool);

private:
    void layoutList();
    void play (std::size_t proposal, int option); ///< option -1: the original
    void stopPlaying();
    void use (std::size_t proposal, int option);
    void dismiss (std::size_t proposal);
    void saveCapabilities();
    void setStatus (const juce::String&);
    bool doctor() const { return tool == "arrangement_doctor"; }

    Session& session;
    juce::String tool;

    juce::Label title, about, goalLabel, barsLabel, toLabel, melodyLabel, status;
    juce::ComboBox goal, melody;
    juce::TextEditor fromBar, toBar;
    juce::TextButton checkButton { "Check" }, stopButton { "Stop" };
    std::vector<TrackId> melodyIds; ///< by combo item id - 1

    struct DeviceRow
    {
        std::string profileId;
        juce::Label name, voicesLabel, partsLabel, rangeLabel;
        juce::TextEditor voices, parts, lowest, highest;
    };
    std::vector<std::unique_ptr<DeviceRow>> devices;
    juce::TextButton saveButton { "Save Numbers" };

    juce::Viewport viewport;
    juce::Component list;
    std::unique_ptr<TextBlock> findings;
    std::vector<arranging::Proposal> proposals;
    std::vector<std::unique_ptr<ProposalCard>> cards;
    int playingProposal = -1, playingOption = -2;
};

} // namespace arranger::ui

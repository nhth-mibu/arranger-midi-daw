#pragma once

#include "Theme.h"
#include "../Session.h"

#include "arranger/core/ChordInput.h"

#include <memory>
#include <vector>

namespace arranger::ui
{

class ProposalCard;

/**
    The AI panel's smart chord input: type chords (names or roman numerals)
    with the next chord to try, sixteen chord pads for the key (also on the
    Launchkey pads), chords played on the keyboard to the chord lane, and
    other chords for a bar (reharmonize) to compare by ear before taking one.
*/
class ChordsView final : public juce::Component, private juce::ChangeListener
{
public:
    explicit ChordsView (Session&);
    ~ChordsView() override;

    /** Shows other chords for the lane chord at this time. */
    void reharmonizeAt (Tick);
    /** Puts text in the typing box and the bar to write it from. */
    void typeAt (int bar, const juce::String& text = {});

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Pads;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshLabels();
    void refreshSuggestions();
    void writeTyped();
    void readVoicing();
    void layoutList();
    void play (int option);
    void use (int option);
    void stopPlaying();
    void setStatus (const juce::String&);

    Session& session;

    juce::Label title, keyLabel, nowLabel, typeHeading, fromLabel, nextLabel, padHeading, invLabel, voicingLabel, octaveLabel,
        keysHeading, gridLabel, zoneLabel, keysHint, reharmHeading, reharmLabel, status;
    juce::TextEditor typed, fromBar;
    juce::TextButton writeButton { "Write" }, reharmButton { "Reharmonize" }, stopButton { "Stop" };
    std::vector<std::unique_ptr<juce::TextButton>> suggestionButtons;
    std::vector<chord_input::Suggestion> suggestions;
    std::unique_ptr<Pads> pads;
    juce::ComboBox inversion, voicing, octave, grid, zone;
    juce::ToggleButton padsOnController { "Controller pads play these chords" }, fromKeys { "Write the chords I play" };

    juce::Viewport viewport;
    juce::Component list;
    std::unique_ptr<ProposalCard> card;
    arranging::Proposal proposal;
    Tick reharmTick = -1;
    int playingOption = -2;
    juce::String lastLabels;
};

} // namespace arranger::ui

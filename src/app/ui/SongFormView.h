#pragma once

#include "Theme.h"
#include "../Session.h"

#include "arranger/core/SongForm.h"

#include <memory>
#include <vector>

namespace arranger::ui
{

class TextBlock;

/**
    The AI panel's Song Forms: other versions of the song (a short version,
    a long intro, a gig version, an instrumental break) made from its
    sections. A form is kept in the song without a copy of the Timeline.
    Each join between parts gets a transition (a fill, a lead-in, a held
    chord, a break) that the user can change and hear, looping two bars on
    each side. "Use" writes the form to the Timeline as one undo step.
*/
class SongFormView final : public juce::Component, private juce::Timer
{
public:
    explicit SongFormView (Session&);
    ~SongFormView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Makes a form from a preset id (short, long_intro, gig, instrumental_break) and shows it. */
    void makePreset (const juce::String& presetId);
    /** Plays a join of a form (from 1), or the whole form with join 0. */
    void play (int form, int join);

private:
    class FormCard;

    void timerCallback() override;
    void refresh();
    void layoutList();
    void saveSections();
    void findAgain();
    void makeFromBox();
    void stopPlaying();
    void use (int form);
    void remove (int form);
    void setTransition (int form, int join, Transition);
    void setEnding (int form, Transition);
    void setStatus (const juce::String&);
    std::vector<SongSection> sections() const;

    Session& session;
    std::uint64_t shownRevision = 0;

    juce::Label title, about, sectionsLabel, makeLabel, status;
    juce::TextEditor sectionText, orderText;
    juce::TextButton saveButton { "Save Sections" }, findButton { "Find Again" }, makeButton { "Make" }, stopButton { "Stop" };
    juce::ComboBox preset;

    juce::Viewport viewport;
    juce::Component list;
    std::vector<std::unique_ptr<FormCard>> cards;
    int playingForm = -1, playingJoin = -1;
};

} // namespace arranger::ui

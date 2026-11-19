#pragma once

#include "Theme.h"
#include "../McpService.h"
#include "../Session.h"

#include "arranger/core/SongForm.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <vector>

namespace arranger::ui
{

/**
    The AI panel's Song Forms tool: the song's sections, other arrangements of
    them (a short version, a long intro), and the transition at each new join
    (fill, pickup, hold, break) with a reason. Play a form or one join without
    changing the Timeline; Use This Form changes it (one undo step).
*/
class SongFormsView final : public juce::Component, private juce::ChangeListener
{
public:
    explicit SongFormsView (Session&);
    ~SongFormsView() override;

    void resized() override;

    /** Makes (or changes) a form from section names, as Save Form does. */
    void makeForm (const juce::String& name, const juce::String& parts);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void refreshJoins();
    void layoutList();
    void findSections();
    void saveSections();
    void saveForm();
    void selectForm();
    void playForm (int join); ///< join from 1, 0: the whole form
    void stopPlaying();
    void useForm();
    void setTransition (int join, Transition::Kind);
    void setStatus (const juce::String&);
    juce::String call (const std::string& tool, const nlohmann::json& args);
    const SongForm* form() const;

    Session& session;
    std::unique_ptr<SessionTools> tools;
    juce::Label title, about, sectionsLabel, formLabel, nameLabel, partsLabel, status;
    juce::TextEditor sections, name, parts;
    juce::ComboBox forms;
    juce::TextButton findButton { "Find Sections" }, setButton { "Set Sections" }, newButton { "New" }, saveButton { "Save Form" },
        playButton { "Play Form" }, stopButton { "Stop" }, useButton { "Use This Form" };

    struct JoinRow
    {
        juce::Label heading, because;
        juce::ComboBox kind;
        juce::TextButton play { "Play" };
    };
    std::vector<std::unique_ptr<JoinRow>> joins;
    juce::Viewport viewport;
    juce::Component list;
    std::string formName; ///< the form shown
    bool playing = false, updating = false;
    std::uint64_t seenRevision = 0;
};

} // namespace arranger::ui

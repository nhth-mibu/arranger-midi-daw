#pragma once

#include "Theme.h"
#include "../Session.h"

#include <functional>
#include <optional>
#include <memory>
#include <vector>

namespace arranger::ui
{

/** A small accent button for the map's main action. */
class AccentButton final : public juce::Button
{
public:
    explicit AccentButton (const juce::String& text) : juce::Button (text) {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

/**
    The MIDI Map view: one track per row, the app's controls as columns, and a
    cell for the controller knob, button or pad that drives each pair.

    The controller page is chosen at the top. A cell opens a menu of the page's
    controls. The map is stored in the app settings, so it survives a restart.
*/
class MidiMapView final : public juce::Component,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    explicit MidiMapView (Session&);
    ~MidiMapView() override;

    /** The edited page, so the host can save it as a user controller page. */
    std::function<void (const ControllerProfile&)> onProfileChanged;
    /** The prompt for the AI panel. */
    std::function<void (const juce::String&)> onAskAi;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Control
    {
        bool isNote = false;
        int channel = 0;
        int number = 0;
        int strip = 0;
        int row = 0;
        ControlAction action = ControlAction::none;
        juce::String mode;
        juce::String shortName;
        juce::String fullName;
    };

    class MatrixPanel;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    const ControllerProfile* selectedProfile() const;
    std::vector<Control> controlsFor (const ControllerProfile&) const;
    std::optional<Control> cellControl (int track, ControlAction, int row) const;
    void assign (int track, ControlAction, int row, const Control&);
    void clearCell (int track, ControlAction, int row);
    void showControlMenu (int track, ControlAction, int row);
    void startLearn (int track, ControlAction, int row);
    void stopLearn (const juce::String& message);
    void drawControllerCard (juce::Graphics&, juce::Rectangle<int>);
    void drawChannelAdvice (juce::Graphics&, juce::Rectangle<int>);
    void askAi();
    juce::String trackOutputText (const Track&) const;

    Session& session;
    std::vector<ControllerProfile> profiles;
    juce::String selectedId;
    juce::ComboBox controllerBox;
    juce::Label bankLabel;
    juce::TextButton bankPrev { "<" }, bankNext { ">" };
    AccentButton askButton { "Ask AI" };
    juce::Viewport matrixViewport;
    std::unique_ptr<MatrixPanel> matrix;
    juce::String shownName;
    juce::Rectangle<int> adviceArea;
    bool updating = false;
    bool learning = false;
    int learnTrack = -1;
    ControlAction learnAction = ControlAction::none;
    int learnRow = 0;
    double learnStartedMs = 0.0;
    juce::String learnMessage;
};

} // namespace arranger::ui

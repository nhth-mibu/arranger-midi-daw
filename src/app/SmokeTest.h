#pragma once

#include "ui/MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace arranger
{

/**
    The UI smoke test (--smoke-test): with the main window open on a song, it
    runs every menu command once in each view, closes the dialogs they open,
    then starts playback and an analysis and quits while they run. It prints
    each command before it runs, so a crash log shows the last one.

    Commands that go to the internet, open another program or scan the
    computer's plugin folders are left out (they are listed when it starts).
    Run it on Linux under Xvfb: native file dialogs on macOS and Windows would
    wait for a click.
*/
class SmokeTest final : private juce::Timer
{
public:
    SmokeTest (ui::MainComponent&, std::function<void (int exitCode)> done);
    ~SmokeTest() override;

    /** Commands the smoke test does not run, and why. */
    static juce::String skipReason (juce::CommandID);

private:
    void timerCallback() override;
    void closeDialogs();
    void step();

    ui::MainComponent& main;
    std::function<void (int)> finished;
    juce::Array<juce::CommandID> commands;
    int view = 0, index = -1, phase = 0, ran = 0;
};

} // namespace arranger

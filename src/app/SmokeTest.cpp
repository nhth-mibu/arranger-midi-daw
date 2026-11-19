#include "SmokeTest.h"

#include <iostream>

namespace arranger
{

namespace
{
    using C = ui::MainComponent;

    /** The views each pass runs the commands in: what a command does depends on the view. */
    constexpr int kViews[] = { C::viewTimeline, C::viewScore, C::viewBeat, C::viewClips, C::viewMap };
}

SmokeTest::SmokeTest (ui::MainComponent& m, std::function<void (int)> done) : main (m), finished (std::move (done))
{
    main.getAllCommands (commands);
    for (const auto id : commands)
        if (const auto why = skipReason (id); why.isNotEmpty())
            std::cout << "smoke test: skips " << main.commandManager().getNameOfCommand (id) << " (" << why << ")" << std::endl;
    startTimer (50);
}

SmokeTest::~SmokeTest() { stopTimer(); }

juce::String SmokeTest::skipReason (juce::CommandID id)
{
    switch (id)
    {
        case C::cmdCheckForUpdates:
        case C::cmdDownloadSoundFont: return "goes to the internet";
        case C::cmdScanPlugins: return "scans the computer's plugin folders";
        case C::cmdDeviceFolder: return "opens the file manager";
        case C::cmdRecord: return "needs an audio input"; // and would record until stopped
        default: return {};
    }
}

void SmokeTest::closeDialogs()
{
    // File choosers, message boxes and other modal dialogs: Cancel.
    if (auto* modal = juce::ModalComponentManager::getInstance())
        modal->cancelAllModalComponents();
}

void SmokeTest::timerCallback()
{
    closeDialogs();
    step();
}

void SmokeTest::step()
{
    if (phase == 0)
    {
        // Every command, in every view.
        if (++index >= commands.size())
        {
            index = 0;
            if (++view >= static_cast<int> (std::size (kViews)))
            {
                phase = 1;
                return;
            }
        }
        if (index == 0)
        {
            std::cout << "smoke test: view " << kViews[view] << std::endl;
            main.showView (kViews[view]);
        }
        const auto id = commands[index];
        if (skipReason (id).isNotEmpty())
            return;
        std::cout << "smoke test: " << main.commandManager().getNameOfCommand (id) << std::endl;
        main.commandManager().invokeDirectly (id, false);
        ++ran;
        return;
    }
    if (phase == 1)
    {
        // Busy: playing, with an analysis window and the AI chat open.
        std::cout << "smoke test: quit while playing" << std::endl;
        main.showView (C::viewTimeline);
        main.getSession().locate (0);
        if (! main.getSession().isPlaying())
            main.getSession().togglePlay();
        main.commandManager().invokeDirectly (C::cmdAnalyseAudio, false);
        main.commandManager().invokeDirectly (C::cmdAiChat, false);
        phase = 2;
        return;
    }
    if (phase == 2)
    {
        stopTimer();
        std::cout << "smoke test: ran " << ran << " commands" << std::endl;
        closeDialogs();
        finished (0);
    }
}

} // namespace arranger

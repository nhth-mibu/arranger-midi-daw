// Catch2's main for the plugin tests. JUCE needs its message thread to be the
// main thread, and its GUI singletons cleaned up at exit.
// It is also the plugin scanner's child process, as the app is (PluginHost::scanFile).
#include "plugins/PluginHost.h"

#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI init;
    if (argc >= 5 && juce::String (argv[1]) == "--scan-plugin")
        return arranger::PluginHost::runScanChild ({ argv[2], argv[3], argv[4] });
    return Catch::Session().run (argc, argv);
}

// Catch2's main for the plugin tests. JUCE needs its message thread to be the
// main thread, and its GUI singletons cleaned up at exit.
#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI init;
    return Catch::Session().run (argc, argv);
}

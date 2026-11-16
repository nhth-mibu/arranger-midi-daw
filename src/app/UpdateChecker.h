#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <optional>

namespace arranger::update
{

struct Release
{
    juce::String version; ///< The tag, for example "v0.2.0".
    juce::String url;     ///< The release page on GitHub.
};

/** The version that this build reports in About and the update check. */
juce::String currentVersion();

/**
    Reads the latest public release on a background thread with curl.

    The result comes on the message thread. `error` is empty on success.
    Destroy the object to cancel a check in progress.
*/
class Checker
{
public:
    using Result = std::function<void (std::optional<Release>, juce::String error)>;

    Checker();
    ~Checker();

    /** Starts one check. A second call while a check runs does nothing. */
    void check (Result onDone);

private:
    struct Worker;
    std::unique_ptr<Worker> worker;
};

} // namespace arranger::update

#include "UpdateChecker.h"

#include <juce_events/juce_events.h>

namespace arranger::update
{
namespace
{
    constexpr const char* kLatestRelease =
        "https://api.github.com/repos/nhth-mibu/lazy-arranger-midi-daw-releases/releases/latest";

    juce::String curlProgram()
    {
#if JUCE_WINDOWS
        return "curl.exe";
#else
        return juce::File ("/usr/bin/curl").existsAsFile() ? "/usr/bin/curl" : "curl";
#endif
    }

    std::optional<Release> parseRelease (const juce::String& json, juce::String& error)
    {
        const auto value = juce::JSON::parse (json);
        const auto* object = value.getDynamicObject();
        if (object == nullptr)
        {
            error = "The update server sent no release.";
            return {};
        }

        const auto version = object->getProperty ("tag_name").toString().trim();
        const auto url = object->getProperty ("html_url").toString().trim();
        if (version.isEmpty() || ! url.startsWith ("https://"))
        {
            error = "The update server sent no release.";
            return {};
        }

        return Release { version, url };
    }
} // namespace

juce::String currentVersion()
{
    return JUCE_APPLICATION_VERSION_STRING;
}

struct Checker::Worker final : juce::Thread
{
    explicit Worker (Result onDone)
        : juce::Thread ("Update check"), done (std::move (onDone))
    {
    }

    ~Worker() override
    {
        stopThread (5000);
    }

    void run() override
    {
        juce::String error;
        std::optional<Release> release;
        juce::ChildProcess process;
        const juce::StringArray args {
            curlProgram(), "--fail", "--location", "--silent", "--show-error",
            "--connect-timeout", "5", "--max-time", "10",
            "-H", "Accept: application/vnd.github+json",
            "-H", "User-Agent: Lazy-Arranger-Midi-DAW",
            kLatestRelease
        };

        if (! process.start (args, juce::ChildProcess::wantStdOut))
        {
            error = "The curl program was not found.";
        }
        else
        {
            while (process.isRunning())
            {
                if (threadShouldExit())
                {
                    process.kill();
                    return;
                }
                wait (100);
            }

            if (process.getExitCode() != 0)
                error = "No connection to the update server.";
            else
                release = parseRelease (process.readAllProcessOutput(), error);
        }

        juce::MessageManager::callAsync ([done = done, release, error] { done (release, error); });
    }

    Result done;
};

Checker::Checker() = default;
Checker::~Checker() = default;

void Checker::check (Result onDone)
{
    if (worker != nullptr && worker->isThreadRunning())
        return;

    worker = std::make_unique<Worker> (std::move (onDone));
    worker->startThread();
}

} // namespace arranger::update

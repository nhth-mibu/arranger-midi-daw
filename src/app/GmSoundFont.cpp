#include "GmSoundFont.h"

#include <juce_events/juce_events.h>

#include <cstring>

namespace arranger::gmsoundfont
{

namespace
{
    // GeneralUser GS by S. Christian Collins. Its licence allows use and
    // redistribution in software. See https://schristiancollins.com/generaluser
    const char* const url = "https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/main/GeneralUser-GS.sf2";
    constexpr juce::int64 minimumSize = 10 * 1024 * 1024;
} // namespace

juce::File downloadedFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Arranger MIDI DAW")
        .getChildFile ("SoundFonts")
        .getChildFile ("GeneralUser-GS.sf2");
}

bool looksLikeSoundFont (const juce::File& f)
{
    juce::FileInputStream in (f);
    if (! in.openedOk())
        return false;
    char head[12] = {};
    if (in.read (head, 12) != 12)
        return false;
    return std::memcmp (head, "RIFF", 4) == 0 && std::memcmp (head + 8, "sfbk", 4) == 0;
}

juce::File findInstalled()
{
    if (const auto f = downloadedFile(); f.existsAsFile() && looksLikeSoundFont (f))
        return f;

#if JUCE_LINUX
    // Packages: fluid-soundfont-gm, timgm6mb-soundfont, musescore-general-soundfont.
    for (const auto* path : { "/usr/share/sounds/sf2/default-GM.sf2", "/usr/share/sounds/sf2/FluidR3_GM.sf2",
                              "/usr/share/soundfonts/default.sf2", "/usr/share/soundfonts/FluidR3_GM.sf2",
                              "/usr/share/sounds/sf2/TimGM6mb.sf2", "/usr/share/sounds/sf2/MuseScore_General.sf2" })
        if (const juce::File f (path); f.existsAsFile() && looksLikeSoundFont (f))
            return f;
#endif
    return {};
}

struct Download::Worker final : juce::Thread
{
    explicit Worker (Done d) : juce::Thread ("SoundFont download"), done (std::move (d)) {}
    ~Worker() override { stopThread (5000); }

    void run() override
    {
        const auto target = downloadedFile();
        target.getParentDirectory().createDirectory();
        const auto part = target.getSiblingFile (target.getFileName() + ".part");
        part.deleteFile();

        juce::String error;
        juce::ChildProcess curl;
        const juce::StringArray args { "curl", "--fail", "--location", "--silent", "--show-error",
                                       "--retry", "2", "--output", part.getFullPathName(), url };
        if (! curl.start (args, juce::ChildProcess::wantStdErr))
        {
            error = "The curl program was not found. Download " + juce::String (name) + " in a web browser, then use File > Choose SoundFont.";
        }
        else
        {
            while (curl.isRunning())
            {
                if (threadShouldExit())
                {
                    curl.kill();
                    part.deleteFile();
                    return;
                }
                wait (100);
            }
            const auto output = curl.readAllProcessOutput().trim();
            if (curl.getExitCode() != 0)
                error = "The download failed. " + output;
            else if (part.getSize() < minimumSize || ! looksLikeSoundFont (part))
                error = "The downloaded file is not a SoundFont.";
            else if (! part.moveFileTo (target))
                error = "Could not save " + target.getFullPathName();
        }
        if (error.isNotEmpty())
            part.deleteFile();

        const auto result = error.isEmpty() ? target : juce::File();
        juce::MessageManager::callAsync ([d = done, result, error] { d (result, error); });
    }

    Done done;
};

Download::Download (Done onDone) : worker (std::make_unique<Worker> (std::move (onDone)))
{
    worker->startThread();
}

Download::~Download() = default;

} // namespace arranger::gmsoundfont

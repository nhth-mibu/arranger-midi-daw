#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>

namespace arranger
{

/**
    Finds or downloads a General MIDI SoundFont for the built-in player.

    macOS and Windows ship a GM sound set only as DLS, which the player
    cannot read. Some Linux systems have an SF2 in /usr/share. So on first run
    the app looks for an SF2 on the computer, and if there is none it offers to
    download GeneralUser GS (free to use in software, about 32 MB).
*/
namespace gmsoundfont
{
    /** Display name of the sound set the app downloads. */
    inline constexpr const char* name = "GeneralUser GS";

    /** Where the app keeps the downloaded SF2. */
    juce::File downloadedFile();

    /** The downloaded SF2 if it is there, else a GM SF2 the system has
        installed (Linux packages), else an empty File. */
    juce::File findInstalled();

    /** True if the file starts like an SF2 (RIFF ... sfbk). */
    bool looksLikeSoundFont (const juce::File&);

    /**
        Downloads the SF2 on a background thread with the system's curl
        (macOS, Windows 10 and later, and nearly all Linux systems have it).
        onDone runs on the message thread with the file, or an empty File and
        a reason. Destroy the object to cancel.
    */
    class Download
    {
    public:
        using Done = std::function<void (const juce::File&, const juce::String& error)>;
        explicit Download (Done onDone);
        ~Download();

    private:
        struct Worker;
        std::unique_ptr<Worker> worker;
    };
} // namespace gmsoundfont

} // namespace arranger

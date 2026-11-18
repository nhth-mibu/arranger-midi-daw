#pragma once

#include <optional>
#include <string>
#include <vector>

/**
    Audio from a link (YouTube and the other sites yt-dlp knows), for Style
    from Audio. The app does not download by itself: it runs yt-dlp, which the
    user installs, with ffmpeg to turn the sound into a WAV file. This part
    only checks links, makes yt-dlp's arguments and reads what it prints, so
    it can be tested without the programs.

    For personal use only: the user must respect the site's terms.
*/
namespace arranger::link_audio
{

/** True for an http(s) link with a host and no spaces. */
bool isLink (const std::string&);

/** The link without spaces around it, and with "https://" added when it starts with "www." or "youtu". Empty when it is no link. */
std::string clean (const std::string&);

/**
    The arguments for yt-dlp (after the program): the best audio of one video
    (no playlist), as a WAV file in `folder` named after the title, and the
    file's path printed at the end as "FILE <path>". `ffmpegFolder` (may be
    empty) is where ffmpeg is when it is not on the PATH.
*/
std::vector<std::string> arguments (const std::string& link, const std::string& folder, const std::string& ffmpegFolder = {});

/** The download progress (0..1) in a line of yt-dlp ("[download]  42.3% of ..."), or empty. */
std::optional<double> progress (const std::string& line);

/** The file in a "FILE <path>" line, or empty. */
std::string fileFrom (const std::string& line);

/** A short reason from yt-dlp's error lines ("ERROR: ... Video unavailable"), or empty. */
std::string errorFrom (const std::string& line);

/** "mac", "windows" or "linux". */
enum class Platform
{
    mac,
    windows,
    linux
};

/** What to type to install yt-dlp and ffmpeg on this platform, as lines for the user. */
std::vector<std::string> installSteps (Platform);

/** The names of the programs on this platform ("yt-dlp" or "yt-dlp.exe"). */
std::string programName (const std::string& base, Platform);

/** The folders where these programs usually are, besides the PATH (Homebrew, pipx, winget...). `home` is the user's home folder. */
std::vector<std::string> usualFolders (Platform, const std::string& home);

inline constexpr const char* kNotice = "For personal use only. Respect the terms of the site and the rights of the music.";

} // namespace arranger::link_audio

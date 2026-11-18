#include "arranger/core/LinkAudio.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace arranger::link_audio
{

namespace
{
    std::string trim (const std::string& s)
    {
        const auto first = s.find_first_not_of (" \t\r\n");
        if (first == std::string::npos)
            return {};
        const auto last = s.find_last_not_of (" \t\r\n");
        return s.substr (first, last - first + 1);
    }

    bool startsWith (const std::string& s, const std::string& prefix) { return s.rfind (prefix, 0) == 0; }

    std::string lower (std::string s)
    {
        std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return s;
    }
}

bool isLink (const std::string& text)
{
    const auto s = trim (text);
    const auto l = lower (s);
    std::string rest;
    if (startsWith (l, "https://"))
        rest = s.substr (8);
    else if (startsWith (l, "http://"))
        rest = s.substr (7);
    else
        return false;
    if (rest.empty() || std::any_of (s.begin(), s.end(), [] (unsigned char c) { return std::isspace (c) || c < 32; }))
        return false;
    const auto host = rest.substr (0, rest.find_first_of ("/?#"));
    return host.find ('.') != std::string::npos && host.front() != '.' && host.back() != '.' && host.front() != '-';
}

std::string clean (const std::string& text)
{
    auto s = trim (text);
    const auto l = lower (s);
    if (startsWith (l, "www.") || startsWith (l, "youtu") || startsWith (l, "m.youtube"))
        s = "https://" + s;
    return isLink (s) ? s : std::string();
}

std::vector<std::string> arguments (const std::string& link, const std::string& folder, const std::string& ffmpegFolder)
{
    std::vector<std::string> a {
        "--no-playlist",
        "--newline",
        "--no-colors",
        "-f", "bestaudio/best",
        "-x", "--audio-format", "wav",
        "--restrict-filenames",
        "-o", folder + "/%(title).80s [%(id)s].%(ext)s",
        "--print", "after_move:FILE %(filepath)s",
        "--progress",
    };
    if (! ffmpegFolder.empty())
    {
        a.push_back ("--ffmpeg-location");
        a.push_back (ffmpegFolder);
    }
    a.push_back ("--");
    a.push_back (link);
    return a;
}

std::optional<double> progress (const std::string& line)
{
    const auto at = line.find ("[download]");
    if (at == std::string::npos)
        return std::nullopt;
    const auto percent = line.find ('%', at);
    if (percent == std::string::npos)
        return std::nullopt;
    auto start = percent;
    while (start > at && (std::isdigit (static_cast<unsigned char> (line[start - 1])) || line[start - 1] == '.'))
        --start;
    if (start == percent)
        return std::nullopt;
    const double v = std::strtod (line.substr (start, percent - start).c_str(), nullptr);
    return std::clamp (v / 100.0, 0.0, 1.0);
}

std::string fileFrom (const std::string& line)
{
    const auto s = trim (line);
    return startsWith (s, "FILE ") ? trim (s.substr (5)) : std::string();
}

std::string errorFrom (const std::string& line)
{
    const auto s = trim (line);
    if (! startsWith (s, "ERROR:"))
        return {};
    auto text = trim (s.substr (6));
    // "[youtube] abc123: Video unavailable" -> "Video unavailable"
    if (startsWith (text, "["))
        if (const auto colon = text.find (": "); colon != std::string::npos)
            text = trim (text.substr (colon + 2));
    return text;
}

std::vector<std::string> installSteps (Platform p)
{
    switch (p)
    {
        case Platform::mac:
            return { "Install Homebrew from brew.sh if you do not have it.", "In Terminal, type:  brew install yt-dlp ffmpeg" };
        case Platform::windows:
            return { "In PowerShell, type:  winget install yt-dlp.yt-dlp Gyan.FFmpeg", "Then start the app again." };
        case Platform::linux:
        default:
            return { "In a terminal, type:  sudo apt install ffmpeg pipx && pipx install yt-dlp",
                     "(The yt-dlp of apt is often too old for YouTube.)" };
    }
}

std::string programName (const std::string& base, Platform p)
{
    return p == Platform::windows ? base + ".exe" : base;
}

std::vector<std::string> usualFolders (Platform p, const std::string& home)
{
    switch (p)
    {
        case Platform::mac:
            return { "/opt/homebrew/bin", "/usr/local/bin", home + "/.local/bin", "/opt/local/bin" };
        case Platform::windows:
            return { home + "\\AppData\\Local\\Microsoft\\WinGet\\Links", home + "\\scoop\\shims", "C:\\ProgramData\\chocolatey\\bin", home + "\\.local\\bin" };
        case Platform::linux:
        default:
            return { home + "/.local/bin", "/usr/local/bin", "/usr/bin", "/snap/bin" };
    }
}

} // namespace arranger::link_audio

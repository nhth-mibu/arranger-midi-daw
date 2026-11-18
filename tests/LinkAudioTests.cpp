#include "arranger/core/LinkAudio.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace arranger;

TEST_CASE ("Links for Style from Audio", "[linkaudio]")
{
    CHECK (link_audio::isLink ("https://www.youtube.com/watch?v=abc123"));
    CHECK (link_audio::isLink ("http://youtu.be/abc123"));
    CHECK_FALSE (link_audio::isLink ("youtube.com/watch?v=abc"));
    CHECK_FALSE (link_audio::isLink ("https://"));
    CHECK_FALSE (link_audio::isLink ("https://localhost/x"));
    CHECK_FALSE (link_audio::isLink ("https://www.you tube.com"));
    CHECK_FALSE (link_audio::isLink ("file:///etc/passwd"));
    CHECK_FALSE (link_audio::isLink ("--exec rm"));

    CHECK (link_audio::clean ("  www.youtube.com/watch?v=x \n") == "https://www.youtube.com/watch?v=x");
    CHECK (link_audio::clean ("youtu.be/x") == "https://youtu.be/x");
    CHECK (link_audio::clean ("not a link").empty());
}

TEST_CASE ("yt-dlp arguments and output", "[linkaudio]")
{
    const auto a = link_audio::arguments ("https://youtu.be/x", "/music/dl", "/opt/homebrew/bin");
    REQUIRE (a.size() > 4);
    CHECK (a.back() == "https://youtu.be/x");
    CHECK (a[a.size() - 2] == "--"); // the link can never be read as an option
    CHECK (std::find (a.begin(), a.end(), "--no-playlist") != a.end());
    CHECK (std::find (a.begin(), a.end(), "wav") != a.end());
    CHECK (std::find (a.begin(), a.end(), "/opt/homebrew/bin") != a.end());
    CHECK (std::find (a.begin(), a.end(), "--ffmpeg-location") == a.end() - 4);
    const auto plain = link_audio::arguments ("https://youtu.be/x", "/music/dl");
    CHECK (std::find (plain.begin(), plain.end(), "--ffmpeg-location") == plain.end());

    CHECK (link_audio::progress ("[download]  42.3% of    3.51MiB at  1.2MiB/s ETA 00:02").value() == Catch::Approx (0.423));
    CHECK (link_audio::progress ("[download] 100% of 3.51MiB").value() == Catch::Approx (1.0));
    CHECK_FALSE (link_audio::progress ("[ExtractAudio] Destination: x.wav"));
    CHECK_FALSE (link_audio::progress ("[download] Destination: x.webm"));

    CHECK (link_audio::fileFrom ("FILE /music/dl/Song [x].wav") == "/music/dl/Song [x].wav");
    CHECK (link_audio::fileFrom ("[download] x").empty());
    CHECK (link_audio::errorFrom ("ERROR: [youtube] abc: Video unavailable") == "Video unavailable");
    CHECK (link_audio::errorFrom ("WARNING: x").empty());
}

TEST_CASE ("yt-dlp install help", "[linkaudio]")
{
    using P = link_audio::Platform;
    CHECK (link_audio::installSteps (P::mac).back().find ("brew install yt-dlp ffmpeg") != std::string::npos);
    CHECK (link_audio::installSteps (P::windows).front().find ("winget install") != std::string::npos);
    CHECK (link_audio::installSteps (P::linux).front().find ("pipx install yt-dlp") != std::string::npos);
    CHECK (link_audio::programName ("yt-dlp", P::windows) == "yt-dlp.exe");
    CHECK (link_audio::usualFolders (P::mac, "/Users/a").front() == "/opt/homebrew/bin");
}

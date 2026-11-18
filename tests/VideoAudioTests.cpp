#include "AudioFiles.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace arranger;

namespace
{
    juce::File fixture (const char* name)
    {
        return juce::File (ARRANGER_MEDIA_FIXTURES).getChildFile (name);
    }

    bool decoderAvailable()
    {
        juce::ChildProcess process;
        return process.start (juce::StringArray { "ffmpeg", "-version" }, 0)
            && process.waitForProcessToFinish (10000) && process.getExitCode() == 0;
    }
}

TEST_CASE ("Audio import accepts common video file extensions", "[video]")
{
    for (const auto* name : { "clip.mp4", "clip.MOV", "clip.mkv", "clip.webm", "clip.avi", "clip.wmv", "clip.m2ts" })
    {
        const auto file = fixture (name);
        CHECK (audio_files::isAudioFile (file));
        CHECK (audio_files::isVideoFile (file));
    }
    CHECK_FALSE (audio_files::isAudioFile (fixture ("project.arranger")));
    CHECK (audio_files::wildcard().contains ("*.mp4"));
    CHECK (audio_files::wildcard().contains ("*.mkv"));
}

TEST_CASE ("Video audio import decodes the first audio stream", "[video]")
{
    if (! decoderAvailable())
        SKIP ("Install FFmpeg on PATH to run video decoding integration tests");
    const auto* name = GENERATE ("tone.mp4", "tone.mkv", "tone.webm");
    const auto sample = audio_files::read (fixture (name).getFullPathName().toStdString());
    CHECK (sample.sampleRate == 48000.0);
    REQUIRE (sample.channels.size() == 2);
    CHECK (sample.seconds() > 0.2);
    CHECK (sample.seconds() < 0.4);
    CHECK (*std::max_element (sample.channels.front().begin(), sample.channels.front().end()) > 0.05f);
    int rising = 0;
    const auto& signal = sample.channels.front();
    for (std::size_t i = 1; i < signal.size(); ++i)
        if (signal[i - 1] <= 0.0f && signal[i] > 0.0f)
            ++rising;
    CHECK (rising > 80);
    CHECK (rising < 140); // first stream is 440 Hz; the second is 880 Hz
    CHECK (sample.name == "tone");
}

TEST_CASE ("A video without audio fails with a useful error", "[video]")
{
    if (! decoderAvailable())
        SKIP ("Install FFmpeg on PATH to run video decoding integration tests");
    CHECK_THROWS_WITH (audio_files::read (fixture ("silent-video.mp4").getFullPathName().toStdString()),
                       Catch::Matchers::ContainsSubstring ("no readable audio stream"));
}

TEST_CASE ("Video import handles Unicode and spaces in paths", "[video]")
{
    if (! decoderAvailable())
        SKIP ("Install FFmpeg on PATH to run video decoding integration tests");
    juce::TemporaryFile temp (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getNonexistentChildFile (juce::String::fromUTF8 ("Video æøå test"), ".mp4"));
    REQUIRE (fixture ("tone.mp4").copyFileTo (temp.getFile()));
    CHECK (audio_files::read (temp.getFile().getFullPathName().toStdString()).frames() > 0);
}

TEST_CASE ("A damaged video fails without becoming an audio clip", "[video]")
{
    if (! decoderAvailable())
        SKIP ("Install FFmpeg on PATH to run video decoding integration tests");
    juce::TemporaryFile temp (".mp4");
    REQUIRE (temp.getFile().replaceWithText ("not a video"));
    CHECK_THROWS_WITH (audio_files::read (temp.getFile().getFullPathName().toStdString()),
                       Catch::Matchers::ContainsSubstring ("no readable audio stream"));
}

TEST_CASE ("Video import explains how to provide a missing decoder", "[video]")
{
    // No FFmpeg anywhere: an empty PATH, and ARRANGER_FFMPEG names a missing file (so /opt/homebrew/bin is not searched either).
    struct RestorePath
    {
        std::string value = juce::SystemStats::getEnvironmentVariable ("PATH", "").toStdString();
        ~RestorePath()
        {
#if JUCE_WINDOWS
            _putenv_s ("PATH", value.c_str());
            _putenv_s ("ARRANGER_FFMPEG", "");
#else
            setenv ("PATH", value.c_str(), 1);
            unsetenv ("ARRANGER_FFMPEG");
#endif
        }
    } restore;
#if JUCE_WINDOWS
    _putenv_s ("PATH", "");
    _putenv_s ("ARRANGER_FFMPEG", "C:\\no-ffmpeg-here\\ffmpeg.exe");
#else
    setenv ("PATH", "", 1);
    setenv ("ARRANGER_FFMPEG", "/no-ffmpeg-here/ffmpeg", 1);
#endif
    CHECK_THROWS_WITH (audio_files::read (fixture ("tone.mp4").getFullPathName().toStdString()),
                       Catch::Matchers::ContainsSubstring ("needs FFmpeg"));
}

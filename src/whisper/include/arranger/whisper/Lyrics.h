#pragma once

#include "arranger/core/AudioAnalysis.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

/**
    Lyrics: writes down the words in audio with Whisper (whisper.cpp), with a
    time for each word. Sung words are harder than speech: give it the vocal
    stem when there is one. The words often need a fix after; the AI chat
    can do that with the get_lyrics and set_lyrics tools.
*/
namespace arranger::lyrics
{

struct Settings
{
    std::string model;               ///< a ggml model file, e.g. ggml-large-v3-turbo-q5_0.bin
    std::string language = "auto";   ///< "auto", or a code such as "en", "da", "vi"
    std::string prompt;              ///< words that help, e.g. the song title or known lyrics
    int threads = 0;                 ///< 0: as many as make sense here
};

struct Transcript
{
    std::vector<analysis::Word> words;
    std::string language; ///< found or given
};

struct Error : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/**
    Writes down the words. `progress` gets 0..1 and returns false to stop
    (then the words so far are given). Throws Error when the model can't load.
    Takes some time: call it off the message thread.
*/
Transcript transcribe (const Sample&, const Settings&, const std::function<bool (float)>& progress = {});

} // namespace arranger::lyrics

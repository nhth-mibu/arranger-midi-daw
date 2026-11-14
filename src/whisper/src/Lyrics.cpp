#include "arranger/whisper/Lyrics.h"

// whisper.cpp's headers are not warning-clean with GCC's -Wshadow (a function
// name hides a struct constructor in ggml-backend.h). Keep the strict warnings
// for our own code, but not for the third-party header.
#if defined(__GNUC__) && ! defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <whisper.h>
#if defined(__GNUC__) && ! defined(__clang__)
    #pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cctype>
#include <memory>
#include <thread>

namespace arranger::lyrics
{

namespace
{
    struct Callbacks
    {
        const std::function<bool (float)>* progress = nullptr;
        bool stop = false;
    };

    void quietLog (ggml_log_level, const char*, void*) {}

    std::string trimmed (const std::string& s)
    {
        std::size_t a = 0, b = s.size();
        while (a < b && std::isspace (static_cast<unsigned char> (s[a])))
            ++a;
        while (b > a && std::isspace (static_cast<unsigned char> (s[b - 1])))
            --b;
        return s.substr (a, b - a);
    }

    /** Whisper's notes in brackets ("[Music]", "(singing)") and music signs are no words. */
    bool isWord (const std::string& text)
    {
        if (text.empty())
            return false;
        if ((text.front() == '[' && text.back() == ']') || (text.front() == '(' && text.back() == ')') || (text.front() == '*' && text.back() == '*'))
            return false;
        if (text.find ("\xE2\x99\xAA") != std::string::npos || text.find ("\xE2\x99\xAB") != std::string::npos) // ♪ ♫
            return false;
        return std::any_of (text.begin(), text.end(), [] (char c) { return std::isalnum (static_cast<unsigned char> (c)) || (static_cast<unsigned char> (c) & 0x80) != 0; });
    }
} // namespace

Transcript transcribe (const Sample& sample, const Settings& settings, const std::function<bool (float)>& progress)
{
    whisper_log_set (quietLog, nullptr);

    auto contextParams = whisper_context_default_params();
    contextParams.use_gpu = true;
    std::unique_ptr<whisper_context, decltype (&whisper_free)> ctx (whisper_init_from_file_with_params (settings.model.c_str(), contextParams), &whisper_free);
    if (ctx == nullptr)
        throw Error ("The Whisper model can't be loaded: " + settings.model);

    const auto audio = analysis::resampleMono (sample, WHISPER_SAMPLE_RATE);
    if (audio.empty())
        return {};

    auto params = whisper_full_default_params (WHISPER_SAMPLING_BEAM_SEARCH);
    params.beam_search.beam_size = 5;
    params.n_threads = settings.threads > 0 ? settings.threads : static_cast<int> (std::clamp (std::thread::hardware_concurrency(), 1u, 8u));
    params.print_progress = false;
    params.print_realtime = false;
    params.print_special = false;
    params.print_timestamps = false;
    params.token_timestamps = true; // a time for each word:
    params.max_len = 1;             // one word per segment
    params.split_on_word = true;
    params.suppress_nst = true;
    const bool automatic = settings.language.empty() || settings.language == "auto";
    params.language = automatic ? "auto" : settings.language.c_str();
    params.detect_language = false;
    if (! settings.prompt.empty())
        params.initial_prompt = settings.prompt.c_str();

    Callbacks callbacks { progress ? &progress : nullptr };
    params.progress_callback = [] (whisper_context*, whisper_state*, int percent, void* user) {
        auto* c = static_cast<Callbacks*> (user);
        if (c->progress != nullptr && ! (*c->progress) (static_cast<float> (percent) / 100.0f))
            c->stop = true;
    };
    params.progress_callback_user_data = &callbacks;
    params.abort_callback = [] (void* user) { return static_cast<Callbacks*> (user)->stop; };
    params.abort_callback_user_data = &callbacks;

    const int status = whisper_full (ctx.get(), params, audio.data(), static_cast<int> (audio.size()));
    if (status != 0 && ! callbacks.stop)
        throw Error ("Whisper could not write down the words (error " + std::to_string (status) + ").");

    Transcript out;
    const int lang = whisper_full_lang_id (ctx.get());
    out.language = lang >= 0 ? whisper_lang_str (lang) : (automatic ? std::string() : settings.language);

    const int segments = whisper_full_n_segments (ctx.get());
    for (int i = 0; i < segments; ++i)
    {
        const auto text = trimmed (whisper_full_get_segment_text (ctx.get(), i));
        if (! isWord (text))
            continue;
        float sum = 0.0f;
        int tokens = 0;
        for (int k = 0; k < whisper_full_n_tokens (ctx.get(), i); ++k)
        {
            const auto data = whisper_full_get_token_data (ctx.get(), i, k);
            if (data.id >= whisper_token_eot (ctx.get()))
                continue; // special tokens
            sum += data.p;
            ++tokens;
        }
        analysis::Word w;
        w.start = static_cast<double> (whisper_full_get_segment_t0 (ctx.get(), i)) / 100.0;
        w.end = static_cast<double> (whisper_full_get_segment_t1 (ctx.get(), i)) / 100.0;
        w.text = text;
        w.probability = tokens > 0 ? sum / static_cast<float> (tokens) : 0.0f;
        out.words.push_back (std::move (w));
    }
    return out;
}

} // namespace arranger::lyrics

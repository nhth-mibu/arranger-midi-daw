#pragma once

#include "arranger/core/AudioAnalysis.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <optional>

namespace arranger
{

class Session;

/**
    Audio > Analyse: takes an audio clip apart into stems, notes, chords,
    instrument names and lyrics, and puts them on new tracks.

    Stems, notes and instrument names come from the analysis tools: a Python
    helper (Demucs, Basic Pitch, PANNs) in the app's own folder, which
    installTools() sets up once. Chords are found by the app. Lyrics come
    from Whisper (whisper.cpp, built into the app) with a model file that
    downloadModel() gets once. Everything runs on this computer.

    One analysis runs at a time, on its own thread; the song changes as one
    undo step when it ends. Use it on the message thread.
*/
class AnalysisService
{
public:
    explicit AnalysisService (Session&);
    ~AnalysisService();

    struct Request
    {
        ClipId clip = 0;
        bool stems = true;
        bool notes = true;
        bool chords = true;
        bool instruments = true;
        bool lyrics = true;
        juce::String language = "auto"; ///< for the lyrics: "auto", "en", "da", ...
        juce::String prompt;             ///< for the lyrics: the title or words that help
    };

    struct Status
    {
        enum class State
        {
            idle,
            running,
            done,
            failed
        };
        State state = State::idle;
        ClipId clip = 0;
        double progress = 0.0;  ///< 0..1
        juce::String step;      ///< what it does now
        juce::String error;
        analysis::Applied applied;
        std::vector<std::string> warnings;
        std::vector<std::string> lyricLines; ///< the words as Whisper heard them
        std::string language;
        std::vector<std::string> chords;     ///< the chord names in order
        std::vector<std::pair<std::string, std::string>> instruments; ///< stem, what was heard
    };

    /** Starts. Returns an empty text when it started, or why it can't. */
    juce::String start (const Request&);
    /** Runs the whole analysis now, on this thread (the command line uses it). Returns the error, or an empty text. */
    juce::String runNow (const Request&, std::function<void (double progress, const juce::String& step)> = {});
    void cancel();
    Status status() const;
    bool isRunning() const noexcept;

    /** Called on the message thread when the status changes. */
    std::function<void()> onChange;

    // ---- the tools ----

    /** What is installed. Runs the helper's check (a second or two) the first time and after an install. */
    struct Setup
    {
        bool python = false;     ///< the app's Python environment is there
        bool demucs = false;
        bool basicPitch = false;
        bool panns = false;
        juce::String device;     ///< "mps", "cuda" or "cpu"
        bool whisper = false;    ///< built into this app
        juce::File model;        ///< the Whisper model in use; a missing file: not downloaded
    };
    Setup setup (bool checkAgain = false);

    /** Sets up the Python tools (about 1-2 GB): gets uv if needed, a Python 3.10, the packages, and the models. */
    juce::String installTools (std::function<void (const juce::String& line)> output, std::function<void (bool ok)> done);
    bool isInstalling() const noexcept;

    /** The Whisper models to choose from: file name, what it is. */
    static juce::StringArray modelNames();
    static juce::String modelDescription (const juce::String& name);
    /** The model in use (saved in the settings). */
    static juce::String chosenModel();
    static void chooseModel (const juce::String& name);
    /** Downloads a Whisper model. `progress` gets 0..1. */
    juce::String downloadModel (const juce::String& name, std::function<void (double)> progress, std::function<void (const juce::String& error)> done);

    /** The app's folder for the tools and models. */
    static juce::File folder();

private:
    struct Job;
    struct Installer;
    struct Download;

    std::unique_ptr<Job> prepare (const Request&, juce::String& why);
    void finished (const analysis::Result&, Request, const juce::String& error);
    void changed();

    Session& session;
    std::unique_ptr<Job> job;
    std::unique_ptr<Installer> installer;
    std::unique_ptr<Download> download;
    mutable juce::CriticalSection lock;
    Status current;
    std::optional<Setup> known;
};

} // namespace arranger

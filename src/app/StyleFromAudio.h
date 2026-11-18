#pragma once

#include "arranger/core/StyleFromAudio.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace arranger
{

class Session;

/**
    Audio > Style from Audio: makes a PSR-S970 style from an audio clip.

    It uses what Analyse Audio found in the clip (stems, notes, chords). When
    the clip was not analysed yet, it runs the analysis first (no lyrics).
    Then it makes the style on its own thread and compares it with the
    stems again and again (see style_from_audio::make). The result stays
    here until the next run: play it against the recording (A/B), save it
    as a style file, or open it in the Clip grid.

    Use it on the message thread.
*/
class StyleFromAudioService final : private juce::Timer
{
public:
    explicit StyleFromAudioService (Session&);
    ~StyleFromAudioService() override;

    struct Request
    {
        ClipId clip = 0;
        double bpm = 0.0;   ///< more than 0: the tempo is known
        int mainBars = 0;   ///< 1, 2 or 4; 0: the loop chooses
        bool vocals = false;
        std::string name;   ///< of the style; empty: the file name
    };

    struct Status
    {
        enum class State
        {
            idle,
            analysing,
            making,
            done,
            failed
        };
        State state = State::idle;
        ClipId clip = 0;
        double progress = 0.0; ///< 0..1 of the current state
        juce::String step, error;
        std::shared_ptr<const style_from_audio::Made> made;
        juce::File saved; ///< where the style was saved last
    };

    /** Starts. Returns an empty text when it started, or why it can't. */
    juce::String start (const Request&);
    void cancel();
    Status status() const;
    bool isRunning() const noexcept;

    /** Writes the style file. An empty file: the user's style folder, else Documents. Returns the error, or an empty text. */
    juce::String save (juce::File& file);

    /** Plays the recording, the style, or both, looping bars [fromBar, toBar) (from 0; toBar 0: to the end). */
    juce::String play (style_from_audio::Listen, int fromBar = 0, int toBar = 0);
    void stopPlaying();

    /** Where save() puts a style when no file is given. */
    juce::File defaultFile() const;

    /** Called on the message thread when the status changes. */
    std::function<void()> onChange;

private:
    struct Job;

    void timerCallback() override;
    void make (const analysis::Result&);
    void finished (std::shared_ptr<const style_from_audio::Made>, const juce::String& error);
    void changed();

    Session& session;
    Request request;
    Status current;
    std::unique_ptr<Job> job;
    bool waitingForAnalysis = false;
};

} // namespace arranger

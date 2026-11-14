#pragma once

#include "arranger/core/MidiAi.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace arranger
{

class SessionTools;

/**
    The AI music tools on this computer (see arranger/core/MidiAi.h):

    - Magenta (Groove, Drumify, MelodyRNN, DrumsRNN, ImprovRNN) runs in
      Node.js with Magenta.js. The install uses the Node.js of the computer
      (version 18 or newer) or downloads one into the app's data folder.
    - The Anticipatory Music Transformer runs in the app's Python environment
      (shared with the audio analysis, see PythonTools.h) with PyTorch.

    Everything lives in <app data>/Arranger MIDI DAW/AI. The models are
    downloaded at install, so the tools then work with no internet.
*/
class MidiAiService
{
public:
    MidiAiService();
    ~MidiAiService();

    static juce::File folder();
    static bool magentaInstalled();
    static bool anticipationInstalled();
    /** "Magenta: ready. Anticipatory Music Transformer: not installed." */
    static juce::String statusText();

    /**
        Installs Magenta (Node.js packages and models) and, when `anticipation`
        is true, PyTorch and the Anticipatory Music Transformer (about 2 GB).
        `output` gets each line; `done` gets an empty text or the error. Both
        on the message thread. Returns an error when an install is running.
    */
    juce::String install (bool anticipation, std::function<void (const juce::String&)> output, std::function<void (const juce::String& error)> done);
    bool installing() const noexcept { return installer != nullptr; }

    /**
        Runs a helper on a request and returns its answer (JSON). Blocks: call
        it on a worker thread. Stops when `thread` should exit. `progress`
        gets 0..1 and a text, on this thread. Throws std::runtime_error.
    */
    static std::string runHelper (const std::string& helper, const std::string& request, juce::Thread& thread,
                                  const std::function<void (double, const juce::String&)>& progress = {});

    /**
        Runs a job: the helper on a worker thread, then `done` on the message
        thread with the answer, or an error. One job at a time.
    */
    juce::String start (ai::Job, std::function<void (double, const juce::String&)> progress,
                        std::function<void (const ai::Job&, const std::string& answer, const juce::String& error)> done);
    void cancel();
    bool busy() const noexcept { return runner != nullptr; }

    /**
        For tool calls that come from a worker thread (the MCP connection, the
        chat with an API key): when `message` is a JSON-RPC tools/call of an AI
        tool, runs it in three steps (prepare on the message thread, the helper
        on this thread, the change on the message thread) and returns the
        reply. Returns an empty text for any other message.
    */
    static std::string runToolCall (const std::shared_ptr<SessionTools>&, const std::string& message, juce::Thread* thread, const std::function<bool()>& cancelled);

    /** Shows what an AI tool is doing (the status bar); empty text: done. Called on the message thread. */
    static std::function<void (const juce::String&)> onActivity;

private:
    struct Installer;
    struct Runner;
    std::unique_ptr<Installer> installer;
    std::unique_ptr<Runner> runner;
};

} // namespace arranger

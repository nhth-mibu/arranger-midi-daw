#pragma once

#include "arranger/core/Arranging.h"
#include "arranger/core/AudioAnalysis.h"
#include "arranger/core/ControllerProfile.h"
#include "arranger/core/DeviceProfile.h"
#include "arranger/core/MidiAi.h"
#include "arranger/core/Song.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <vector>
#include <string>

namespace arranger::mcp
{

/**
    What the server works on: the song, its undo steps and the transport.
    The app gives its session; tests give a Document.
*/
class Host
{
public:
    virtual ~Host() = default;

    virtual const Song& song() const = 0;
    /** The document revision, including project replacement, for asynchronous work. */
    virtual std::uint64_t songRevision() const = 0;
    /** One change as one undo step. */
    virtual void edit (const std::string& description, const std::function<void (Song&)>& change) = 0;
    virtual bool undo() { return false; }
    virtual bool redo() { return false; }

    virtual void play() {}
    virtual void stop() {}
    virtual void locate (Tick) {}
    virtual bool isPlaying() const { return false; }
    virtual double position() const { return 0.0; }

    /** The Clip grid. `scene` counts from 0. */
    virtual void launchSlot (TrackId, int /*scene*/) {}
    virtual void launchScene (int /*scene*/) {}
    virtual void stopSlots() {}

    /** The MIDI outputs plugged in now. */
    virtual std::vector<std::string> midiOutputs() const { return {}; }
    /** The device profiles (each synth's sound list). Null: none. */
    virtual const DeviceLibrary* devices() const { return nullptr; }
    /** The style files in the user's style folders (full paths). */
    virtual std::vector<std::string> styleFiles() const { return {}; }
    /** The user's Skills folder (JSON skill packs). Empty when there is none. */
    virtual std::string skillsFolder() const { return {}; }

    // ---- MIDI setup ----
    struct MidiInput
    {
        std::string name;
        bool enabled = true;
    };
    virtual std::vector<MidiInput> midiInputs() const { return {}; }
    /** Turns a MIDI input on or off. Returns false when there is no such input. */
    virtual bool setMidiInputEnabled (const std::string& /*name*/, bool /*on*/) { return false; }
    /** The controller pages (mixer layouts for hardware controllers). */
    virtual std::vector<ControllerProfile> controllers() const { return {}; }
    /** The controller page an input uses now: chosen by hand, else by its name. Empty: none. */
    virtual std::string controllerPageFor (const std::string& input) const
    {
        for (const auto& c : controllers())
            if (c.matchesPort (input))
                return c.id;
        return {};
    }
    /** Chooses by hand the device profile of an output (empty id: by its name again). */
    virtual void assignDeviceProfile (const std::string& /*output*/, const std::string& /*profileId*/) { throw std::runtime_error ("the device setup can't be changed here"); }
    /** Chooses by hand the controller page of an input: a page id, "none", or empty for by its name again. */
    virtual void assignControllerPage (const std::string& /*input*/, const std::string& /*pageId*/) { throw std::runtime_error ("the device setup can't be changed here"); }

    /** A device profile or controller page file (YAML). */
    struct DeviceFile
    {
        std::string name; ///< file name, e.g. "roland-d-05.yaml"
        std::string text;
        bool builtIn = false;
    };
    virtual std::vector<DeviceFile> deviceFiles() const { return {}; }
    /** Saves a file in the user's device folder and loads all profiles again. Throws on errors. */
    virtual void writeDeviceFile (const std::string& /*name*/, const std::string& /*text*/) { throw std::runtime_error ("device files can't be saved here"); }

    /** MIDI that came in during the last seconds, oldest first. */
    struct ReceivedMidi
    {
        double secondsAgo = 0.0;
        std::string port;
        std::vector<std::uint8_t> bytes;
    };
    virtual std::vector<ReceivedMidi> recentMidi (double /*seconds*/) const { return {}; }

    // ---- arranging help ----
    /** The user's earlier choices of arrangement proposals (for the order). Null: none. */
    virtual const arranging::Preferences* arrangingPreferences() const { return nullptr; }
    /** Remembers that the user took an option of a proposal. */
    virtual void rememberArrangingChoice (const arranging::Proposal&, int /*option*/) {}
    /** Saves what a device can play (polyphony, parts, range) as the user's own numbers. */
    virtual void setDeviceCapabilities (const std::string& /*profileId*/, const DeviceCapabilities&) { throw std::runtime_error ("device capabilities can't be saved here"); }

    // ---- synth sound editors ----
    /** Sends a sound setting to the synth and remembers it. False when the synth isn't plugged in. */
    virtual bool sendSynthParameter (const std::string& /*profileId*/, const std::string& /*page*/, const std::string& /*param*/, int /*value*/) { return false; }
    /** The last value sent to or read from a sound setting; -1 when not known. */
    virtual int synthParameter (const std::string& /*profileId*/, const std::string& /*page*/, const std::string& /*param*/) const { return -1; }
    /** Starts reading the sound names stored in the synth. Returns an empty text when it started, or why it can't. */
    virtual std::string readSoundNames (const std::string& /*profileId*/) { return "the app can't read from synths here"; }

    // ---- audio analysis ----
    struct AnalysisRequest
    {
        ClipId clip = 0;
        bool stems = true, notes = true, chords = true, instruments = true, lyrics = true;
        std::string language = "auto";
        std::string prompt;
    };
    /** Starts taking an audio clip apart. Returns an empty text when it started, or why it can't. */
    virtual std::string startAnalysis (const AnalysisRequest&) { return "audio analysis is not available here"; }

    struct AnalysisStatus
    {
        std::string state = "idle"; ///< idle, running, done, failed
        ClipId clip = 0;
        double progress = 0.0;
        std::string step, error, language;
        analysis::Applied applied;
        std::vector<std::string> warnings, chords, lyricLines;
        std::vector<std::pair<std::string, std::string>> instruments; ///< stem, what was heard
    };
    virtual AnalysisStatus analysisStatus() const { return {}; }

    // ---- AI music helpers ----
    /** Runs an AI helper program (ai::kMagenta or ai::kAnticipation) on a request and returns its answer (JSON). Blocks until it is done. Throws when it fails or isn't installed. */
    virtual std::string runAiHelper (const std::string& /*helper*/, const std::string& /*request*/)
    {
        throw std::runtime_error ("the AI music tools are not installed. Ask the user to choose AI > Install AI Music Tools in the app.");
    }
};

/**
    A Model Context Protocol server (JSON-RPC 2.0, one message per line) with
    tools to read and change the song, so that an AI assistant can edit it
    from plain words. It does not do input or output: the caller gives each
    message and sends the reply.

    Times in the tools are a bar (from 1) and a beat in the bar (from 1,
    decimals allowed); lengths are in beats of the time signature.
*/
class Server
{
public:
    explicit Server (Host&);

    /** Handles one message. Returns the reply, or an empty string when the message needs none (a notification). */
    std::string handle (const std::string& message);

    /** The tool definitions as JSON (for tests and the manual). */
    std::string toolList() const;

    /** Calls a tool directly; `arguments` is a JSON object. Returns the tool's JSON result text; throws on errors. */
    std::string call (const std::string& tool, const std::string& arguments);

    /**
        An AI tool call whose helper takes a while. The app runs the helper on
        another thread, so the window doesn't stop: startAiCall on the message
        thread, then Host::runAiHelper-like work elsewhere, then finishAiCall
        on the message thread again.
    */
    struct AiCall
    {
        std::string id; ///< the JSON-RPC id (JSON)
        ai::Job job;
        std::uint64_t revision = 0;
    };
    /** For a tools/call of an AI tool that runs a helper: the call. For any other message: nothing, and `reply` is
        the reply when it is already known (bad arguments, or an AI tool without a helper), else empty (use handle()). */
    std::optional<AiCall> startAiCall (const std::string& message, std::string& reply);
    /** Puts the helper's answer into the song (one undo step) and returns the reply. `error`: the helper failed. */
    std::string finishAiCall (const AiCall&, const std::string& answer, const std::string& error);

private:
    Host& host;
};

} // namespace arranger::mcp

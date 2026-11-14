#pragma once

#include "arranger/core/ControllerProfile.h"
#include "arranger/core/DeviceProfile.h"
#include "arranger/core/Song.h"

#include <cstdint>
#include <functional>
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

    // ---- synth sound editors ----
    /** Sends a sound setting to the synth and remembers it. False when the synth isn't plugged in. */
    virtual bool sendSynthParameter (const std::string& /*profileId*/, const std::string& /*page*/, const std::string& /*param*/, int /*value*/) { return false; }
    /** The last value sent to or read from a sound setting; -1 when not known. */
    virtual int synthParameter (const std::string& /*profileId*/, const std::string& /*page*/, const std::string& /*param*/) const { return -1; }
    /** Starts reading the sound names stored in the synth. Returns an empty text when it started, or why it can't. */
    virtual std::string readSoundNames (const std::string& /*profileId*/) { return "the app can't read from synths here"; }
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

private:
    Host& host;
};

} // namespace arranger::mcp

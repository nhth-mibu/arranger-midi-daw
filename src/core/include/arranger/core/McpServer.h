#pragma once

#include "arranger/core/DeviceProfile.h"
#include "arranger/core/Song.h"

#include <functional>
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

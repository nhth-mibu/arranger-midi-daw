#include "arranger/core/ControllerProfile.h"

#include "arranger/core/DeviceProfile.h"
#include "arranger/core/Song.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <map>

namespace arranger
{

namespace
{
    using Error = device_profile::Error;

    std::string lower (std::string s)
    {
        std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return s;
    }

    [[noreturn]] void fail (const YAML::Node& node, const std::string& what)
    {
        throw Error ("Line " + std::to_string (node.Mark().line + 1) + ": " + what);
    }

    ControlAction actionFromName (const YAML::Node& n)
    {
        static const std::map<std::string, ControlAction> names = {
            { "volume", ControlAction::volume }, { "pan", ControlAction::pan }, { "reverb", ControlAction::reverb },
            { "chorus", ControlAction::chorus }, { "mute", ControlAction::mute }, { "solo", ControlAction::solo },
            { "arm", ControlAction::arm }, { "select", ControlAction::select }, { "panCentre", ControlAction::panCentre },
            { "masterVolume", ControlAction::masterVolume }, { "play", ControlAction::play }, { "stop", ControlAction::stop },
            { "record", ControlAction::record }, { "loop", ControlAction::loop }, { "bankLeft", ControlAction::bankLeft },
            { "bankRight", ControlAction::bankRight }, { "launchClip", ControlAction::launchClip },
            { "launchScene", ControlAction::launchScene }, { "stopClips", ControlAction::stopClips },
            { "sceneUp", ControlAction::sceneUp }, { "sceneDown", ControlAction::sceneDown },
        };
        const auto it = names.find (n.as<std::string> (""));
        if (it == names.end())
            fail (n, "unknown action \"" + n.as<std::string> ("") + "\"");
        return it->second;
    }

    bool isStripAction (ControlAction a) { return (a >= ControlAction::volume && a <= ControlAction::panCentre) || a == ControlAction::launchClip; }
    std::string actionToString (ControlAction a)
    {
        static const std::map<ControlAction, std::string> names = {
            { ControlAction::none, "none" }, { ControlAction::volume, "volume" }, { ControlAction::pan, "pan" },
            { ControlAction::reverb, "reverb" }, { ControlAction::chorus, "chorus" }, { ControlAction::mute, "mute" },
            { ControlAction::solo, "solo" }, { ControlAction::arm, "arm" }, { ControlAction::select, "select" },
            { ControlAction::panCentre, "panCentre" }, { ControlAction::masterVolume, "masterVolume" },
            { ControlAction::play, "play" }, { ControlAction::stop, "stop" }, { ControlAction::record, "record" },
            { ControlAction::loop, "loop" }, { ControlAction::bankLeft, "bankLeft" }, { ControlAction::bankRight, "bankRight" },
            { ControlAction::launchClip, "launchClip" }, { ControlAction::launchScene, "launchScene" },
            { ControlAction::stopClips, "stopClips" }, { ControlAction::sceneUp, "sceneUp" }, { ControlAction::sceneDown, "sceneDown" },
        };
        const auto it = names.find (a);
        return it != names.end() ? it->second : "none";
    }

    std::string quote (const std::string& text)
    {
        std::string out = "\"";
        for (const char c : text)
        {
            if (c == '\\' || c == '"')
                out += '\\';
            out += c;
        }
        return out + "\"";
    }

    std::string hex (const std::vector<std::uint8_t>& bytes)
    {
        std::string out;
        char part[4] = {};
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            std::snprintf (part, sizeof (part), i == 0 ? "%02X" : " %02X", static_cast<unsigned> (bytes[i]));
            out += part;
        }
        return out;
    }
    bool isValueAction (ControlAction a) { return (a >= ControlAction::volume && a <= ControlAction::chorus) || a == ControlAction::masterVolume; }

    /** Reads `cc:` or `note:` plus channel into a message address. */
    void readAddress (const YAML::Node& n, bool& isNote, int& number, int& channel)
    {
        if (n["cc"])
            number = n["cc"].as<int>();
        else if (n["note"])
        {
            isNote = true;
            number = n["note"].as<int>();
        }
        else
            fail (n, "needs cc: or note:");
        channel = n["channel"].as<int> (0);
        if (number < 0 || number > 127)
            fail (n, "cc and note numbers are 0-127");
        if (channel < 0 || channel > 16)
            fail (n, "channel is 1-16");
    }

    std::vector<std::vector<std::uint8_t>> readMessages (const YAML::Node& list)
    {
        std::vector<std::vector<std::uint8_t>> out;
        if (list)
            for (const auto& n : list)
            {
                try
                {
                    out.push_back (device_profile::parseHex (n.as<std::string>()));
                }
                catch (const Error& e)
                {
                    fail (n, e.what());
                }
            }
        return out;
    }
}

bool ControllerProfile::matchesPort (const std::string& portName) const
{
    const auto port = lower (portName);
    return std::any_of (ports.begin(), ports.end(), [&] (const std::string& p) { return ! p.empty() && port.find (lower (p)) != std::string::npos; });
}

bool ControllerProfile::isPortOfDevice (const std::string& portName) const
{
    const auto port = lower (portName);
    return matchesPort (portName)
        || std::any_of (keyPorts.begin(), keyPorts.end(), [&] (const std::string& p) { return ! p.empty() && port.find (lower (p)) != std::string::npos; });
}

bool ControllerProfile::owns (std::uint8_t status) const noexcept
{
    if (status >= 0xf0)
        return false;
    return channel == 0 || (status & 0x0f) + 1 == channel;
}

int ControllerProfile::padRows() const noexcept
{
    int rows = 0;
    for (const auto& b : bindings)
        if (b.action == ControlAction::launchClip && b.isNote)
            rows = std::max (rows, b.row + 1);
    return rows;
}

std::vector<PadLight> padLights (const ControllerProfile& p, const Song& song, int firstTrack, int firstScene,
                                 const std::function<SlotStatus (int)>& status)
{
    std::vector<PadLight> out;
    for (const auto& b : p.bindings)
    {
        if (b.action != ControlAction::launchClip || ! b.isNote)
            continue;
        const int scene = firstScene + b.row;
        for (int i = 0; i < b.count; ++i)
        {
            PadLight light { p.pads.channel, b.number + i, p.pads.empty };
            const int index = firstTrack + b.firstStrip + i;
            if (index >= 0 && index < static_cast<int> (song.tracks.size()))
            {
                const auto& track = song.tracks[static_cast<std::size_t> (index)];
                const auto st = status (index);
                const bool has = scene >= 0 && static_cast<std::size_t> (scene) < track.slots.size() && track.slots[static_cast<std::size_t> (scene)] != nullptr;
                if (has && st.pending == scene)
                    light = { p.pads.flashChannel, light.note, p.pads.waiting };
                else if (has && st.playing == scene)
                    light = { p.pads.pulseChannel, light.note, p.pads.playing };
                else if (has)
                {
                    light.colour = p.pads.clip;
                    for (const auto& [name, colour] : p.pads.colours)
                        if (name == track.colour)
                            light.colour = colour;
                }
            }
            out.push_back (light);
        }
    }
    return out;
}

ControllerProfile controller_profile::parse (const std::string& yaml)
{
    try
    {
        const YAML::Node root = YAML::Load (yaml);
        if (! root.IsMap() || root["controller"].as<int> (0) != 1)
            throw Error ("Line 1: first line must be \"controller: 1\"");

        ControllerProfile p;
        p.id = root["id"].as<std::string> ("");
        p.name = root["name"].as<std::string> ("");
        if (p.id.empty() || p.name.empty())
            fail (root, "id and name are required");
        p.maker = root["maker"].as<std::string> ("");
        p.notes = root["notes"].as<std::string> ("");
        if (root["ports"])
            for (const auto& n : root["ports"])
                p.ports.push_back (n.as<std::string>());
        if (p.ports.empty())
            fail (root, "ports is required");
        if (root["keyPorts"])
            for (const auto& n : root["keyPorts"])
                p.keyPorts.push_back (n.as<std::string>());
        p.channel = root["channel"].as<int> (0);
        if (p.channel < 0 || p.channel > 16)
            fail (root["channel"], "channel is 1-16, or 0 for the whole port");
        p.strips = root["strips"].as<int> (8);
        if (p.strips < 1 || p.strips > 32)
            fail (root["strips"], "strips is 1-32");
        p.connect = readMessages (root["connect"]);
        p.disconnect = readMessages (root["disconnect"]);

        if (root["modes"])
            for (const auto& n : root["modes"])
            {
                ControllerProfile::ModeSwitch m;
                m.name = n["name"].as<std::string> ("");
                if (m.name.empty())
                    fail (n, "a mode needs a name");
                if (n["cc"] || n["note"])
                {
                    readAddress (n, m.isNote, m.number, m.channel);
                    m.value = n["value"].as<int> (-1);
                }
                else
                    m.number = -1; // a mode nothing switches to, other than being the default
                p.modes.push_back (std::move (m));
            }
        p.defaultMode = root["defaultMode"].as<std::string> (p.modes.empty() ? "" : p.modes.front().name);

        const auto controls = root["controls"];
        if (! controls || ! controls.IsSequence())
            fail (root, "controls is required");
        for (const auto& n : controls)
        {
            ControllerProfile::Binding b;
            readAddress (n, b.isNote, b.number, b.channel);
            b.action = actionFromName (n["action"]);
            b.count = n["count"].as<int> (1);
            if (b.count < 1 || b.number + b.count > 128)
                fail (n, "count runs past 127");
            b.firstStrip = n["strip"].as<int> (0);
            if (b.firstStrip < 0 || b.firstStrip + b.count > p.strips)
                fail (n, "strip runs past the controller's strips");
            if (b.count > 1 && ! isStripAction (b.action))
                fail (n, "count is only for per-strip actions");
            if (b.firstStrip > 0 && ! isStripAction (b.action))
                fail (n, "strip is only for per-strip actions");
            b.row = n["row"].as<int> (0);
            if (b.row < 0 || b.row > 15)
                fail (n, "row is 0-15");
            b.mode = n["mode"].as<std::string> ("");
            if (! b.mode.empty() && std::none_of (p.modes.begin(), p.modes.end(), [&] (const auto& m) { return m.name == b.mode; }))
                fail (n, "mode \"" + b.mode + "\" is not listed under modes");
            p.bindings.push_back (std::move (b));
        }

        if (const auto pads = root["pads"])
        {
            const auto number = [&] (const char* key, int fallback, int low, int high) {
                const int v = pads[key].as<int> (fallback);
                if (v < low || v > high)
                    fail (pads[key], std::string (key) + " is " + std::to_string (low) + "-" + std::to_string (high));
                return v;
            };
            p.pads.channel = number ("channel", 1, 1, 16);
            p.pads.flashChannel = number ("flashChannel", p.pads.channel, 1, 16);
            p.pads.pulseChannel = number ("pulseChannel", p.pads.channel, 1, 16);
            p.pads.empty = number ("empty", 0, 0, 127);
            p.pads.clip = number ("clip", 3, 0, 127);
            p.pads.playing = number ("playing", 21, 0, 127);
            p.pads.waiting = number ("waiting", p.pads.playing, 0, 127);
            if (const auto colours = pads["colours"])
                for (const auto& entry : colours)
                {
                    const int v = entry.second.as<int>();
                    if (v < 0 || v > 127)
                        fail (entry.second, "a colour is 0-127");
                    p.pads.colours.push_back ({ entry.first.as<std::string>(), v });
                }
        }
        return p;
    }
    catch (const YAML::Exception& e)
    {
        throw Error ("Line " + std::to_string (e.mark.line + 1) + ": " + e.msg);
    }
}

ControllerSurface::ControllerSurface (ControllerProfile p) : prof (std::move (p)), currentMode (prof.defaultMode) {}

std::optional<SurfaceEvent> ControllerSurface::handle (const std::array<std::uint8_t, 3>& bytes)
{
    const int kind = bytes[0] & 0xf0;
    const int channel = (bytes[0] & 0x0f) + 1;
    const int number = bytes[1], value = bytes[2];
    const bool isCc = kind == 0xb0, isNoteOn = kind == 0x90 && value > 0;
    const bool isNoteOff = kind == 0x80 || (kind == 0x90 && value == 0);
    if (isNoteOff)
    {
        // A pad that goes up: only clip pads report it (chord pads sound while held).
        for (const auto& b : prof.bindings)
            if (b.isNote && b.action == ControlAction::launchClip && (b.channel == 0 || b.channel == channel)
                && number >= b.number && number < b.number + b.count && (b.mode.empty() || b.mode == currentMode))
                return SurfaceEvent { b.action, b.firstStrip + (number - b.number), 0, b.row, true };
        return {};
    }
    if (! isCc && ! isNoteOn)
        return {};

    for (const auto& m : prof.modes)
        if (m.number >= 0 && m.isNote == ! isCc && m.number == number && (m.channel == 0 || m.channel == channel)
            && (m.value < 0 ? value > 0 : m.value == value))
        {
            currentMode = m.name;
            return {};
        }

    for (const auto& b : prof.bindings)
    {
        if (b.isNote == isCc || (b.channel != 0 && b.channel != channel))
            continue;
        if (number < b.number || number >= b.number + b.count)
            continue;
        if (! b.mode.empty() && b.mode != currentMode)
            continue;
        if (! isValueAction (b.action) && value == 0)
            return {}; // button release
        return SurfaceEvent { b.action, b.firstStrip + (number - b.number), value, b.row };
    }
    return {};
}


std::string controller_profile::write (const ControllerProfile& p)
{
    std::string y = "controller: 1\n";
    y += "id: " + quote (p.id) + "\n";
    y += "name: " + quote (p.name) + "\n";
    if (! p.maker.empty())
        y += "maker: " + quote (p.maker) + "\n";
    y += "ports: [";
    for (std::size_t i = 0; i < p.ports.size(); ++i)
        y += (i == 0 ? "" : ", ") + quote (p.ports[i]);
    y += "]\n";
    if (! p.keyPorts.empty())
    {
        y += "keyPorts: [";
        for (std::size_t i = 0; i < p.keyPorts.size(); ++i)
            y += (i == 0 ? "" : ", ") + quote (p.keyPorts[i]);
        y += "]\n";
    }
    y += "channel: " + std::to_string (p.channel) + "\n";
    y += "strips: " + std::to_string (p.strips) + "\n";
    if (! p.defaultMode.empty())
        y += "defaultMode: " + quote (p.defaultMode) + "\n";
    if (! p.notes.empty())
    {
        y += "notes: |\n";
        std::string notes = p.notes;
        if (! notes.empty() && notes.back() == '\n')
            notes.pop_back();
        std::size_t start = 0;
        while (start <= notes.size())
        {
            const auto end = notes.find ('\n', start);
            y += "  " + notes.substr (start, end == std::string::npos ? std::string::npos : end - start) + "\n";
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
    }
    if (! p.connect.empty())
    {
        y += "connect:\n";
        for (const auto& m : p.connect)
            y += "  - " + quote (hex (m)) + "\n";
    }
    if (! p.disconnect.empty())
    {
        y += "disconnect:\n";
        for (const auto& m : p.disconnect)
            y += "  - " + quote (hex (m)) + "\n";
    }
    if (! p.modes.empty())
    {
        y += "modes:\n";
        for (const auto& m : p.modes)
        {
            y += "  - { name: " + quote (m.name);
            if (m.number >= 0)
            {
                y += std::string (", ") + (m.isNote ? "note: " : "cc: ") + std::to_string (m.number);
                if (m.channel != 0)
                    y += ", channel: " + std::to_string (m.channel);
                if (m.value >= 0)
                    y += ", value: " + std::to_string (m.value);
            }
            y += " }\n";
        }
    }
    y += "controls:\n";
    for (const auto& b : p.bindings)
    {
        y += b.isNote ? "  - { note: " : "  - { cc: ";
        y += std::to_string (b.number);
        if (b.channel != 0)
            y += ", channel: " + std::to_string (b.channel);
        if (b.count != 1)
            y += ", count: " + std::to_string (b.count);
        if (b.firstStrip != 0)
            y += ", strip: " + std::to_string (b.firstStrip);
        y += ", action: " + actionToString (b.action);
        if (b.row != 0)
            y += ", row: " + std::to_string (b.row);
        if (! b.mode.empty())
            y += ", mode: " + quote (b.mode);
        y += " }\n";
    }
    y += "pads:\n";
    y += "  channel: " + std::to_string (p.pads.channel) + "\n";
    if (p.pads.flashChannel != p.pads.channel)
        y += "  flashChannel: " + std::to_string (p.pads.flashChannel) + "\n";
    if (p.pads.pulseChannel != p.pads.channel)
        y += "  pulseChannel: " + std::to_string (p.pads.pulseChannel) + "\n";
    y += "  empty: " + std::to_string (p.pads.empty) + "\n";
    y += "  clip: " + std::to_string (p.pads.clip) + "\n";
    y += "  playing: " + std::to_string (p.pads.playing) + "\n";
    y += "  waiting: " + std::to_string (p.pads.waiting) + "\n";
    if (! p.pads.colours.empty())
    {
        y += "  colours: {";
        for (std::size_t i = 0; i < p.pads.colours.size(); ++i)
            y += (i == 0 ? " " : ", ") + quote (p.pads.colours[i].first) + ": " + std::to_string (p.pads.colours[i].second);
        y += " }\n";
    }
    return y;
}

} // namespace arranger

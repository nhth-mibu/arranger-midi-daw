#include "arranger/core/ControllerProfile.h"

#include "arranger/core/DeviceProfile.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
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
            { "bankRight", ControlAction::bankRight },
        };
        const auto it = names.find (n.as<std::string> (""));
        if (it == names.end())
            fail (n, "unknown action \"" + n.as<std::string> ("") + "\"");
        return it->second;
    }

    bool isStripAction (ControlAction a) { return a >= ControlAction::volume && a <= ControlAction::panCentre; }
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

bool ControllerProfile::owns (std::uint8_t status) const noexcept
{
    if (status >= 0xf0)
        return false;
    return channel == 0 || (status & 0x0f) + 1 == channel;
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
            if (b.count > 1 && ! isStripAction (b.action))
                fail (n, "count is only for per-strip actions");
            b.mode = n["mode"].as<std::string> ("");
            if (! b.mode.empty() && std::none_of (p.modes.begin(), p.modes.end(), [&] (const auto& m) { return m.name == b.mode; }))
                fail (n, "mode \"" + b.mode + "\" is not listed under modes");
            p.bindings.push_back (std::move (b));
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
        return SurfaceEvent { b.action, number - b.number, value };
    }
    return {};
}

} // namespace arranger

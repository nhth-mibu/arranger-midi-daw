#include "arranger/core/DeviceProfile.h"

#include "arranger/core/GeneralMidi.h"

#include "SynthEditorYaml.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace arranger
{

namespace
{
    std::string lower (std::string s)
    {
        std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return s;
    }

    [[noreturn]] void fail (const YAML::Node& node, const std::string& what)
    {
        const auto mark = node.Mark();
        std::ostringstream msg;
        if (mark.line >= 0)
            msg << "Line " << (mark.line + 1) << ": ";
        msg << what;
        throw device_profile::Error (msg.str());
    }

    /** A controller number, or -1 for null / "none" / "-". */
    int controllerOrNone (const YAML::Node& n, const char* key, int fallback)
    {
        const auto v = n[key];
        if (! v)
            return fallback;
        if (v.IsNull())
            return -1;
        const auto text = v.as<std::string>();
        if (text == "none" || text == "-")
            return -1;
        const int cc = v.as<int>();
        if (cc < 0 || cc > 127)
            fail (v, std::string (key) + " must be a controller number 0-127, or none");
        return cc;
    }

    int byteOrNone (const YAML::Node& v, const char* what, int lo, int hi, int offset = 0)
    {
        if (! v || v.IsNull())
            return -1;
        const auto text = v.as<std::string>();
        if (text == "-" || text == "none")
            return -1;
        int value = 0;
        try
        {
            value = v.as<int>();
        }
        catch (const YAML::Exception&)
        {
            fail (v, std::string (what) + " must be a number");
        }
        if (value < lo || value > hi)
            fail (v, std::string (what) + " must be " + std::to_string (lo) + "-" + std::to_string (hi));
        return value - offset;
    }

    ProfileSound parseSound (const YAML::Node& n, int programBase)
    {
        ProfileSound s;
        const int maxProgram = 127 + programBase;
        if (n.IsSequence())
        {
            // Compact row: [name, msb, lsb, program]
            if (n.size() != 4)
                fail (n, "a sound row needs 4 values: [name, msb, lsb, program]");
            s.name = n[0].as<std::string>();
            s.bankMsb = byteOrNone (n[1], "bank MSB", 0, 127);
            s.bankLsb = byteOrNone (n[2], "bank LSB", 0, 127);
            s.program = byteOrNone (n[3], "program", programBase, maxProgram, programBase);
        }
        else if (n.IsMap())
        {
            s.name = n["name"].as<std::string> ("");
            s.bankMsb = byteOrNone (n["msb"], "msb", 0, 127);
            s.bankLsb = byteOrNone (n["lsb"], "lsb", 0, 127);
            s.program = byteOrNone (n["program"], "program", programBase, maxProgram, programBase);
        }
        else
            fail (n, "a sound is either [name, msb, lsb, program] or a map");
        if (s.name.empty())
            fail (n, "a sound needs a name");
        return s;
    }

    std::vector<SoundGroup> parseGroups (const YAML::Node& sounds, int programBase)
    {
        if (! sounds.IsSequence())
            fail (sounds, "sounds is a list of groups");
        std::vector<SoundGroup> groups;
        for (const auto& g : sounds)
        {
            SoundGroup group;
            group.name = g["group"].as<std::string> ("");
            const auto items = g["items"];
            if (! items || ! items.IsSequence())
                fail (g, "each sound group needs items");
            for (const auto& item : items)
                group.sounds.push_back (parseSound (item, programBase));
            groups.push_back (std::move (group));
        }
        return groups;
    }

    void mergeGroups (std::vector<SoundGroup>& into, const std::vector<SoundGroup>& extra)
    {
        for (const auto& g : extra)
        {
            auto it = std::find_if (into.begin(), into.end(), [&] (const SoundGroup& x) { return x.name == g.name; });
            if (it == into.end())
            {
                into.push_back (g);
                continue;
            }
            for (const auto& s : g.sounds)
            {
                auto same = std::find_if (it->sounds.begin(), it->sounds.end(), [&] (const ProfileSound& x) {
                    return x.bankMsb == s.bankMsb && x.bankLsb == s.bankLsb && x.program == s.program;
                });
                if (same != it->sounds.end())
                    same->name = s.name;
                else
                    it->sounds.push_back (s);
            }
        }
    }

    std::string quoted (const std::string& s)
    {
        std::string out = "\"";
        for (const char c : s)
        {
            if (c == '"' || c == '\\')
                out += '\\';
            out += c;
        }
        return out + "\"";
    }
}

void DeviceProfile::addSounds (const std::vector<SoundGroup>& extra)
{
    mergeGroups (groups, extra);
}

void SoundAddOn::merge (const std::vector<SoundGroup>& extra)
{
    mergeGroups (groups, extra);
}

bool DeviceProfile::matchesPort (const std::string& portName) const
{
    const auto port = lower (portName);
    return std::any_of (ports.begin(), ports.end(), [&] (const std::string& p) { return ! p.empty() && port.find (lower (p)) != std::string::npos; });
}

std::size_t DeviceProfile::soundCount() const noexcept
{
    std::size_t n = 0;
    for (const auto& g : groups)
        n += g.sounds.size();
    return n;
}

TrackSound DeviceProfile::trackSound (const SoundGroup& group, const ProfileSound& sound) const
{
    TrackSound t;
    t.name = group.name.empty() ? sound.name : group.name + " > " + sound.name;
    t.bankMsb = sound.bankMsb;
    t.bankLsb = sound.bankLsb;
    t.program = sound.program;
    return t;
}

namespace device_profile
{

std::vector<std::uint8_t> parseHex (const std::string& text)
{
    std::vector<std::uint8_t> bytes;
    std::istringstream in (text);
    std::string token;
    while (in >> token)
    {
        if (token.size() > 2 && (token[0] == '0') && (token[1] == 'x' || token[1] == 'X'))
            token = token.substr (2);
        if (token.size() > 1 && (token.back() == 'h' || token.back() == 'H'))
            token.pop_back();
        if (token.empty() || token.size() > 2 || ! std::all_of (token.begin(), token.end(), [] (unsigned char c) { return std::isxdigit (c); }))
            throw Error ("\"" + token + "\" is not a hex byte");
        bytes.push_back (static_cast<std::uint8_t> (std::stoi (token, nullptr, 16)));
    }
    if (bytes.empty())
        throw Error ("empty message");
    if (bytes.front() == 0xf0 && bytes.back() != 0xf7)
        throw Error ("SysEx must end with F7");
    return bytes;
}

DeviceProfile parse (const std::string& yaml)
{
    YAML::Node root;
    try
    {
        root = YAML::Load (yaml);
    }
    catch (const YAML::ParserException& e)
    {
        throw Error ("Line " + std::to_string (e.mark.line + 1) + ": " + e.msg);
    }
    if (! root.IsMap())
        throw Error ("A device profile is a YAML map (key: value lines)");

    try
    {
        if (const auto v = root["profile"]; ! v || v.as<int> (0) != 1)
            fail (root, "first line must be \"profile: 1\"");

        DeviceProfile p;
        p.id = root["id"].as<std::string> ("");
        p.name = root["name"].as<std::string> ("");
        if (p.id.empty() || p.name.empty())
            fail (root, "id and name are required");
        p.maker = root["maker"].as<std::string> ("");
        p.notes = root["notes"].as<std::string> ("");
        p.dialect = lower (root["dialect"].as<std::string> ("gm"));
        if (p.dialect != "gm" && p.dialect != "gs" && p.dialect != "xg" && p.dialect != "none")
            fail (root["dialect"], "dialect must be gm, gs, xg or none");

        if (const auto ports = root["ports"])
        {
            if (! ports.IsSequence())
                fail (ports, "ports is a list of names");
            for (const auto& n : ports)
                p.ports.push_back (n.as<std::string>());
        }

        if (const auto drums = root["drumChannels"])
        {
            p.drumChannels.clear();
            for (const auto& n : drums)
            {
                const int ch = n.as<int>();
                if (ch < 1 || ch > 16)
                    fail (n, "drum channels are 1-16");
                p.drumChannels.push_back (ch);
            }
        }

        if (const auto m = root["mixer"])
        {
            p.mixer.volume = controllerOrNone (m, "volume", 7);
            p.mixer.pan = controllerOrNone (m, "pan", 10);
            p.mixer.reverb = controllerOrNone (m, "reverb", 91);
            p.mixer.chorus = controllerOrNone (m, "chorus", 93);
        }

        if (const auto reset = root["reset"])
            for (const auto& n : reset)
            {
                try
                {
                    p.reset.push_back (parseHex (n.as<std::string>()));
                }
                catch (const Error& e)
                {
                    fail (n, e.what());
                }
            }

        const int programBase = root["programBase"].as<int> (1);
        if (programBase != 0 && programBase != 1)
            fail (root["programBase"], "programBase is 0 or 1");

        if (const auto sounds = root["sounds"])
            p.groups = parseGroups (sounds, programBase);
        if (const auto e = root["editor"])
            p.editor = synth_edit::parseEditor (e);
        return p;
    }
    catch (const YAML::Exception& e)
    {
        throw Error ("Line " + std::to_string (e.mark.line + 1) + ": " + e.msg);
    }
}

bool isSoundAddOn (const std::string& yaml)
{
    return yaml.rfind ("soundsFor:", 0) == 0 || yaml.find ("\nsoundsFor:") != std::string::npos;
}

SoundAddOn parseAddOn (const std::string& yaml)
{
    YAML::Node root;
    try
    {
        root = YAML::Load (yaml);
    }
    catch (const YAML::ParserException& e)
    {
        throw Error ("Line " + std::to_string (e.mark.line + 1) + ": " + e.msg);
    }
    try
    {
        if (! root.IsMap() || ! root["soundsFor"])
            throw Error ("a sound list starts with \"soundsFor: <device profile id>\"");
        SoundAddOn a;
        a.profileId = root["soundsFor"].as<std::string>();
        if (const auto sounds = root["sounds"])
            a.groups = parseGroups (sounds, 1);
        return a;
    }
    catch (const YAML::Exception& e)
    {
        throw Error ("Line " + std::to_string (e.mark.line + 1) + ": " + e.msg);
    }
}

std::string writeAddOn (const SoundAddOn& a)
{
    std::ostringstream out;
    out << "# Your own sounds for this device. The app adds them to its sound list.\n"
        << "# A sound with the bank and program of a listed one renames it.\n"
        << "soundsFor: " << a.profileId << "\n\nsounds:\n";
    const auto byte = [] (int v) { return v < 0 ? std::string ("-") : std::to_string (v); };
    for (const auto& g : a.groups)
    {
        out << "  - group: " << quoted (g.name) << "\n    items:\n";
        for (const auto& s : g.sounds)
            out << "      - [" << quoted (s.name) << ", " << byte (s.bankMsb) << ", " << byte (s.bankLsb) << ", " << (s.program + 1) << "]\n";
    }
    return out.str();
}

DeviceProfile generalMidi()
{
    DeviceProfile p;
    p.id = "general-midi";
    p.name = "General MIDI";
    p.dialect = "gm";
    p.reset.push_back ({ 0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7 });
    for (int f = 0; f < 16; ++f)
    {
        SoundGroup g;
        g.name = kGeneralMidiFamilies[f];
        for (int i = 0; i < 8; ++i)
            g.sounds.push_back ({ kGeneralMidiNames[f * 8 + i], -1, -1, f * 8 + i });
        p.groups.push_back (std::move (g));
    }
    return p;
}

} // namespace device_profile

void DeviceLibrary::add (DeviceProfile p)
{
    for (auto& existing : profiles)
        if (existing.id == p.id)
        {
            existing = std::move (p);
            return;
        }
    profiles.push_back (std::move (p));
}

const DeviceProfile* DeviceLibrary::byId (const std::string& id) const
{
    for (const auto& p : profiles)
        if (p.id == id)
            return &p;
    return id == gm.id ? &gm : nullptr;
}

bool DeviceLibrary::addSounds (const SoundAddOn& a)
{
    for (auto& p : profiles)
        if (p.id == a.profileId)
        {
            p.addSounds (a.groups);
            return true;
        }
    return false;
}

void DeviceLibrary::assign (const std::string& portName, const std::string& profileId)
{
    if (profileId.empty())
        assigned.erase (portName);
    else
        assigned[portName] = profileId;
}

const DeviceProfile& DeviceLibrary::forPort (const std::string& portName) const
{
    if (const auto it = assigned.find (portName); it != assigned.end())
        if (const auto* chosen = byId (it->second))
            return *chosen;
    // The most specific match wins: the longest matching port pattern.
    const DeviceProfile* best = nullptr;
    std::size_t bestLength = 0;
    const auto port = lower (portName);
    for (const auto& p : profiles)
        for (const auto& pattern : p.ports)
            if (! pattern.empty() && port.find (lower (pattern)) != std::string::npos && pattern.size() > bestLength)
            {
                best = &p;
                bestLength = pattern.size();
            }
    return best != nullptr ? *best : gm;
}

} // namespace arranger

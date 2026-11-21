#include "arranger/core/MidiInputPolicy.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace arranger
{

bool MidiInputPolicy::looksVirtual (const std::string& portName)
{
    std::string name = portName;
    std::transform (name.begin(), name.end(), name.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    static const std::array<const char*, 9> virtualPorts = {
        "iac driver", "iac bus", "network session", "midi through", "loopmidi", "loopbe", "virtual", "rtpmidi", "arranger midi daw",
    };
    return std::any_of (virtualPorts.begin(), virtualPorts.end(), [&] (const char* v) { return name.find (v) != std::string::npos; });
}

std::vector<std::string> MidiInputPolicy::update (const std::vector<Port>& ports, const std::function<bool (const std::string&)>& isKnown)
{
    std::vector<std::string> enable;
    std::map<std::string, bool> now;
    for (const auto& p : ports)
    {
        bool state = p.enabled;
        if (const auto it = last.find (p.name); it == last.end())
        {
            // New since the last look: an instrument that is off comes on, unless the user said no.
            const bool instrument = (isKnown && isKnown (p.name)) || ! looksVirtual (p.name);
            if (! state && off.count (p.name) == 0 && instrument)
            {
                enable.push_back (p.name);
                state = true;
            }
        }
        else if (it->second != state)
        {
            // Changed by the user since the last look.
            if (state)
                off.erase (p.name);
            else
                off.insert (p.name);
        }
        now[p.name] = state;
    }
    last = std::move (now); // an input that goes away counts as new when it comes back
    return enable;
}

} // namespace arranger

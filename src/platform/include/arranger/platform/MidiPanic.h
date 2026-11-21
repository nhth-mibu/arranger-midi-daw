#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace arranger::platform
{
/** The channel messages that silence a synth: sustain off, all sound off, all notes off. */
inline std::vector<std::array<std::uint8_t, 3>> panicMessages()
{
    std::vector<std::array<std::uint8_t, 3>> messages;
    messages.reserve (16 * 3);
    for (int channel = 0; channel < 16; ++channel)
    {
        const auto status = static_cast<std::uint8_t> (0xb0 | channel);
        messages.push_back ({ status, 64, 0 });  // sustain off
        messages.push_back ({ status, 120, 0 }); // all sound off
        messages.push_back ({ status, 123, 0 }); // all notes off
    }
    return messages;
}
} // namespace arranger::platform

#include "arranger/core/D05Reverb.h"

namespace arranger::d05_reverb
{

std::uint32_t address (int slot)
{
    const auto n = static_cast<std::uint32_t> (slot < firstSlot ? firstSlot : slot > firstSlot + slotCount - 1 ? firstSlot + slotCount - 1 : slot);
    return (3u * 128u + 96u) * 128u + (n - firstSlot) * 376u;
}

void size (std::uint8_t out[3])
{
    out[0] = 0x00;
    out[1] = 0x01;
    out[2] = 0x3c;
}

std::vector<std::uint8_t> readRequest (int slot, std::uint8_t deviceId)
{
    const auto a = address (slot);
    std::vector<std::uint8_t> msg { 0xf0, 0x41, deviceId, 0x14, 0x11,
                                    static_cast<std::uint8_t> ((a >> 14) & 0x7f),
                                    static_cast<std::uint8_t> ((a >> 7) & 0x7f),
                                    static_cast<std::uint8_t> (a & 0x7f),
                                    0x00, 0x01, 0x3c };
    int sum = 0;
    for (std::size_t i = 5; i < msg.size(); ++i)
        sum += msg[i];
    msg.push_back (static_cast<std::uint8_t> ((128 - (sum & 0x7f)) & 0x7f));
    msg.push_back (0xf7);
    return msg;
}

} // namespace arranger::d05_reverb

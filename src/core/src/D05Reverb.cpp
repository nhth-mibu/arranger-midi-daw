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

} // namespace arranger::d05_reverb

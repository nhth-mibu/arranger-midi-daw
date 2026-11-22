#include "arranger/core/D05Reverb.h"

#include <algorithm>

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
    out[1] = 0x02;
    out[2] = 0x78;
}

std::vector<std::uint8_t> readRequest (int slot, std::uint8_t deviceId)
{
    const auto a = address (slot);
    std::vector<std::uint8_t> msg { 0xf0, 0x41, deviceId, 0x14, 0x11,
                                    static_cast<std::uint8_t> ((a >> 14) & 0x7f),
                                    static_cast<std::uint8_t> ((a >> 7) & 0x7f),
                                    static_cast<std::uint8_t> (a & 0x7f),
                                    0x00, 0x02, 0x78 };
    int sum = 0;
    for (std::size_t i = 5; i < msg.size(); ++i)
        sum += msg[i];
    msg.push_back (static_cast<std::uint8_t> ((128 - (sum & 0x7f)) & 0x7f));
    msg.push_back (0xf7);
    return msg;
}

std::vector<int> decode (const std::vector<std::uint8_t>& data)
{
    std::vector<int> values;
    values.reserve (data.size() / 2);
    for (std::size_t i = 0; i + 1 < data.size(); i += 2)
        values.push_back (((data[i] & 0x0f) << 4) | (data[i + 1] & 0x0f));
    return values;
}

std::vector<std::uint8_t> encode (const std::vector<int>& values)
{
    std::vector<std::uint8_t> data;
    data.reserve (values.size() * 2);
    for (const int v : values)
    {
        const int clamped = v < 0 ? 0 : v > 255 ? 255 : v;
        data.push_back (static_cast<std::uint8_t> ((clamped >> 4) & 0x0f));
        data.push_back (static_cast<std::uint8_t> (clamped & 0x0f));
    }
    return data;
}

std::vector<std::vector<std::uint8_t>> writeRequests (int slot, const std::vector<int>& values, std::uint8_t deviceId)
{
    const auto data = encode (values);
    std::vector<std::vector<std::uint8_t>> messages;
    for (std::size_t offset = 0; offset < data.size(); offset += 256)
    {
        const auto count = std::min<std::size_t> (256, data.size() - offset);
        const auto a = address (slot) + offset;
        std::vector<std::uint8_t> msg { 0xf0, 0x41, deviceId, 0x14, 0x12,
                                        static_cast<std::uint8_t> ((a >> 14) & 0x7f),
                                        static_cast<std::uint8_t> ((a >> 7) & 0x7f),
                                        static_cast<std::uint8_t> (a & 0x7f) };
        msg.insert (msg.end(), data.begin() + static_cast<long> (offset), data.begin() + static_cast<long> (offset + count));
        int sum = 0;
        for (std::size_t i = 5; i < msg.size(); ++i)
            sum += msg[i];
        msg.push_back (static_cast<std::uint8_t> ((128 - (sum & 0x7f)) & 0x7f));
        msg.push_back (0xf7);
        messages.push_back (std::move (msg));
    }
    return messages;
}

} // namespace arranger::d05_reverb

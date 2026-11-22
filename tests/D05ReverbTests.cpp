#include "arranger/core/D05Reverb.h"

#include <catch2/catch_test_macros.hpp>

using namespace arranger;

TEST_CASE ("D-05 reverb slots use the documented addresses", "[d05]")
{
    CHECK (d05_reverb::address (17) == 0x0F000u);
    CHECK (d05_reverb::address (18) == 0x0F178u); // 0x036178
    CHECK (d05_reverb::address (32) == 0x10608u);
    std::uint8_t size[3] = {};
    d05_reverb::size (size);
    CHECK (size[0] == 0x00);
    CHECK (size[1] == 0x02);
    CHECK (size[2] == 0x78);
    CHECK (d05_reverb::readRequest (17) == std::vector<std::uint8_t> { 0xf0, 0x41, 0x10, 0x14, 0x11, 0x03, 0x60, 0x00, 0x00, 0x02, 0x78, 0x23, 0xf7 });
    CHECK (d05_reverb::decode ({ 0x00, 0x00, 0x0a, 0x0b }) == std::vector<int> { 0, 171 });
    CHECK (d05_reverb::encode ({ 0, 171 }) == std::vector<std::uint8_t> { 0x00, 0x00, 0x0a, 0x0b });
    const auto writes = d05_reverb::writeRequests (17, std::vector<int> (188, 0));
    REQUIRE (writes.size() == 2);
    CHECK (writes[0].front() == 0xf0);
    CHECK (writes[0][4] == 0x12);
    CHECK (writes[0].back() == 0xf7);
    CHECK (writes[0].size() == 8 + 256 + 2);
    CHECK (writes[1].size() == 8 + 120 + 2);
    std::vector<int> values (188);
    for (int i = 0; i < 188; ++i)
        values[static_cast<std::size_t> (i)] = (i * 37) & 0xff;
    CHECK (d05_reverb::decode (d05_reverb::encode (values)) == values);
}

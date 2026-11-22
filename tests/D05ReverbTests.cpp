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
    CHECK (size[1] == 0x01);
    CHECK (size[2] == 0x3c);
    CHECK (d05_reverb::readRequest (17) == std::vector<std::uint8_t> { 0xf0, 0x41, 0x10, 0x14, 0x11, 0x03, 0x60, 0x00, 0x00, 0x01, 0x3c, 0x60, 0xf7 });
    CHECK (d05_reverb::decode ({ 0x00, 0x00, 0x01, 0x02, 0x7f, 0x7f }) == std::vector<int> { 0, 130, 16383 });
    CHECK (d05_reverb::encode ({ 0, 130, 16383 }) == std::vector<std::uint8_t> { 0x00, 0x00, 0x01, 0x02, 0x7f, 0x7f });
    const auto write = d05_reverb::writeRequest (17, std::vector<int> (94, 0));
    CHECK (write.front() == 0xf0);
    CHECK (write[4] == 0x12);
    CHECK (write.back() == 0xf7);
    CHECK (write.size() == 8 + 188 + 2);
    std::vector<int> values (94);
    for (int i = 0; i < 94; ++i)
        values[static_cast<std::size_t> (i)] = i * 173;
    CHECK (d05_reverb::decode (d05_reverb::encode (values)) == values);
}

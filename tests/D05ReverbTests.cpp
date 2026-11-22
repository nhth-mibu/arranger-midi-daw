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
}

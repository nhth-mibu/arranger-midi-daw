#include "arranger/platform/MidiPanic.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("MIDI panic silences every channel", "[midi]")
{
    const auto messages = arranger::platform::panicMessages();
    REQUIRE (messages.size() == 48);

    for (int channel = 0; channel < 16; ++channel)
    {
        const auto status = static_cast<std::uint8_t> (0xb0 | channel);
        CHECK (messages[static_cast<std::size_t> (channel * 3)] == std::array<std::uint8_t, 3> { status, 64, 0 });
        CHECK (messages[static_cast<std::size_t> (channel * 3 + 1)] == std::array<std::uint8_t, 3> { status, 120, 0 });
        CHECK (messages[static_cast<std::size_t> (channel * 3 + 2)] == std::array<std::uint8_t, 3> { status, 123, 0 });
    }
}

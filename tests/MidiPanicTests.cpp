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

TEST_CASE ("MIDI panic discards messages that were waiting to be sent", "[midi]")
{
    arranger::platform::PanicQueue<int> queue;
    queue.push (1);
    queue.push (2);
    queue.requestPanic();

    CHECK (queue.discardBeforeSending());
    CHECK (queue.empty());
    CHECK_FALSE (queue.discardBeforeSending());

    queue.push (3);
    CHECK (queue.discardBeforeSending() == false);
    CHECK (queue.size() == 1);
}

TEST_CASE ("A panic on an empty queue does not drop the next note", "[midi]")
{
    arranger::platform::PanicQueue<int> queue;
    queue.requestPanic();

    CHECK (queue.discardBeforeSending());
    queue.push (4);
    CHECK (queue.size() == 1);
    CHECK_FALSE (queue.discardBeforeSending());
}

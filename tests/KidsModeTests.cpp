#include "arranger/core/KidsMode.h"

#include <catch2/catch_test_macros.hpp>

using namespace arranger::kids_mode;

TEST_CASE ("Kids Mode: quiet before any key")
{
    Keys k;
    CHECK (k.quiet (0.0, 1.5));
    CHECK (k.presses() == 0);
}

TEST_CASE ("Kids Mode: a tap keeps the song playing for the wait")
{
    Keys k;
    k.tap (1000.0);
    CHECK_FALSE (k.quiet (1000.0, 1.5));
    CHECK_FALSE (k.quiet (2400.0, 1.5));
    CHECK (k.quiet (2500.0, 1.5));
    CHECK (k.presses() == 1);
}

TEST_CASE ("Kids Mode: a held key keeps playing, the wait starts when it goes up")
{
    Keys k;
    k.down (60, 0.0);
    CHECK (k.held (5000.0) == 1);
    CHECK_FALSE (k.quiet (5000.0, 1.0));
    k.up (60, 5000.0);
    CHECK_FALSE (k.quiet (5900.0, 1.0));
    CHECK (k.quiet (6000.0, 1.0));
}

TEST_CASE ("Kids Mode: a lost note-off stops counting after a while")
{
    Keys k;
    k.down (60, 0.0);
    CHECK_FALSE (k.quiet (kMaxHoldMs - 1.0, 1.0));
    CHECK (k.held (kMaxHoldMs + 1.0) == 0);
    CHECK_FALSE (k.quiet (kMaxHoldMs + 500.0, 1.0));
    CHECK (k.quiet (kMaxHoldMs + 1000.0, 1.0));
}

TEST_CASE ("Kids Mode: the wait is kept in range")
{
    CHECK (clampPauseAfter (0.1) == kMinPauseAfter);
    CHECK (clampPauseAfter (60.0) == kMaxPauseAfter);
    CHECK (clampPauseAfter (2.0) == 2.0);
    Keys k;
    k.tap (0.0);
    CHECK_FALSE (k.quiet (400.0, 0.0)); // 0 s becomes 0.5 s
    CHECK (k.quiet (500.0, 0.0));
}

TEST_CASE ("Kids Mode: reset forgets held keys")
{
    Keys k;
    k.down (60, 0.0);
    k.reset();
    CHECK (k.held (1.0) == 0);
    CHECK (k.quiet (1.0, 1.0));
    k.up (60, 2.0); // the note-off of a forgotten key changes nothing
    CHECK (k.quiet (3.0, 1.0));
}

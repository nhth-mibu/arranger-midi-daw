#include "arranger/core/Time.h"

#include <catch2/catch_test_macros.hpp>

using namespace arranger;

TEST_CASE ("Beats and bars follow the meter")
{
    CHECK (TimeSignature { 4, 4 }.ticksPerBeat() == 960);
    CHECK (TimeSignature { 4, 4 }.ticksPerBar() == 3840);
    CHECK (TimeSignature { 6, 8 }.ticksPerBeat() == 480);
    CHECK (TimeSignature { 6, 8 }.ticksPerBar() == 2880);
    CHECK (TimeSignature { 3, 4 }.ticksPerBar() == 2880);
    CHECK (TimeSignature { 7, 8 }.isValid());
    CHECK_FALSE (TimeSignature { 4, 3 }.isValid());
    CHECK_FALSE (TimeSignature { 0, 4 }.isValid());
}

TEST_CASE ("Bar.beat.sixteenth display")
{
    const TimeSignature fourFour;
    CHECK (formatBarBeat (toBarBeat (0, fourFour)) == "1.1.1");
    CHECK (formatBarBeat (toBarBeat (960, fourFour)) == "1.2.1");
    CHECK (formatBarBeat (toBarBeat (240, fourFour)) == "1.1.2");
    CHECK (formatBarBeat (toBarBeat (3840 * 16 + 960 * 2 + 240, fourFour)) == "17.3.2");
    CHECK (toBarBeat (250, fourFour).tick == 10);

    const TimeSignature sixEight { 6, 8 };
    CHECK (formatBarBeat (toBarBeat (480 * 5, sixEight)) == "1.6.1");
    CHECK (formatBarBeat (toBarBeat (480 * 6, sixEight)) == "2.1.1");

    // A one-bar count-in before the song shows as bar 0.
    CHECK (formatBarBeat (toBarBeat (-960, fourFour)) == "0.4.1");
}

TEST_CASE ("Tempo is kept in range")
{
    CHECK (clampTempo (5.0) == kMinTempoBpm);
    CHECK (clampTempo (1000.0) == kMaxTempoBpm);
    CHECK (clampTempo (112.5) == 112.5);
}

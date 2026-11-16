#include "arranger/core/Progressions.h"

#include <catch2/catch_test_macros.hpp>

#include <sstream>

using namespace arranger;

namespace
{
    std::size_t chordCount (const std::string& roman)
    {
        std::istringstream in (roman);
        std::string word;
        std::size_t count = 0;
        while (in >> word)
            ++count;
        return count;
    }
}

TEST_CASE ("Every common progression spells in its key", "[progressions]")
{
    const auto& all = progressions::all();
    REQUIRE (all.size() == 25);
    for (const auto& entry : all)
    {
        INFO (entry.id << " " << entry.roman);
        const auto key = theory::parseKey (entry.key);
        REQUIRE (key.has_value());
        const auto chords = progressions::chords (entry, *key);
        CHECK (chords.size() == chordCount (entry.roman));
        for (const auto& chord : chords)
        {
            CHECK_FALSE (theory::chordName (chord, *key).empty());
            CHECK_FALSE (theory::roman (chord, *key).empty());
        }
    }
}

TEST_CASE ("Common progressions are found by genre and words", "[progressions]")
{
    CHECK (progressions::find ("", "Pop").size() == 8);
    CHECK (progressions::find ("jazz").size() == 8);
    CHECK (progressions::find ("blues").size() == 1);
    CHECK (progressions::find ("V7/vi").size() == 1);
    CHECK (progressions::find ("C minor").size() == 2);
    CHECK (progressions::find ("nothing here").empty());
}

TEST_CASE ("A common progression transposes", "[progressions]")
{
    const auto pop = progressions::find ("Pop: I–V–vi–IV");
    REQUIRE (pop.size() == 1);
    const auto key = theory::parseKey ("D major");
    REQUIRE (key.has_value());
    const auto chords = progressions::chords (pop.front(), *key);
    REQUIRE (chords.size() == 4);
    CHECK (theory::chordName (chords[0], *key) == "D");
    CHECK (theory::chordName (chords[1], *key) == "A");
    CHECK (theory::chordName (chords[2], *key) == "Bm");
    CHECK (theory::chordName (chords[3], *key) == "G");
}

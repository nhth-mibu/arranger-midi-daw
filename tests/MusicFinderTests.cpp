#include "arranger/core/MusicFinder.h"

#include <catch2/catch_test_macros.hpp>

using namespace arranger;

TEST_CASE ("A Music Finder book round-trips", "[music-finder]")
{
    music_finder::Book book;
    book.entries.push_back ({ "Song A", "SlowBossa.sty", 92.0, { "Piano", "Bass" }, "intro" });
    book.entries.push_back ({ "Song B", "Pop16Beat.sty", 126.0, {}, "" });
    const auto back = music_finder::parse (music_finder::write (book));
    REQUIRE (back.entries.size() == 2);
    CHECK (back.entries[0].name == "Song A");
    CHECK (back.entries[0].tempo == 92.0);
    CHECK (back.entries[0].voices == std::vector<std::string> { "Piano", "Bass" });
    CHECK (back.entries[1].style == "Pop16Beat.sty");
    CHECK_THROWS (music_finder::parse ("{}"));
    CHECK_THROWS (music_finder::parse ("not json"));
}

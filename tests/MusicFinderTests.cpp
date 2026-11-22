#include "arranger/core/MusicFinder.h"

#include <catch2/catch_test_macros.hpp>

using namespace arranger;

TEST_CASE ("A Music Finder book round-trips", "[music-finder]")
{
    music_finder::Book book;
    book.entries.push_back ({ "Song A", "SlowBossa.sty", 92.0, -1, 4, 4, { "Piano", "Bass" }, "intro" });
    book.entries.push_back ({ "Song B", "Pop16Beat.sty", 126.0, -1, 4, 4, {}, "" });
    const auto back = music_finder::parse (music_finder::write (book));
    REQUIRE (back.entries.size() == 2);
    CHECK (back.entries[0].name == "Song A");
    CHECK (back.entries[0].tempo == 92.0);
    CHECK (back.entries[0].voices == std::vector<std::string> { "Piano", "Bass" });
    CHECK (back.entries[1].style == "Pop16Beat.sty");
    CHECK_THROWS (music_finder::parse ("{}"));
    CHECK_THROWS (music_finder::parse ("not json"));
    const auto csv = music_finder::toCsv (book);
    CHECK (csv.find ("Song A,") != std::string::npos);
    book.entries[0].notes = "line one\nline two";
    CHECK (music_finder::toCsv (book).find ("line one line two") != std::string::npos);
    CHECK (csv.find ("Piano|Bass") != std::string::npos);
    const auto csvBack = music_finder::fromCsv (csv);
    REQUIRE (csvBack.entries.size() == 2);
    CHECK (csvBack.entries[0].name == "Song A");
    CHECK (csvBack.entries[0].voices == std::vector<std::string> { "Piano", "Bass" });
    CHECK_THROWS (music_finder::fromCsv ("x"));
}

TEST_CASE ("Yamaha Music Finder files become entries", "[music-finder]")
{
    std::string text = "MDB-100-100-3000PSR-S970";
    text += '\0';
    text += "v1.02";
    text += '\0';
    text.resize (36, '\0');
    text[34] = 0;
    text[35] = 2;
    const auto record = [] (const std::string& name) {
        std::string r (92, '\0');
        std::copy (name.begin(), name.end(), r.begin() + 11);
        const std::string genre = "Classical", keywords = "piano";
        std::copy (genre.begin(), genre.end(), r.begin() + 43);
        std::copy (keywords.begin(), keywords.end(), r.begin() + 59);
        return r;
    };
    text += record ("Song A");
    text += record ("Song B");
    const auto book = music_finder::fromMfd (text);
    REQUIRE (book.entries.size() == 2);
    CHECK (book.entries[0].name == "Song A");
    CHECK (book.entries[0].notes == "Classical, piano");
    CHECK (book.entries[0].styleNumber == 0);
    CHECK_THROWS (music_finder::fromMfd ("nope"));
}

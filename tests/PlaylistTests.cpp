#include "arranger/core/Playlist.h"

#include <catch2/catch_test_macros.hpp>

using namespace arranger;

TEST_CASE ("A Yamaha Playlist round-trips", "[playlist]")
{
    playlist::List list;
    list.items.push_back ({ "Song A", "I:/Playlist/A.rgt", 0, false });
    list.items.push_back ({ "Song B", "I:/Playlist/B.rgt", 2, true });
    const auto parsed = playlist::parse (playlist::write (list));
    REQUIRE (parsed.items.size() == 2);
    CHECK (parsed.items[0].title == "Song A");
    CHECK (parsed.items[1].regist == 2);
    CHECK (parsed.items[1].openViewer);
    CHECK (playlist::write (list).find ("TITLE\tFILEPATH") == 0);
    CHECK_THROWS (playlist::parse ("nope"));
    CHECK_THROWS (playlist::parse ("TITLE\tFILEPATH\tREGIST\tOPENVIEWER\r\nbad\r\n"));
}

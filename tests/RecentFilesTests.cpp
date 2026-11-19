#include "arranger/core/RecentFiles.h"

#include <catch2/catch_test_macros.hpp>

using arranger::RecentFiles;

TEST_CASE ("Recent files keep the newest first, without duplicates")
{
    RecentFiles list (3);
    list.add ("/a.arranger");
    list.add ("/b.mid");
    list.add ("/a.arranger");
    REQUIRE (list.items() == std::vector<std::string> { "/a.arranger", "/b.mid" });

    list.add ("/c.mid");
    list.add ("/d.mid");
    CHECK (list.items() == std::vector<std::string> { "/d.mid", "/c.mid", "/a.arranger" });

    list.add ("");
    CHECK (list.items().size() == 3);
}

TEST_CASE ("Recent files drop missing files and can be cleared")
{
    RecentFiles list;
    list.add ("/gone.mid");
    list.add ("/here.mid");
    list.removeMissing ([] (const std::string& p) { return p == "/here.mid"; });
    CHECK (list.items() == std::vector<std::string> { "/here.mid" });
    list.clear();
    CHECK (list.empty());
}

TEST_CASE ("Recent files round-trip through text and respect the limit")
{
    RecentFiles list (10);
    for (int i = 0; i < 12; ++i)
        list.add ("/song " + std::to_string (i) + ".arranger");
    REQUIRE (list.items().size() == 10);
    CHECK (list.items().front() == "/song 11.arranger");

    const auto back = RecentFiles::fromString (list.toString(), 10);
    CHECK (back.items() == list.items());

    const auto windows = RecentFiles::fromString ("C:\\a.mid\r\nC:\\a.mid\r\n\r\nC:\\b.mid\r\n", 5);
    CHECK (windows.items() == std::vector<std::string> { "C:\\a.mid", "C:\\b.mid" });

    auto shrink = back;
    shrink.setMaxItems (5);
    CHECK (shrink.items().size() == 5);
}

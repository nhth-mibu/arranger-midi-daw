// Real style files: read, write and read again, and nothing changes.
//
// Yamaha's styles can't go in the repository, so this test reads a folder you
// give it: set ARRANGER_STYLE_SAMPLES to a folder of .sty/.prs files (for
// example the style folder on the NAS) and run
//   arranger_tests "[stylesamples]"
// Without the variable it checks only the demo style.

#include "KitchenSink.h"

#include "arranger/core/Style.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace arranger;
namespace fs = std::filesystem;

namespace
{
    std::vector<std::uint8_t> readBytes (const fs::path& p)
    {
        std::ifstream in (p, std::ios::binary);
        return { std::istreambuf_iterator<char> (in), {} };
    }

    std::vector<fs::path> sampleStyles()
    {
        std::vector<fs::path> out { test::examplesFolder() / "demo-style.sty" };
        if (const char* folder = std::getenv ("ARRANGER_STYLE_SAMPLES"))
            for (const auto& e : fs::recursive_directory_iterator (folder, fs::directory_options::skip_permission_denied))
                if (e.is_regular_file() && style_file::hasStyleExtension (e.path().filename().string()))
                    out.push_back (e.path());
        return out;
    }
}

TEST_CASE ("Real styles survive a write and read with every part, rule and setting", "[style][stylesamples]")
{
    int checked = 0, refused = 0;
    for (const auto& file : sampleStyles())
    {
        INFO (file.string());
        const auto original = readBytes (file);
        Style style;
        try
        {
            style = style_file::read (original);
        }
        catch (const style_file::Error& e)
        {
            // A file the reader refuses must say why; that's not a test failure, but it is counted.
            UNSCOPED_INFO ("refused: " << e.what());
            ++refused;
            continue;
        }
        const auto written = style_file::write (style);
        const auto back = style_file::read (written);

        CHECK (back.name == style.name);
        CHECK (back.format == style.format);
        CHECK (back.timeSignature == style.timeSignature);
        CHECK (std::abs (back.tempo - style.tempo) < 0.01);
        CHECK (back.setup == style.setup);
        REQUIRE (back.sections.size() == style.sections.size());
        for (std::size_t i = 0; i < style.sections.size(); ++i)
        {
            INFO ("section " << style.sections[i].name);
            CHECK (back.sections[i] == style.sections[i]);
        }
        CHECK (back.ruleSets == style.ruleSets);
        // One Touch Settings and Music Finder data go back exactly as they came.
        CHECK (back.extra == style.extra);
        if (! style.extra.empty())
            CHECK (std::search (original.begin(), original.end(), style.extra.begin(), style.extra.end()) != original.end());
        // And a second write gives the same bytes.
        CHECK (style_file::write (back) == written);
        ++checked;
    }
    INFO (checked << " styles checked, " << refused << " refused");
    CHECK (checked > 0);
    CHECK (refused == 0);
}

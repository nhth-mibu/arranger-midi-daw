#include "arranger/core/ProjectFile.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>

using namespace arranger;
using Catch::Matchers::ContainsSubstring;

namespace
{
    Song demoSong()
    {
        Song s;
        s.title = "Late Night Sketch";
        s.tempoBpm = 112.0;
        s.timeSignature = { 6, 8 };
        s.key = "C minor";
        s.tracks = { { "Drums", "coral", 10 }, { "Strings", "teal", 3 } };
        s.metronome = { false, 0.4f, false };
        return s;
    }
}

TEST_CASE ("A song survives a save and load unchanged")
{
    const auto song = demoSong();
    CHECK (project_file::fromJson (project_file::toJson (song)) == song);
}

TEST_CASE ("Saving to disk and loading back")
{
    const auto path = std::filesystem::temp_directory_path() / "arranger-test-project.arranger";
    const auto song = demoSong();

    project_file::save (song, path);
    project_file::save (song, path); // overwriting an existing file works too
    CHECK (project_file::load (path) == song);
    CHECK_FALSE (std::filesystem::exists (std::filesystem::path (path) += ".saving"));

    std::filesystem::remove (path);
}

TEST_CASE ("The file is readable JSON")
{
    const auto json = project_file::toJson (demoSong());
    CHECK_THAT (json, ContainsSubstring ("\"format\": \"arranger-midi-daw-project\""));
    CHECK_THAT (json, ContainsSubstring ("\"timeSignature\": [\n      6,\n      8\n    ]"));
}

TEST_CASE ("Missing fields fall back to defaults")
{
    const auto song = project_file::fromJson (R"({"format":"arranger-midi-daw-project","version":1,"song":{"title":"Sketch"}})");
    CHECK (song.title == "Sketch");
    CHECK (song.tempoBpm == 120.0);
    CHECK (song.timeSignature == TimeSignature {});
    CHECK (song.tracks.empty());
}

TEST_CASE ("Out-of-range values are clamped")
{
    const auto song = project_file::fromJson (R"({"format":"arranger-midi-daw-project","version":1,
        "song":{"tempoBpm":9000,"tracks":[{"name":"X","midiChannel":40}],"metronome":{"level":3}}})");
    CHECK (song.tempoBpm == kMaxTempoBpm);
    CHECK (song.tracks.at (0).midiChannel == 16);
    CHECK (song.metronome.level == 1.0f);
}

TEST_CASE ("Bad files are refused with a clear message")
{
    using project_file::Error;
    using project_file::fromJson;

    CHECK_THROWS_AS (fromJson ("not json"), Error);
    CHECK_THROWS_WITH (fromJson (R"({"format":"something-else","version":1})"), ContainsSubstring ("Not an Arranger"));
    CHECK_THROWS_WITH (fromJson (R"({"format":"arranger-midi-daw-project","version":99,"song":{}})"), ContainsSubstring ("newer version"));
    CHECK_THROWS_AS (fromJson (R"({"format":"arranger-midi-daw-project","version":1,"song":{"timeSignature":[4,3]}})"), Error);
    CHECK_THROWS_AS (fromJson (R"({"format":"arranger-midi-daw-project","version":1,"song":{"tempoBpm":"fast"}})"), Error);
    CHECK_THROWS_AS (project_file::load ("/no/such/file.arranger"), Error);
}

#include "arranger/core/ProjectFile.h"
#include "arranger/core/SongEdit.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <algorithm>
#include <fstream>

using namespace arranger;
using Catch::Matchers::ContainsSubstring;

namespace
{
    Song demoSong()
    {
        Song s;
        s.title = "Late Night Sketch";
        s.tempo.setPoints ({ { 0, 112.0 }, { 3840 * 8, 96.5 } });
        s.timeSignature = { 6, 8 };
        s.key = "C minor";
        s.metronome = { false, 0.4f, false };
        s.loop = { true, 0, 3840 * 4 };
        s.master.gainDb = -3.0f;
        s.soundFont = "/Users/me/SoundFonts/GeneralUser.sf2";

        auto& drums = edit::addTrack (s, "Drums");
        drums.output.channel = 10;
        drums.mixer = { 90, 50, 30, 10, false, true };
        auto& strings = edit::addTrack (s, "Strings \xc3\xa9");
        strings.output = { TrackOutput::Kind::midiPort, "PSR-S970 Port 1", 3 };
        strings.sound = { "Strings > Orchestra", 0, 117, 48 };
        strings.armed = true;
        strings.inputPort = "Launchkey Mini MK3 MIDI Port";
        strings.inputChannel = 2;
        const auto stringsId = strings.id;

        Clip c;
        c.start = 3840;
        c.length = 3840 * 2;
        c.notes = { { 0, 960, 60, 100 }, { 960, 480, 64, 90 } };
        c.controls = { { 0, ControlEvent::Kind::controller, 1, 64 }, { 480, ControlEvent::Kind::pitchBend, 0, -2000 } };
        edit::addClip (s, stringsId, c);
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
    CHECK (song.baseTempo() == 120.0);
    CHECK (song.timeSignature == TimeSignature {});
    CHECK (song.tracks.empty());
}

TEST_CASE ("Out-of-range values are clamped")
{
    const auto song = project_file::fromJson (R"({"format":"arranger-midi-daw-project","version":1,
        "song":{"tempoBpm":9000,"tracks":[{"name":"X","midiChannel":40}],"metronome":{"level":3}}})");
    // (a version 1 file: tempoBpm and midiChannel are read and migrated)
    CHECK (song.baseTempo() == kMaxTempoBpm);
    CHECK (song.tracks.at (0).output.channel == 16);
    CHECK (song.metronome.level == 1.0f);
}

TEST_CASE ("Bad files are refused with a clear message")
{
    using project_file::Error;
    using project_file::fromJson;

    CHECK_THROWS_AS (fromJson ("not json"), Error);
    CHECK_THROWS_WITH (fromJson (R"({"format":"something-else","version":1})"), ContainsSubstring ("Not an Arranger"));
    CHECK_THROWS_WITH (fromJson (R"({"format":"arranger-midi-daw-project","version":99,"song":{}})"), ContainsSubstring ("newer version"));
    CHECK_THROWS_AS (fromJson (R"({"format":"arranger-midi-daw-project","version":2,"song":{"tracks":[{"clips":[{"notes":[[1,2]]}]}]}})"), Error);
    CHECK_THROWS_AS (fromJson (R"({"format":"arranger-midi-daw-project","version":1,"song":{"timeSignature":[4,3]}})"), Error);
    CHECK_THROWS_AS (fromJson (R"({"format":"arranger-midi-daw-project","version":1,"song":{"tempoBpm":"fast"}})"), Error);
    CHECK_THROWS_AS (project_file::load ("/no/such/file.arranger"), Error);
}

TEST_CASE ("Version 1 files from Phase 0 still open")
{
    const auto song = project_file::fromJson (R"({"format":"arranger-midi-daw-project","version":1,
        "song":{"title":"Old","tempoBpm":112,"timeSignature":[3,4],"tracks":[{"name":"Drums","colour":"coral","midiChannel":10}]}})");
    CHECK (song.baseTempo() == 112.0);
    REQUIRE (song.tracks.size() == 1);
    CHECK (song.tracks[0].output.channel == 10);
    CHECK (song.tracks[0].id != 0);
    CHECK (song.nextId > song.tracks[0].id);
}

TEST_CASE ("Duplicate ids in a file are repaired")
{
    const auto song = project_file::fromJson (R"({"format":"arranger-midi-daw-project","version":2,
        "song":{"tracks":[{"id":5,"clips":[{"id":5},{"id":5}]},{"id":5}]}})");
    std::vector<std::uint32_t> ids;
    for (const auto& t : song.tracks)
    {
        ids.push_back (t.id);
        for (const auto& c : t.clips)
            ids.push_back (c->id);
    }
    std::sort (ids.begin(), ids.end());
    CHECK (std::adjacent_find (ids.begin(), ids.end()) == ids.end());
    CHECK (song.nextId > ids.back());
}

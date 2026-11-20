#include "KitchenSink.h"

#include "arranger/core/ProjectFile.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace arranger;
namespace fs = std::filesystem;

namespace
{
    std::string readFile (const fs::path& p)
    {
        std::ifstream in (p, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }
}

TEST_CASE ("The kitchen-sink song uses every part of the model", "[roundtrip]")
{
    const auto s = test::kitchenSinkSong();
    CHECK (s.style.has_value());
    CHECK (s.buses.size() == 2);
    CHECK_FALSE (s.master.effects.empty());
    bool audio = false, sampler = false, plugin = false, port = false, slot = false, audioSlot = false;
    for (const auto& t : s.tracks)
    {
        audio = audio || ! t.audio.empty();
        sampler = sampler || ! t.kit.pads.empty();
        plugin = plugin || ! t.instrument.empty();
        port = port || t.output.kind == TrackOutput::Kind::midiPort;
        for (const auto& c : t.slots)
            slot = slot || c != nullptr;
        for (const auto& c : t.audioSlots)
            audioSlot = audioSlot || c != nullptr;
    }
    CHECK (audio);
    CHECK (sampler);
    CHECK (plugin);
    CHECK (port);
    CHECK (slot);
    CHECK (audioSlot);
}

TEST_CASE ("Every part of the song survives a save and load", "[roundtrip]")
{
    const auto song = test::kitchenSinkSong();
    const auto back = project_file::fromJson (project_file::toJson (song));
    CHECK (back == song);

    // Field by field, so a failure says where.
    CHECK (back.title == song.title);
    CHECK (back.tempo.getPoints().size() == song.tempo.getPoints().size());
    CHECK (back.style == song.style);
    CHECK (back.buses == song.buses);
    CHECK (back.master == song.master);
    CHECK (back.scenes == song.scenes);
    REQUIRE (back.tracks.size() == song.tracks.size());
    for (std::size_t i = 0; i < song.tracks.size(); ++i)
    {
        INFO ("track " << song.tracks[i].name);
        CHECK (back.tracks[i] == song.tracks[i]);
        CHECK (back.tracks[i].effects == song.tracks[i].effects);
        CHECK (back.tracks[i].kit == song.tracks[i].kit);
        CHECK (back.tracks[i].marks == song.tracks[i].marks);
    }
}

TEST_CASE ("Save, load and save again gives the same file", "[roundtrip]")
{
    const auto folder = fs::temp_directory_path() / "arranger-roundtrip";
    fs::create_directories (folder);
    const auto a = folder / "a.arranger";
    const auto b = folder / "b.arranger";

    project_file::save (test::kitchenSinkSong(), a);
    project_file::save (project_file::load (a), b);
    const auto first = readFile (a);
    const auto second = readFile (b);
    CHECK (first == second);
    if (first != second)
    {
        std::size_t at = 0;
        while (at < first.size() && at < second.size() && first[at] == second[at])
            ++at;
        FAIL_CHECK ("sizes " << first.size() << " and " << second.size() << ", first difference at " << at << ": ..."
                    << first.substr (at > 60 ? at - 60 : 0, 140) << " ... versus ..." << second.substr (at > 60 ? at - 60 : 0, 140));
    }

    fs::remove_all (folder);
}

TEST_CASE ("The example projects of every release still open", "[roundtrip]")
{
    // The examples folder holds files saved by earlier versions (file version 3).
    // Each must open, and a save and load of it must keep it unchanged.
    int opened = 0;
    for (const auto& entry : fs::recursive_directory_iterator (test::examplesFolder()))
    {
        if (entry.path().extension() != project_file::kExtension)
            continue;
        INFO (entry.path().string());
        const auto song = project_file::load (entry.path());
        CHECK_FALSE (song.tracks.empty());
        CHECK (project_file::fromJson (project_file::toJson (song)) == song);
        ++opened;
    }
    CHECK (opened >= 5);
}

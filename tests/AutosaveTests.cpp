#include "arranger/core/Autosave.h"
#include "arranger/core/ProjectFile.h"
#include "arranger/core/SongEdit.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using namespace arranger;
namespace fs = std::filesystem;

namespace
{
    struct TempFolder
    {
        fs::path path;
        explicit TempFolder (const std::string& name) : path (fs::temp_directory_path() / name)
        {
            fs::remove_all (path);
        }
        ~TempFolder()
        {
            std::error_code ec;
            fs::remove_all (path, ec);
        }
    };

    Document editedDocument()
    {
        Document doc;
        doc.edit ("Add Track", [] (Song& s) {
            s.title = "Crash Test";
            auto& t = edit::addTrack (s, "Piano");
            Clip c;
            c.length = 3840;
            c.notes = { { 0, 480, 60, 100 } };
            edit::addClip (s, t.id, c);
        });
        return doc;
    }

    /** Makes an Autosaver's copy look like it was left by another app, as after a crash. */
    void pretendWrittenBy (const autosave::Autosaver& saver, std::int64_t pid, std::int64_t heartbeat)
    {
        std::ifstream in (saver.metaPath());
        auto meta = nlohmann::json::parse (in);
        in.close();
        meta["pid"] = pid;
        meta["heartbeat"] = heartbeat;
        std::ofstream (saver.metaPath()) << meta.dump();
    }

    constexpr std::int64_t kDeadPid = 2'000'000'000; // above any real process id
}

TEST_CASE ("Autosave waits for the interval, then writes the unsaved song", "[autosave]")
{
    TempFolder folder ("arranger-autosave-interval");
    autosave::Autosaver saver (folder.path, { 60'000, 30'000 });
    auto doc = editedDocument();

    CHECK_FALSE (saver.tick (doc, "", 1'000));
    CHECK_FALSE (saver.tick (doc, "", 60'000));
    CHECK_FALSE (fs::exists (saver.projectPath()));

    CHECK (saver.tick (doc, "", 61'000));
    REQUIRE (fs::exists (saver.projectPath()));
    REQUIRE (fs::exists (saver.metaPath()));
    CHECK (project_file::load (saver.projectPath()) == doc.song());

    // Nothing new: no second write.
    CHECK_FALSE (saver.tick (doc, "", 200'000));
}

TEST_CASE ("A long run of edits can't hold off the autosave", "[autosave]")
{
    TempFolder folder ("arranger-autosave-busy");
    autosave::Autosaver saver (folder.path, { 60'000, 30'000 });
    auto doc = editedDocument();

    bool wrote = false;
    for (std::int64_t t = 0; t <= 61'000; t += 1'000)
    {
        doc.edit ("Volume", [t] (Song& s) { s.tracks[0].mixer.volume = static_cast<int> (t / 1000) % 127; });
        wrote = saver.tick (doc, "", t) || wrote;
    }
    CHECK (wrote);
}

TEST_CASE ("A later edit is written one interval after the last copy", "[autosave]")
{
    TempFolder folder ("arranger-autosave-later");
    autosave::Autosaver saver (folder.path, { 60'000, 30'000 });
    auto doc = editedDocument();
    CHECK_FALSE (saver.tick (doc, "", 0)); // the app sees the change
    REQUIRE (saver.tick (doc, "", 60'000));

    doc.edit ("Tempo", [] (Song& s) { s.title = "Changed"; });
    CHECK_FALSE (saver.tick (doc, "", 90'000));
    CHECK (saver.tick (doc, "", 120'000));
    CHECK (project_file::load (saver.projectPath()).title == "Changed");
}

TEST_CASE ("Saving the song or undoing back to the saved version removes the copy", "[autosave]")
{
    TempFolder folder ("arranger-autosave-saved");
    autosave::Autosaver saver (folder.path, { 1'000, 30'000 });
    auto doc = editedDocument();
    doc.markSaved();
    doc.edit ("Rename", [] (Song& s) { s.title = "New"; });

    saver.tick (doc, "/music/song.arranger", 0);
    REQUIRE (saver.tick (doc, "/music/song.arranger", 5'000));
    REQUIRE (saver.hasCopy());

    doc.undo(); // back to the saved version
    saver.tick (doc, "/music/song.arranger", 6'000);
    CHECK_FALSE (saver.hasCopy());
    CHECK_FALSE (fs::exists (saver.projectPath()));
    CHECK_FALSE (fs::exists (saver.metaPath()));

    doc.redo();
    saver.tick (doc, "/music/song.arranger", 7'000);
    REQUIRE (saver.tick (doc, "/music/song.arranger", 8'000));
    doc.markSaved();
    saver.tick (doc, "/music/song.arranger", 8'100);
    CHECK_FALSE (fs::exists (saver.projectPath()));
}

TEST_CASE ("A clean quit leaves nothing to recover", "[autosave]")
{
    TempFolder folder ("arranger-autosave-quit");
    {
        autosave::Autosaver saver (folder.path, { 0, 30'000 });
        auto doc = editedDocument();
        REQUIRE (saver.tick (doc, "", 1));
        saver.discard();
    }
    CHECK (fs::is_empty (folder.path));
    CHECK (autosave::findOrphans (folder.path, 10'000'000).empty());
}

TEST_CASE ("A copy left by an app that crashed can be recovered", "[autosave]")
{
    TempFolder folder ("arranger-autosave-crash");
    auto doc = editedDocument();
    const auto now = autosave::nowMs();
    {
        autosave::Autosaver saver (folder.path, { 0, 30'000 });
        REQUIRE (saver.tick (doc, "/music/My Song \xc3\xa9.arranger", now));
        pretendWrittenBy (saver, kDeadPid, now); // the app "crashes": nothing removes the copy
    }

    const auto orphans = autosave::findOrphans (folder.path, now + 1'000);
    REQUIRE (orphans.size() == 1);
    CHECK (orphans[0].original == "/music/My Song \xc3\xa9.arranger");
    CHECK (orphans[0].title == "Crash Test");
    CHECK (orphans[0].savedAtMs == now);
    CHECK (autosave::recover (orphans[0]) == doc.song());

    autosave::remove (orphans[0]);
    CHECK (autosave::findOrphans (folder.path, now + 2'000).empty());
    CHECK (fs::is_empty (folder.path));
}

TEST_CASE ("A copy of an app that still runs is not offered", "[autosave]")
{
    TempFolder folder ("arranger-autosave-running");
    auto doc = editedDocument();
    const auto now = autosave::nowMs();
    autosave::Autosaver saver (folder.path, { 0, 30'000 });
    REQUIRE (saver.tick (doc, "", now));

    // This process wrote it.
    CHECK (autosave::findOrphans (folder.path, now).empty());

    CHECK (autosave::processIsRunning (autosave::currentProcessId()));
    CHECK_FALSE (autosave::processIsRunning (kDeadPid));
    CHECK_FALSE (autosave::processIsRunning (0));
}

TEST_CASE ("A running process with an old heartbeat is a reused id: its copy is offered", "[autosave]")
{
    TempFolder folder ("arranger-autosave-stale");
    auto doc = editedDocument();
    const auto now = autosave::nowMs();
    {
        autosave::Autosaver saver (folder.path, { 0, 30'000 });
        REQUIRE (saver.tick (doc, "", now));
        // pid 1 always runs (init on Linux and macOS; System on Windows is 4, which also runs)
#if defined(_WIN32)
        pretendWrittenBy (saver, 4, now - 10 * 60'000);
#else
        pretendWrittenBy (saver, 1, now - 10 * 60'000);
#endif
    }
    CHECK (autosave::findOrphans (folder.path, now).size() == 1);
}

TEST_CASE ("The heartbeat stays fresh while the app runs", "[autosave]")
{
    TempFolder folder ("arranger-autosave-heartbeat");
    auto doc = editedDocument();
    autosave::Autosaver saver (folder.path, { 0, 30'000 });
    REQUIRE (saver.tick (doc, "", 1'000));
    saver.tick (doc, "", 40'000);

    std::ifstream in (saver.metaPath());
    const auto meta = nlohmann::json::parse (in);
    CHECK (meta["heartbeat"] == 40'000);
    CHECK (meta["savedAt"] == 1'000);
}

TEST_CASE ("Broken, half-written and very old copies are never offered", "[autosave]")
{
    TempFolder folder ("arranger-autosave-broken");
    fs::create_directories (folder.path);
    const auto now = autosave::nowMs();

    // A .json without its song (someone deleted one half).
    std::ofstream (folder.path / "autosave-1-1.json") << R"({"format":"arranger-midi-daw-autosave","pid":2000000000,"savedAt":1})";
    // Not JSON.
    std::ofstream (folder.path / "autosave-2-2.json") << "{ half";
    std::ofstream (folder.path / "autosave-2-2.arranger") << "{}";
    // Somebody else's file.
    std::ofstream (folder.path / "notes.json") << "{}";

    // Too old: removed.
    {
        autosave::Autosaver saver (folder.path, { 0, 30'000 });
        auto doc = editedDocument();
        REQUIRE (saver.tick (doc, "", now - 40LL * 24 * 3600 * 1000));
        pretendWrittenBy (saver, kDeadPid, now - 40LL * 24 * 3600 * 1000);
        CHECK (autosave::findOrphans (folder.path, now).empty());
        CHECK_FALSE (fs::exists (saver.projectPath()));
    }
    CHECK (fs::exists (folder.path / "notes.json"));
}

TEST_CASE ("Several crashed copies come newest first", "[autosave]")
{
    TempFolder folder ("arranger-autosave-several");
    const auto now = autosave::nowMs();
    for (int i = 0; i < 3; ++i)
    {
        autosave::Autosaver saver (folder.path, { 0, 30'000 });
        auto doc = editedDocument();
        doc.edit ("Rename", [i] (Song& s) { s.title = "Take " + std::to_string (i); });
        REQUIRE (saver.tick (doc, "", now + i * 1'000));
        pretendWrittenBy (saver, kDeadPid, now + i * 1'000);
    }
    const auto orphans = autosave::findOrphans (folder.path, now + 10'000);
    REQUIRE (orphans.size() == 3);
    CHECK (orphans[0].title == "Take 2");
    CHECK (orphans[2].title == "Take 0");
}

TEST_CASE ("A folder that can't be written is reported, not thrown", "[autosave]")
{
    TempFolder folder ("arranger-autosave-blocked");
    fs::create_directories (folder.path.parent_path());
    std::ofstream (folder.path) << "a file where the folder should be";

    autosave::Autosaver saver (folder.path, { 0, 30'000 });
    auto doc = editedDocument();
    CHECK_FALSE (saver.tick (doc, "", 1'000));
    CHECK_FALSE (saver.lastError().empty());
    CHECK_FALSE (saver.hasCopy());
    std::error_code ec;
    fs::remove (folder.path, ec);
}

TEST_CASE ("A recovered song counts as unsaved", "[autosave]")
{
    Document doc;
    doc.reset (editedDocument().song());
    CHECK_FALSE (doc.isModified());
    doc.markUnsaved();
    CHECK (doc.isModified());
    doc.markSaved();
    CHECK_FALSE (doc.isModified());
}

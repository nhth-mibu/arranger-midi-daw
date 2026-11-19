#include "arranger/core/Autosave.h"

#include "arranger/core/ProjectFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#else
  #include <cerrno>
  #include <csignal>
  #include <sys/types.h>
  #include <unistd.h>
#endif

namespace arranger::autosave
{

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
    constexpr const char* kMetaFormat = "arranger-midi-daw-autosave";

    std::string fileName (const fs::path& p)
    {
        const auto u8 = p.u8string();
        return std::string (u8.begin(), u8.end());
    }

    bool writeTextAtomically (const fs::path& path, const std::string& text)
    {
        auto temp = path;
        temp += ".saving";
        {
            std::ofstream out (temp, std::ios::binary | std::ios::trunc);
            if (! out)
                return false;
            out << text;
            out.flush();
            if (! out)
                return false;
        }
        std::error_code ec;
        fs::rename (temp, path, ec);
        if (ec)
        {
            fs::remove (temp, ec);
            return false;
        }
        return true;
    }

    std::string readText (const fs::path& path)
    {
        std::ifstream in (path, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }
}

std::int64_t nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds> (system_clock::now().time_since_epoch()).count();
}

std::int64_t currentProcessId()
{
#if defined(_WIN32)
    return static_cast<std::int64_t> (GetCurrentProcessId());
#else
    return static_cast<std::int64_t> (getpid());
#endif
}

bool processIsRunning (std::int64_t pid)
{
    if (pid <= 0)
        return false;
#if defined(_WIN32)
    HANDLE process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD> (pid));
    if (process == nullptr)
        return GetLastError() == ERROR_ACCESS_DENIED; // it exists, but belongs to someone else
    DWORD code = 0;
    const bool running = GetExitCodeProcess (process, &code) != 0 && code == STILL_ACTIVE;
    CloseHandle (process);
    return running;
#else
    if (kill (static_cast<pid_t> (pid), 0) == 0)
        return true;
    return errno == EPERM; // it exists, but belongs to someone else
#endif
}

// ---- Autosaver ---------------------------------------------------------------

Autosaver::Autosaver (fs::path f, Options o) : folder (std::move (f)), options (o)
{
    // The id is unique per run: the process id, and the start time for when an id is reused.
    // A counter tells apart several in one run (tests), even within one millisecond.
    static std::atomic<int> counter { 0 };
    const auto id = "autosave-" + std::to_string (currentProcessId()) + "-" + std::to_string (nowMs()) + "-" + std::to_string (counter++);
    projectFile = folder / (id + project_file::kExtension);
    metaFile = folder / (id + ".json");
}

bool Autosaver::writeMeta (std::int64_t savedAt, std::int64_t now)
{
    const json meta = { { "format", kMetaFormat },
                        { "version", 1 },
                        { "pid", currentProcessId() },
                        { "original", original },
                        { "title", title },
                        { "savedAt", savedAt },
                        { "heartbeat", now } };
    if (! writeTextAtomically (metaFile, meta.dump (2)))
    {
        error = "Can't write " + fileName (metaFile);
        return false;
    }
    lastHeartbeatMs = now;
    return true;
}

bool Autosaver::saveNow (const Song& song, std::uint64_t revision, const std::string& originalFile, std::int64_t now)
{
    lastWriteMs = now; // also after a failure: try again one interval later, not on every tick
    std::error_code ec;
    fs::create_directories (folder, ec);
    try
    {
        // The song first, then the .json: a pair without its .json is never offered.
        project_file::save (song, projectFile);
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
    original = originalFile;
    title = song.title;
    savedAtMs = now;
    written = true;
    if (! writeMeta (now, now))
        return false;
    writtenRevision = revision;
    error.clear();
    return true;
}

bool Autosaver::tick (const Document& doc, const std::string& originalFile, std::int64_t now)
{
    if (! doc.isModified())
    {
        firstDirtyMs = -1;
        if (written)
            discard();
        return false;
    }

    if (firstDirtyMs < 0)
        firstDirtyMs = now;

    // Write when the copy is behind and changes have waited an interval. The interval
    // counts from the first unsaved change, so a long run of edits can't hold off the save.
    const bool behind = ! written || doc.revision() != writtenRevision || originalFile != original;
    const auto since = std::max (firstDirtyMs, lastWriteMs);
    if (behind && now - since >= options.intervalMs)
    {
        const bool ok = saveNow (doc.song(), doc.revision(), originalFile, now);
        firstDirtyMs = now;
        return ok;
    }

    if (written && now - lastHeartbeatMs >= options.heartbeatMs)
        writeMeta (savedAtMs, now);
    return false;
}

void Autosaver::discard()
{
    std::error_code ec;
    fs::remove (metaFile, ec); // the .json first: a lone .arranger is never offered
    fs::remove (projectFile, ec);
    written = false;
    writtenRevision = 0;
}

// ---- Recovery ----------------------------------------------------------------

std::vector<Entry> findOrphans (const fs::path& folder, std::int64_t now, std::int64_t staleMs, std::int64_t maxAgeMs)
{
    std::vector<Entry> found;
    std::error_code ec;
    if (! fs::is_directory (folder, ec))
        return found;

    const auto self = currentProcessId();
    for (fs::directory_iterator it (folder, ec), end; ! ec && it != end; it.increment (ec))
    {
        const auto& path = it->path();
        if (path.extension() != ".json" || fileName (path.filename()).rfind ("autosave-", 0) != 0)
            continue;

        Entry e;
        e.meta = path;
        e.project = path;
        e.project.replace_extension (project_file::kExtension);
        try
        {
            const auto meta = json::parse (readText (path));
            if (meta.value ("format", std::string()) != kMetaFormat)
                continue;
            e.pid = meta.value ("pid", std::int64_t (0));
            e.original = meta.value ("original", std::string());
            e.title = meta.value ("title", std::string());
            e.savedAtMs = meta.value ("savedAt", std::int64_t (0));
            e.heartbeatMs = meta.value ("heartbeat", std::int64_t (0));
        }
        catch (const std::exception&)
        {
            continue; // half written or not ours
        }

        if (! fs::is_regular_file (e.project, ec))
            continue;
        if (now - e.savedAtMs > maxAgeMs)
        {
            remove (e);
            continue;
        }
        const bool owned = e.pid == self || (processIsRunning (e.pid) && now - e.heartbeatMs < staleMs);
        if (! owned)
            found.push_back (std::move (e));
    }

    std::sort (found.begin(), found.end(), [] (const Entry& a, const Entry& b) { return a.savedAtMs > b.savedAtMs; });
    return found;
}

Song recover (const Entry& e)
{
    return project_file::load (e.project);
}

void remove (const Entry& e)
{
    std::error_code ec;
    fs::remove (e.meta, ec);
    fs::remove (e.project, ec);
}

} // namespace arranger::autosave

#pragma once

#include "arranger/core/Document.h"
#include "arranger/core/Song.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace arranger
{

/**
    Autosave and crash recovery.

    While the app runs, an Autosaver writes a copy of the song with unsaved
    changes to its own pair of files in the autosave folder:

        <id>.arranger   the song, as a normal project file
        <id>.json       who wrote it: process id, original project, title, times

    A save or a clean quit removes the pair. So a pair that is still there
    when the app starts, and whose process is no longer running, is work that
    a crash (or a power cut) left behind: findOrphans() lists it.
*/
namespace autosave
{
    struct Options
    {
        std::int64_t intervalMs = 60'000;  ///< the longest time unsaved changes stay unsaved
        std::int64_t heartbeatMs = 30'000; ///< how often the .json is touched while the app runs
    };

    /** One left-over copy that can be recovered. */
    struct Entry
    {
        std::filesystem::path project; ///< the .arranger copy
        std::filesystem::path meta;    ///< its .json
        std::string original;          ///< the project file the work belongs to; empty for a new song
        std::string title;
        std::int64_t savedAtMs = 0;    ///< when the copy was written (ms since 1970)
        std::int64_t heartbeatMs = 0;  ///< the last sign of life of the app that wrote it
        std::int64_t pid = 0;
    };

    /** Milliseconds since 1970, from the system clock. */
    std::int64_t nowMs();

    /** This process's id. */
    std::int64_t currentProcessId();

    /** True when a process with this id runs now. */
    bool processIsRunning (std::int64_t pid);

    class Autosaver
    {
    public:
        /** Creates nothing on disk until there is work to save. */
        Autosaver (std::filesystem::path folder, Options = {});

        /**
            Call often (the app calls it from its UI timer). Writes the song when
            it has unsaved changes that are not in the copy yet and the interval
            has passed; removes the copy when the song is saved (or undone back
            to its saved state); keeps the heartbeat fresh. Returns true when it
            wrote the song. Throws nothing: a failed write is retried later and
            reported by lastError().
        */
        bool tick (const Document&, const std::string& originalFile, std::int64_t now);

        /** Writes the song now, whatever the interval. False if it failed. */
        bool saveNow (const Song&, std::uint64_t revision, const std::string& originalFile, std::int64_t now);

        /** Removes this app's copy (after a save, on a clean quit). */
        void discard();

        bool hasCopy() const noexcept { return written; }
        const std::filesystem::path& projectPath() const noexcept { return projectFile; }
        const std::filesystem::path& metaPath() const noexcept { return metaFile; }
        const std::string& lastError() const noexcept { return error; }

    private:
        bool writeMeta (std::int64_t savedAt, std::int64_t now);

        std::filesystem::path folder, projectFile, metaFile;
        Options options;
        bool written = false;
        std::uint64_t writtenRevision = 0;
        std::int64_t lastWriteMs = 0, lastHeartbeatMs = 0, savedAtMs = 0, firstDirtyMs = -1;
        std::string original, title, error;
    };

    /**
        The copies in `folder` that no running app owns: the process that wrote
        them has ended, or (if its id was reused) it has given no sign of life
        for `staleMs`. Newest first. Pairs with a missing or broken half are left
        out; copies older than `maxAgeMs` are removed.
    */
    std::vector<Entry> findOrphans (const std::filesystem::path& folder, std::int64_t now,
                                    std::int64_t staleMs = 5 * 60'000,
                                    std::int64_t maxAgeMs = 30LL * 24 * 3600 * 1000);

    /** Reads a copy. Throws project_file::Error. */
    Song recover (const Entry&);

    /** Removes a copy (after it was recovered or the user discarded it). */
    void remove (const Entry&);
}

} // namespace arranger

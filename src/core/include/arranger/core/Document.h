#pragma once

#include "arranger/core/Song.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace arranger
{

/**
    A song with undo and redo. Every change goes through edit(), which keeps
    the previous version. Clips are shared between versions, so a snapshot
    costs little more than the track list.

    Consecutive edits with the same `mergeKey` (for example dragging a fader)
    become one undo step.
*/
class Document
{
public:
    explicit Document (Song initial = {}, std::size_t maxUndo = 200);

    const Song& song() const noexcept { return current; }

    /** Applies a change. `description` is shown in the Edit menu ("Undo Move Clip"). */
    void edit (const std::string& description, const std::function<void (Song&)>& change, const std::string& mergeKey = {});

    /** Replaces the song and clears history (open, new). */
    void reset (Song);

    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    std::string undoDescription() const;
    std::string redoDescription() const;
    bool undo();
    bool redo();

    /** Ends any merging, so the next edit starts a new undo step. */
    void closeMerge() noexcept { lastMergeKey.clear(); }

    /** Changes on every edit, undo and redo. The same revision always means the same song. */
    std::uint64_t revision() const noexcept { return rev; }

    bool isModified() const noexcept { return rev != savedRev; }
    void markSaved() noexcept { savedRev = rev; }
    /** The song is not in any file yet (a recovered song): it counts as modified until saved. */
    void markUnsaved() noexcept { savedRev = ~std::uint64_t (0); }

private:
    struct Step
    {
        Song before;
        std::string description;
        std::uint64_t revisionBefore = 0;
    };

    Song current;
    std::vector<Step> undoStack, redoStack;
    std::size_t maxUndo;
    std::string lastMergeKey;
    std::uint64_t rev = 0, savedRev = 0, nextRev = 1;
};

} // namespace arranger

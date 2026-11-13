#include "arranger/core/Document.h"

namespace arranger
{

Document::Document (Song initial, std::size_t maxUndoSteps) : current (std::move (initial)), maxUndo (maxUndoSteps) {}

void Document::edit (const std::string& description, const std::function<void (Song&)>& change, const std::string& mergeKey)
{
    Song next = current;
    change (next);
    if (next == current)
        return;

    const bool merge = ! mergeKey.empty() && mergeKey == lastMergeKey && ! undoStack.empty() && redoStack.empty();
    if (! merge)
    {
        undoStack.push_back ({ current, description, rev });
        if (undoStack.size() > maxUndo)
            undoStack.erase (undoStack.begin());
    }
    redoStack.clear();
    current = std::move (next);
    rev = nextRev++;
    lastMergeKey = mergeKey;
}

void Document::reset (Song s)
{
    current = std::move (s);
    undoStack.clear();
    redoStack.clear();
    lastMergeKey.clear();
    rev = nextRev++;
    savedRev = rev;
}

std::string Document::undoDescription() const { return undoStack.empty() ? std::string() : undoStack.back().description; }
std::string Document::redoDescription() const { return redoStack.empty() ? std::string() : redoStack.back().description; }

bool Document::undo()
{
    if (undoStack.empty())
        return false;
    auto step = std::move (undoStack.back());
    undoStack.pop_back();
    redoStack.push_back ({ current, step.description, rev });
    current = std::move (step.before);
    rev = step.revisionBefore;
    lastMergeKey.clear();
    return true;
}

bool Document::redo()
{
    if (redoStack.empty())
        return false;
    auto step = std::move (redoStack.back());
    redoStack.pop_back();
    undoStack.push_back ({ current, step.description, rev });
    current = std::move (step.before);
    rev = step.revisionBefore;
    lastMergeKey.clear();
    return true;
}

} // namespace arranger

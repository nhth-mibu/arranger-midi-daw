#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace arranger
{

/**
    The list for File > Open Recent: newest first, no duplicates, at most
    `maxItems` paths. It is stored as one path per line.
*/
class RecentFiles
{
public:
    static constexpr std::size_t kDefaultMax = 10;

    explicit RecentFiles (std::size_t maxItems = kDefaultMax);

    /** Puts `path` first. An older copy of the same path is removed. */
    void add (const std::string& path);
    void remove (const std::string& path);
    void clear() { paths.clear(); }

    /** Removes the paths for which `exists` returns false. */
    void removeMissing (const std::function<bool (const std::string&)>& exists);

    void setMaxItems (std::size_t maxItems);
    std::size_t maxItems() const { return max; }

    const std::vector<std::string>& items() const { return paths; }
    bool empty() const { return paths.empty(); }

    std::string toString() const;
    static RecentFiles fromString (const std::string& text, std::size_t maxItems = kDefaultMax);

private:
    void trim();

    std::size_t max;
    std::vector<std::string> paths;
};

} // namespace arranger

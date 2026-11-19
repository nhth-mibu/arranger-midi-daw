#include "arranger/core/RecentFiles.h"

#include <algorithm>
#include <sstream>

namespace arranger
{

RecentFiles::RecentFiles (std::size_t maxItems) : max (std::max<std::size_t> (1, maxItems)) {}

void RecentFiles::add (const std::string& path)
{
    if (path.empty())
        return;
    remove (path);
    paths.insert (paths.begin(), path);
    trim();
}

void RecentFiles::remove (const std::string& path)
{
    paths.erase (std::remove (paths.begin(), paths.end(), path), paths.end());
}

void RecentFiles::removeMissing (const std::function<bool (const std::string&)>& exists)
{
    paths.erase (std::remove_if (paths.begin(), paths.end(), [&] (const std::string& p) { return ! exists (p); }), paths.end());
}

void RecentFiles::setMaxItems (std::size_t maxItems)
{
    max = std::max<std::size_t> (1, maxItems);
    trim();
}

void RecentFiles::trim()
{
    if (paths.size() > max)
        paths.resize (max);
}

std::string RecentFiles::toString() const
{
    std::string text;
    for (const auto& p : paths)
        text += p + "\n";
    return text;
}

RecentFiles RecentFiles::fromString (const std::string& text, std::size_t maxItems)
{
    RecentFiles list (maxItems);
    std::istringstream in (text);
    std::string line;
    std::vector<std::string> read;
    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        if (! line.empty() && std::find (read.begin(), read.end(), line) == read.end())
            read.push_back (line);
    }
    list.paths = std::move (read);
    list.trim();
    return list;
}

} // namespace arranger

#pragma once

#include <string>
#include <vector>

/** Yamaha Playlist files: a tab-separated list of Registration files. */
namespace arranger::playlist
{

struct Item
{
    std::string title;
    std::string filePath; ///< the path stored by the keyboard, e.g. I:/Playlist/Song.rgt
    int regist = 0;
    bool openViewer = false;
};

struct List
{
    std::vector<Item> items;
};

/** Reads a Playlist .tsv. Throws std::runtime_error when the header is missing. */
List parse (const std::string& text);

/** Writes the same tab-separated format with CRLF line ends. */
std::string write (const List& list);

} // namespace arranger::playlist

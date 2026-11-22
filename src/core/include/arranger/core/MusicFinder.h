#pragma once

#include <string>
#include <vector>

/** A desktop Music Finder: named songs with their style and keyboard settings. */
namespace arranger::music_finder
{

struct Entry
{
    std::string name;
    std::string style;       ///< style file name or style name
    double tempo = 120.0;
    std::vector<std::string> voices; ///< one voice name for each part
    std::string notes;
};

struct Book
{
    std::vector<Entry> entries; ///< no limit
};

/** Reads a Music Finder JSON file. Throws std::runtime_error. */
Book parse (const std::string& json);

/** Writes the stable JSON format. */
std::string write (const Book&);

/** Writes a spreadsheet-friendly CSV: name, style, tempo, voices, notes. */
std::string toCsv (const Book&);
/** Reads that CSV. A missing header is an error. */
Book fromCsv (const std::string&);
/** Reads a Yamaha Music Finder file (MDB-100-100-3000). */
Book fromMfd (const std::string&);

} // namespace arranger::music_finder

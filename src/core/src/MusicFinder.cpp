#include "arranger/core/MusicFinder.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace arranger::music_finder
{

Book parse (const std::string& text)
{
    try
    {
        const auto root = nlohmann::json::parse (text);
        if (! root.is_object() || root.value ("format", "") != "arranger-music-finder")
            throw std::runtime_error ("not a Music Finder file");
        Book book;
        for (const auto& item : root.at ("entries"))
        {
            Entry entry;
            entry.name = item.at ("name").get<std::string>();
            entry.style = item.value ("style", "");
            entry.tempo = item.value ("tempo", 120.0);
            entry.notes = item.value ("notes", "");
            if (item.contains ("voices"))
                for (const auto& voice : item["voices"])
                    entry.voices.push_back (voice.get<std::string>());
            book.entries.push_back (std::move (entry));
        }
        return book;
    }
    catch (const nlohmann::json::exception& e)
    {
        throw std::runtime_error (std::string ("Music Finder JSON: ") + e.what());
    }
}

std::string write (const Book& book)
{
    nlohmann::json root;
    root["format"] = "arranger-music-finder";
    root["entries"] = nlohmann::json::array();
    for (const auto& entry : book.entries)
    {
        nlohmann::json item;
        item["name"] = entry.name;
        item["style"] = entry.style;
        item["tempo"] = entry.tempo;
        item["voices"] = entry.voices;
        item["notes"] = entry.notes;
        root["entries"].push_back (std::move (item));
    }
    return root.dump (2);
}

} // namespace arranger::music_finder

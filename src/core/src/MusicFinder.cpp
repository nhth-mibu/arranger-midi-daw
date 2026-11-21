#include "arranger/core/MusicFinder.h"

#include <nlohmann/json.hpp>

#include <sstream>
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

std::string toCsv (const Book& book)
{
    const auto quote = [] (const std::string& text) {
        if (text.find_first_of (",\"\n\r") == std::string::npos)
            return text;
        std::string out = "\"";
        for (const char c : text)
            out += c == '"' ? std::string ("\"\"") : (c == '\n' || c == '\r') ? std::string (" ") : std::string (1, c);
        return out + "\"";
    };
    std::string out = "name,style,tempo,voices,notes\n";
    for (const auto& entry : book.entries)
    {
        std::string voices;
        for (std::size_t i = 0; i < entry.voices.size(); ++i)
            voices += (i ? "|" : "") + entry.voices[i];
        auto tempo = std::to_string (entry.tempo);
        while (tempo.size() > 1 && tempo.back() == '0') tempo.pop_back();
        if (! tempo.empty() && tempo.back() == '.') tempo.pop_back();
        out += quote (entry.name) + "," + quote (entry.style) + "," + tempo + ","
             + quote (voices) + "," + quote (entry.notes) + "\n";
    }
    return out;
}

namespace
{
    std::vector<std::string> splitCsv (const std::string& line)
    {
        std::vector<std::string> fields;
        std::string field;
        bool quoted = false;
        for (std::size_t i = 0; i < line.size(); ++i)
        {
            const char c = line[i];
            if (quoted && c == '"' && i + 1 < line.size() && line[i + 1] == '"')
            {
                field += '"';
                ++i;
            }
            else if (c == '"')
                quoted = ! quoted;
            else if (c == ',' && ! quoted)
            {
                fields.push_back (field);
                field.clear();
            }
            else
                field += c;
        }
        fields.push_back (field);
        return fields;
    }
}

Book fromCsv (const std::string& text)
{
    std::istringstream in (text);
    std::string line;
    if (! std::getline (in, line) || line.rfind ("name,style,tempo,voices,notes", 0) != 0)
        throw std::runtime_error ("not a Music Finder CSV");
    Book book;
    while (std::getline (in, line))
    {
        if (line.empty())
            continue;
        const auto fields = splitCsv (line);
        if (fields.size() < 3)
            throw std::runtime_error ("a Music Finder CSV row needs name, style and tempo");
        Entry entry;
        entry.name = fields[0];
        entry.style = fields[1];
        try { entry.tempo = std::stod (fields[2]); }
        catch (...) { throw std::runtime_error ("a Music Finder tempo is not a number"); }
        if (fields.size() > 3)
        {
            std::istringstream voices (fields[3]);
            std::string voice;
            while (std::getline (voices, voice, '|'))
                if (! voice.empty())
                    entry.voices.push_back (voice);
        }
        if (fields.size() > 4)
            entry.notes = fields[4];
        book.entries.push_back (std::move (entry));
    }
    return book;
}

} // namespace arranger::music_finder

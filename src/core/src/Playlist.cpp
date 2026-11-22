#include "arranger/core/Playlist.h"

#include <sstream>
#include <stdexcept>

namespace arranger::playlist
{

namespace
{
    std::string trimCr (std::string text)
    {
        if (! text.empty() && text.back() == '\r')
            text.pop_back();
        return text;
    }
}

List parse (const std::string& text)
{
    std::istringstream in (text);
    std::string line;
    if (! std::getline (in, line) || trimCr (line).rfind ("TITLE\tFILEPATH\tREGIST\tOPENVIEWER", 0) != 0)
        throw std::runtime_error ("not a Yamaha Playlist file");
    List list;
    while (std::getline (in, line))
    {
        line = trimCr (line);
        if (line.empty())
            continue;
        const auto a = line.find ('\t');
        const auto b = a == std::string::npos ? std::string::npos : line.find ('\t', a + 1);
        const auto c = b == std::string::npos ? std::string::npos : line.find ('\t', b + 1);
        if (a == std::string::npos || b == std::string::npos || c == std::string::npos)
            throw std::runtime_error ("a Playlist row needs four tab-separated fields");
        Item item;
        item.title = line.substr (0, a);
        item.filePath = line.substr (a + 1, b - a - 1);
        try { item.regist = std::stoi (line.substr (b + 1, c - b - 1)); }
        catch (...) { throw std::runtime_error ("a Playlist REGIST value is not a number"); }
        item.openViewer = line.substr (c + 1) == "ON";
        list.items.push_back (std::move (item));
    }
    return list;
}

std::string write (const List& list)
{
    std::string out = "TITLE\tFILEPATH\tREGIST\tOPENVIEWER\r\n";
    for (const auto& item : list.items)
        out += item.title + "\t" + item.filePath + "\t" + std::to_string (item.regist) + "\t"
             + (item.openViewer ? "ON" : "OFF") + "\r\n";
    return out;
}

} // namespace arranger::playlist

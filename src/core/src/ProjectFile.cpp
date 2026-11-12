#include "arranger/core/ProjectFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace arranger::project_file
{

using nlohmann::json;

namespace
{
    template <typename T>
    T valueOr (const json& object, const char* key, T fallback)
    {
        const auto it = object.find (key);
        if (it == object.end() || it->is_null())
            return fallback;
        try
        {
            return it->get<T>();
        }
        catch (const json::exception&)
        {
            throw Error (std::string ("Field \"") + key + "\" has the wrong type");
        }
    }

    json trackToJson (const Track& t)
    {
        return { { "name", t.name }, { "colour", t.colour }, { "midiChannel", t.midiChannel } };
    }

    Track trackFromJson (const json& j)
    {
        if (! j.is_object())
            throw Error ("A track must be an object");

        Track t;
        t.name = valueOr (j, "name", t.name);
        t.colour = valueOr (j, "colour", t.colour);
        t.midiChannel = std::clamp (valueOr (j, "midiChannel", t.midiChannel), 1, 16);
        return t;
    }
}

std::string toJson (const Song& song)
{
    json tracks = json::array();
    for (const auto& t : song.tracks)
        tracks.push_back (trackToJson (t));

    const json root = {
        { "format", kFormatName },
        { "version", kVersion },
        { "song",
          {
              { "title", song.title },
              { "tempoBpm", song.tempoBpm },
              { "timeSignature", { song.timeSignature.numerator, song.timeSignature.denominator } },
              { "key", song.key },
              { "tracks", tracks },
              { "metronome",
                {
                    { "enabled", song.metronome.enabled },
                    { "level", song.metronome.level },
                    { "accentDownbeat", song.metronome.accentDownbeat },
                } },
          } },
    };

    return root.dump (2) + "\n";
}

Song fromJson (std::string_view text)
{
    json root;
    try
    {
        root = json::parse (text);
    }
    catch (const json::parse_error& e)
    {
        throw Error (std::string ("Not valid JSON: ") + e.what());
    }

    if (! root.is_object() || valueOr<std::string> (root, "format", "") != kFormatName)
        throw Error ("Not an Arranger MIDI DAW project");

    const int version = valueOr (root, "version", 0);
    if (version < 1)
        throw Error ("Project file has no valid version");
    if (version > kVersion)
        throw Error ("Project was saved by a newer version of the app (file version "
                     + std::to_string (version) + ")");

    const auto songIt = root.find ("song");
    if (songIt == root.end() || ! songIt->is_object())
        throw Error ("Project has no song");
    const json& s = *songIt;

    Song song;
    song.title = valueOr (s, "title", song.title);
    song.tempoBpm = clampTempo (valueOr (s, "tempoBpm", song.tempoBpm));
    song.key = valueOr (s, "key", song.key);

    if (const auto it = s.find ("timeSignature"); it != s.end())
    {
        if (! it->is_array() || it->size() != 2 || ! (*it)[0].is_number_integer() || ! (*it)[1].is_number_integer())
            throw Error ("timeSignature must be [numerator, denominator]");

        TimeSignature sig { (*it)[0].get<int>(), (*it)[1].get<int>() };
        if (! sig.isValid())
            throw Error ("Unsupported time signature");
        song.timeSignature = sig;
    }

    if (const auto it = s.find ("tracks"); it != s.end())
    {
        if (! it->is_array())
            throw Error ("tracks must be a list");
        for (const auto& t : *it)
            song.tracks.push_back (trackFromJson (t));
    }

    if (const auto it = s.find ("metronome"); it != s.end() && it->is_object())
    {
        auto& m = song.metronome;
        m.enabled = valueOr (*it, "enabled", m.enabled);
        m.level = std::clamp (valueOr (*it, "level", m.level), 0.0f, 1.0f);
        m.accentDownbeat = valueOr (*it, "accentDownbeat", m.accentDownbeat);
    }

    return song;
}

void save (const Song& song, const std::filesystem::path& path)
{
    namespace fs = std::filesystem;

    auto temp = path;
    temp += ".saving";

    {
        std::ofstream out (temp, std::ios::binary | std::ios::trunc);
        if (! out)
            throw Error ("Can't write " + temp.string());
        out << toJson (song);
        out.flush();
        if (! out)
            throw Error ("Write failed for " + temp.string());
    }

    std::error_code ec;
    fs::rename (temp, path, ec);
    if (ec)
    {
        fs::remove (temp, ec);
        throw Error ("Can't replace " + path.string());
    }
}

Song load (const std::filesystem::path& path)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        throw Error ("Can't open " + path.string());

    std::stringstream buffer;
    buffer << in.rdbuf();
    return fromJson (buffer.str());
}

} // namespace arranger::project_file

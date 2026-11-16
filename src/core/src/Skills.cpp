#include "arranger/core/Skills.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace arranger::skills
{

namespace
{
    using json = nlohmann::json;

    constexpr std::size_t kMaxSkillBytes = 512 * 1024;
    constexpr std::size_t kMaxUserFiles = 100;
    constexpr std::size_t kMaxSkills = 200;

    // The built-in skill pack. It is data, not code: add another pack in the
    // user's Skills folder without rebuilding the app.
    constexpr const char* kBuiltIn = R"SKILL(
{
  "skills": [
    {
      "id": "common-progressions",
      "name": "Common chord progressions",
      "description": "Pop, rock, jazz and twelve-bar blues progressions, written as roman numerals so they fit any key.",
      "when": "Use when the user asks for a common chord progression, a progression in a genre, a starting point for a new song, or names a progression such as I-V-vi-IV.",
      "tags": ["chords", "progressions", "harmony", "pop", "rock", "jazz", "blues", "theory"],
      "instructions": "Pick the progression that fits the user's genre and mood. The data lists each progression with a stable id, name, genre, its own key, roman numerals in 'roman', and example chord names in 'chords'. The chord names are in the progression's own key; ask chords_from_roman for another key, or transpose the roman numerals. Write chord symbols with set_chords, and add the notes with add_notes. Use fill_from_style when the user wants a style arrangement. One chord is one bar unless the user asks for another length. For the twelve-bar blues, write one chord for each bar; do not repeat the first chord only. Keep the user's song key unless they name another key.",
      "examples": [
        "Use a pop I-V-vi-IV progression in D major on the Piano track.",
        "Find a jazz ii-V-I and write it on bars 1 to 4.",
        "Give me a twelve-bar blues in C, one chord a bar.",
        "What common rock progressions can I use?"
      ],
      "data": {
        "progressions": [
          { "id": "pop-1", "name": "Pop: I-V-vi-IV", "genre": "Pop", "roman": "I V vi IV", "key": "C major" },
          { "id": "pop-2", "name": "Pop: I-IV-V-I", "genre": "Pop", "roman": "I IV V I", "key": "C major" },
          { "id": "pop-3", "name": "Pop: I-IV-Vsus4-V", "genre": "Pop", "roman": "I IV Vsus4 V", "key": "C major" },
          { "id": "pop-4", "name": "Pop: I-vi-ii-V", "genre": "Pop", "roman": "I vi ii V", "key": "C major" },
          { "id": "pop-5", "name": "Pop: I-ii-IV-V", "genre": "Pop", "roman": "I ii IV V", "key": "C major" },
          { "id": "pop-6", "name": "Pop: I-V6-vi-V", "genre": "Pop", "roman": "I V6 vi V", "key": "C major" },
          { "id": "pop-7", "name": "Pop: i-iv-V-i", "genre": "Pop", "roman": "i iv V i", "key": "A minor" },
          { "id": "pop-8", "name": "Pop: i-III-iv-v", "genre": "Pop", "roman": "i III iv v", "key": "A minor" },

          { "id": "rock-1", "name": "Rock: I-vi-IV-V", "genre": "Rock", "roman": "I vi IV V", "key": "C major" },
          { "id": "rock-2", "name": "Rock: I-IV-I-V", "genre": "Rock", "roman": "I IV I V", "key": "C major" },
          { "id": "rock-3", "name": "Rock: I-I-bIII-IV", "genre": "Rock", "roman": "I I bIII IV", "key": "C major" },
          { "id": "rock-4", "name": "Rock: I-III-IV-iv", "genre": "Rock", "roman": "I III IV iv", "key": "C major" },
          { "id": "rock-5", "name": "Rock: ii-I-V-bVII", "genre": "Rock", "roman": "ii I V bVII", "key": "C major" },
          { "id": "rock-6", "name": "Rock: V-V-IV-I", "genre": "Rock", "roman": "V V IV I", "key": "C major" },
          { "id": "rock-7", "name": "Rock: vi-IV-I-V", "genre": "Rock", "roman": "vi IV I V", "key": "C major" },
          { "id": "rock-8", "name": "Rock: vi-V7/vi-II-V7", "genre": "Rock", "roman": "vi V7/vi II V7", "key": "C major" },

          { "id": "jazz-1", "name": "Jazz: ii7-V7-Imaj7-Imaj7", "genre": "Jazz", "roman": "ii7 V7 Imaj7 Imaj7", "key": "C major" },
          { "id": "jazz-2", "name": "Jazz: ii7-V7-Imaj7-VI7", "genre": "Jazz", "roman": "ii7 V7 Imaj7 VI7", "key": "C major" },
          { "id": "jazz-3", "name": "Jazz: Imaj7-vi7-ii7-V7", "genre": "Jazz", "roman": "Imaj7 vi7 ii7 V7", "key": "C major" },
          { "id": "jazz-4", "name": "Jazz: Imaj7-#Idim7-ii7-V7", "genre": "Jazz", "roman": "Imaj7 #Io7 ii7 V7", "key": "C major" },
          { "id": "jazz-5", "name": "Jazz: Imaj7-I7-IVmaj7-iv7", "genre": "Jazz", "roman": "Imaj7 I7 IVmaj7 iv7", "key": "C major" },
          { "id": "jazz-6", "name": "Jazz: viø7-ii7-V7-Imaj7", "genre": "Jazz", "roman": "viø7 ii7 V7 Imaj7", "key": "C major" },
          { "id": "jazz-7", "name": "Jazz: iiø7-V7-i7-i7", "genre": "Jazz", "roman": "iiø7 V7 i7 i7", "key": "C minor" },
          { "id": "jazz-8", "name": "Jazz: i7-V7-i7-V7", "genre": "Jazz", "roman": "i7 V7 i7 V7", "key": "C minor" },

          { "id": "blues-1", "name": "Blues: 12-bar I7-IV7-V7", "genre": "Blues", "roman": "I7 I7 I7 I7 IV7 IV7 I7 I7 V7 IV7 I7 I7", "key": "C major" }
        ]
      }
    }
  ]
}
)SKILL";

    std::string lower (std::string_view text)
    {
        std::string out (text);
        std::transform (out.begin(), out.end(), out.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return out;
    }

    std::vector<std::string> words (std::string_view text)
    {
        std::vector<std::string> out;
        std::string word;
        for (const char c : text)
        {
            if (std::isspace (static_cast<unsigned char> (c)))
            {
                if (! word.empty())
                    out.push_back (word);
                word.clear();
            }
            else
            {
                word += static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
            }
        }
        if (! word.empty())
            out.push_back (word);
        return out;
    }

    std::vector<std::string> strings (const json& value, const char* key)
    {
        std::vector<std::string> out;
        if (value.contains (key) && value[key].is_array())
            for (const auto& item : value[key])
                if (item.is_string())
                    out.push_back (item.get<std::string>());
        return out;
    }

    std::optional<Skill> fromJson (const json& item, const std::string& source, bool builtIn, std::string& why)
    {
        if (! item.is_object() || ! item.contains ("id") || ! item["id"].is_string())
        {
            why = "a skill needs a string \"id\"";
            return {};
        }
        Skill skill;
        skill.id = item["id"].get<std::string>();
        if (skill.id.empty())
        {
            why = "a skill needs a non-empty \"id\"";
            return {};
        }
        skill.name = item.value ("name", skill.id);
        skill.description = item.value ("description", std::string());
        skill.when = item.value ("when", std::string());
        skill.instructions = item.value ("instructions", std::string());
        if (skill.instructions.empty())
        {
            why = "skill " + skill.id + " needs \"instructions\"";
            return {};
        }
        skill.tags = strings (item, "tags");
        skill.examples = strings (item, "examples");
        if (item.contains ("data"))
            skill.data = item["data"].dump();
        skill.source = source;
        skill.builtIn = builtIn;
        return skill;
    }
} // namespace

Registry::Registry (std::string userFolder)
{
    try
    {
        const auto document = json::parse (kBuiltIn);
        for (const auto& item : document.at ("skills"))
        {
            std::string why;
            if (auto skill = fromJson (item, "built-in", true, why))
                skills.push_back (std::move (*skill));
            else
                problems.push_back ("Built-in skill: " + why);
        }
    }
    catch (const std::exception& e)
    {
        problems.push_back (std::string ("Built-in skills can't be read: ") + e.what());
    }

    if (! userFolder.empty())
        loadUserFolder (userFolder);
}

void Registry::loadUserFolder (const std::string& folder)
{
    std::error_code error;
    const std::filesystem::path path (folder);
    if (! std::filesystem::is_directory (path, error))
        return;

    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it (path, error), end; ! error && it != end; it.increment (error))
    {
        if (files.size() >= kMaxUserFiles)
            break;
        const auto& p = it->path();
        if (! it->is_regular_file (error) || p.extension() != ".json")
            continue;
        files.push_back (p);
    }
    std::sort (files.begin(), files.end());

    for (const auto& file : files)
    {
        std::ifstream in (file, std::ios::binary);
        if (! in)
        {
            problems.push_back ("Can't read " + file.string());
            continue;
        }
        std::ostringstream text;
        text << in.rdbuf();
        const auto contents = text.str();
        if (contents.size() > kMaxSkillBytes)
        {
            problems.push_back (file.filename().string() + " is larger than 512 KB");
            continue;
        }
        try
        {
            const auto document = json::parse (contents);
            std::vector<json> items;
            if (document.is_array())
                for (const auto& item : document)
                    items.push_back (item);
            else if (document.contains ("skills") && document["skills"].is_array())
                for (const auto& item : document["skills"])
                    items.push_back (item);
            else
                items.push_back (document);

            for (const auto& item : items)
            {
                if (skills.size() >= kMaxSkills)
                {
                    problems.push_back ("Only the first 200 skills are loaded.");
                    return;
                }
                std::string why;
                auto skill = fromJson (item, file.string(), false, why);
                if (! skill)
                {
                    problems.push_back (file.filename().string() + ": " + why);
                    continue;
                }
                const auto duplicate = std::find_if (skills.begin(), skills.end(), [&] (const Skill& s) { return s.id == skill->id; });
                if (duplicate != skills.end())
                {
                    problems.push_back (file.filename().string() + ": the skill id " + skill->id + " is already used");
                    continue;
                }
                skills.push_back (std::move (*skill));
            }
        }
        catch (const std::exception& e)
        {
            problems.push_back (file.filename().string() + ": " + e.what());
        }
    }
}

std::vector<Skill> Registry::find (std::string_view query, std::string_view tag) const
{
    const auto wantedTag = lower (tag);
    const auto wanted = words (query);
    std::vector<Skill> out;
    for (const auto& skill : skills)
    {
        if (! wantedTag.empty())
        {
            const auto hasTag = std::any_of (skill.tags.begin(), skill.tags.end(), [&] (const std::string& t) { return lower (t) == wantedTag; });
            if (! hasTag)
                continue;
        }
        std::string haystack = lower (skill.id + " " + skill.name + " " + skill.description + " " + skill.when + " " + skill.instructions + " " + skill.data);
        for (const auto& t : skill.tags)
            haystack += " " + lower (t);
        bool matches = true;
        for (const auto& word : wanted)
            if (haystack.find (word) == std::string::npos)
            {
                matches = false;
                break;
            }
        if (matches)
            out.push_back (skill);
    }
    return out;
}

std::optional<Skill> Registry::get (std::string_view id) const
{
    for (const auto& skill : skills)
        if (skill.id == id)
            return skill;
    return {};
}

} // namespace arranger::skills

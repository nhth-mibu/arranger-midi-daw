#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
    Skills: recipes and data for the AI assistant, kept apart from the code
    tools. The built-in skills are always there; the user can add more as JSON
    files in a Skills folder. A skill only contains text and data. It never
    runs code.
*/
namespace arranger::skills
{

struct Skill
{
    std::string id;
    std::string name;
    std::string description;
    std::string when;         ///< when the assistant should use it
    std::string instructions; ///< what to do, in words
    std::vector<std::string> tags;
    std::vector<std::string> examples;
    std::string data;         ///< optional JSON text (for example progressions)
    std::string source;       ///< "built-in" or the file path
    bool builtIn = false;
};

/**
    Reads the built-in skills and the JSON files in `userFolder`. A file may
    hold one skill, a list of skills, or an object with a "skills" list.
    Malformed files are skipped and reported in warnings().
*/
class Registry
{
public:
    explicit Registry (std::string userFolder = {});

    const std::vector<Skill>& all() const noexcept { return skills; }
    std::vector<Skill> find (std::string_view query, std::string_view tag = {}) const;
    std::optional<Skill> get (std::string_view id) const;

    const std::vector<std::string>& warnings() const noexcept { return problems; }

private:
    void loadUserFolder (const std::string& folder);

    std::vector<Skill> skills;
    std::vector<std::string> problems;
};

} // namespace arranger::skills

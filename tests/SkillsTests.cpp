#include "arranger/core/Skills.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using namespace arranger;

TEST_CASE ("The built-in skill pack holds the common progressions", "[skills]")
{
    const skills::Registry registry;
    CHECK (registry.warnings().empty());

    const auto found = registry.find ("progressions");
    REQUIRE (found.size() == 1);
    CHECK (found.front().id == "common-progressions");
    CHECK (found.front().builtIn);
    CHECK (found.front().source == "built-in");

    const auto skill = registry.get ("common-progressions");
    REQUIRE (skill.has_value());
    REQUIRE_FALSE (skill->instructions.empty());
    const auto data = nlohmann::json::parse (skill->data);
    REQUIRE (data.at ("progressions").size() == 25);
    CHECK (data.at ("progressions").front().at ("id") == "pop-1");
    CHECK (data.at ("progressions").front().at ("roman") == "I V vi IV");
    CHECK (data.at ("progressions").back().at ("id") == "blues-1");
}

TEST_CASE ("A user skill pack is found and bad packs are reported", "[skills]")
{
    const auto folder = std::filesystem::temp_directory_path() / "arranger-skills-test";
    std::filesystem::remove_all (folder);
    std::filesystem::create_directories (folder);

    {
        std::ofstream file (folder / "test-pack.json");
        file << R"({"skills":[
            {"id":"test-skill","name":"Test skill","description":"A test recipe",
             "when":"When testing","instructions":"Do the test.","tags":["test","recipe"],
             "examples":["use the test skill"],"data":{"value":42}},
            {"id":"broken","name":"No instructions"}
        ]})";
    }
    {
        std::ofstream file (folder / "not-json.json");
        file << "this is not json";
    }

    const skills::Registry registry (folder.string());
    const auto found = registry.find ("test");
    REQUIRE (found.size() == 1);
    CHECK (found.front().id == "test-skill");
    CHECK (found.front().builtIn == false);
    CHECK (found.front().tags.size() == 2);

    const auto skill = registry.get ("test-skill");
    REQUIRE (skill.has_value());
    CHECK (nlohmann::json::parse (skill->data).at ("value") == 42);
    CHECK (registry.warnings().size() == 2); // the skill without instructions, and the bad JSON

    std::filesystem::remove_all (folder);
}

// Every MCP tool, called with missing, wrong and extreme arguments. An AI makes
// mistakes: a tool must answer with an error (or do something sensible), never
// crash, hang or leave a song that can't be saved and opened again.
//
// The arguments come from each tool's own schema (tools/list), so a new tool is
// tested here without writing anything.

#include "KitchenSink.h"

#include "arranger/core/Document.h"
#include "arranger/core/McpServer.h"
#include "arranger/core/ProjectFile.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <limits>
#include <set>

using namespace arranger;
using nlohmann::json;

namespace
{
    /** The base Host: nothing outside the song (no files, devices, helpers or downloads). */
    struct SongOnlyHost final : mcp::Host
    {
        Document doc;
        explicit SongOnlyHost (Song s) : doc (std::move (s)) {}
        const Song& song() const override { return doc.song(); }
        std::uint64_t songRevision() const override { return doc.revision(); }
        void edit (const std::string& d, const std::function<void (Song&)>& fn) override { doc.edit (d, fn); }
        bool undo() override { return doc.undo(); }
        bool redo() override { return doc.redo(); }
    };

    /** Values of every JSON type, plus the extremes of numbers and strings. */
    std::vector<json> oddValues()
    {
        return { nullptr,
                 true,
                 0,
                 -1,
                 1,
                 127,
                 128,
                 65536,
                 999999999,
                 -999999999,
                 2.5e8,
                 std::numeric_limits<std::int64_t>::max(),
                 std::numeric_limits<std::int64_t>::min(),
                 std::numeric_limits<std::uint64_t>::max(),
                 0.5,
                 -0.0001,
                 1e300,
                 -1e300,
                 "",
                 "x",
                 "C4",
                 "Keys",
                 "1/4",
                 std::string (10000, 'a'),
                 "\xff\xfe broken UTF-8",
                 json::array(),
                 json::array ({ 1, "a", nullptr }),
                 json::array ({ json::object ({ { "bar", -5 }, { "beat", 1e9 }, { "pitch", "Z9" }, { "length", -1 } }) }),
                 json::object(),
                 json::object ({ { "bar", 0 } }) };
    }

    /** The arguments to try for one tool: none, each property alone with each odd value,
        all properties at once, and the right keys with values for the wrong property. */
    std::vector<json> argumentSets (const json& schema)
    {
        std::vector<json> out { json::object(), nullptr, json::array(), "text" };
        const auto values = oddValues();
        if (! schema.contains ("properties"))
            return out;
        for (const auto& [key, property] : schema["properties"].items())
        {
            for (const auto& v : values)
                out.push_back ({ { key, v } });
            // Enum values are the ones most likely to reach deep code.
            if (property.contains ("enum"))
                for (const auto& e : property["enum"])
                    out.push_back ({ { key, e } });
        }
        for (const auto& v : values)
        {
            json all = json::object();
            for (const auto& [key, property] : schema["properties"].items())
                all[key] = v;
            out.push_back (all);
        }
        return out;
    }

    bool isWellFormedReply (const std::string& reply)
    {
        const auto r = json::parse (reply, nullptr, false);
        if (r.is_discarded() || ! r.contains ("result"))
            return r.contains ("error"); // a JSON-RPC error is fine too
        const auto& result = r["result"];
        return result.contains ("content") && result["content"].is_array() && ! result["content"].empty();
    }
}

TEST_CASE ("MCP: every tool survives missing, wrong and extreme arguments", "[mcp][robustness]")
{
    SongOnlyHost host (test::kitchenSinkSong());
    mcp::Server server (host);
    const auto tools = json::parse (server.toolList());
    REQUIRE (tools.size() > 60);

    std::set<std::string> leaked; // tools whose error showed a C++ library message, not a reason
    int calls = 0, id = 0;
    std::pair<std::string, double> slowest { {}, 0.0 }; // no call may hang
    for (const auto& tool : tools)
    {
        const auto name = tool["name"].get<std::string>();
        INFO ("tool " << name);
        for (const auto& args : argumentSets (tool["inputSchema"]))
        {
            INFO ("arguments " << args.dump (-1, ' ', false, json::error_handler_t::replace).substr (0, 300));
            const auto started = std::chrono::steady_clock::now();
            const auto reply = server.handle (json ({ { "jsonrpc", "2.0" }, { "id", ++id }, { "method", "tools/call" },
                                                      { "params", { { "name", name }, { "arguments", args } } } })
                                                  .dump (-1, ' ', false, json::error_handler_t::replace));
            CHECK (isWellFormedReply (reply));
            const auto took = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
            if (took > slowest.second)
                slowest = { name + " " + args.dump (-1, ' ', false, json::error_handler_t::replace).substr (0, 200), took };
            ++calls;
            if (reply.find ("json.exception") != std::string::npos || reply.find ("basic_string") != std::string::npos
                || reply.find ("vector::") != std::string::npos || reply.find ("stoi") != std::string::npos)
                leaked.insert (name);

            // The song stays one the app can save and open.
            if (host.doc.revision() % 16 == 0)
                CHECK_NOTHROW (project_file::fromJson (project_file::toJson (host.song())));
        }
        // Bring the song back now and then, so later tools still have tracks and clips to work on.
        if (host.song().tracks.size() < 3)
            host.doc.reset (test::kitchenSinkSong());
    }
    CHECK_NOTHROW (project_file::fromJson (project_file::toJson (host.song())));
    INFO ("tools that answered with a C++ library message: " << json (leaked).dump());
    CHECK (leaked.empty());
    CHECK (calls > 3000);
    INFO ("slowest call: " << slowest.first << " (" << slowest.second << " s)");
    CHECK (slowest.second < 2.0);
}

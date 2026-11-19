// arranger_fuzz: a small mutation fuzzer for the file readers and the MCP server.
//
//   arranger_fuzz <target> [--seconds 10] [--seed 1] [--out folder]
//   arranger_fuzz <target> --replay file...      (run saved inputs once, for regressions)
//   arranger_fuzz --list
//
// It starts from real files (seeds: examples, device profiles, generated songs),
// changes them at random (bits, bytes, numbers, cut, copy, splice, tokens), and
// runs the target on each. Build it with -DARRANGER_SANITIZE=address,undefined:
// the sanitizers turn hidden memory errors into crashes. On a crash, a finding
// or a slow input, the input is saved to the out folder (crash-*, finding-*,
// slow-*), so it can be replayed and kept as a regression test.
//
// It works with any compiler (Apple Clang has no libFuzzer). It has no coverage
// feedback, so it finds shallow bugs fast and deep ones slowly: run it long.

#include "FuzzTargets.h"
#include "../KitchenSink.h"

#include "arranger/core/McpServer.h"
#include "arranger/core/MidiFile.h"
#include "arranger/core/MusicXml.h"
#include "arranger/core/ProjectFile.h"
#include "arranger/core/Sample.h"
#include "arranger/core/Document.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#if ! defined(_WIN32)
  #include <fcntl.h>
  #include <unistd.h>
#endif

using namespace arranger;
namespace fs = std::filesystem;
using Buffer = std::vector<std::uint8_t>;

#if defined(__GNUC__) || defined(__clang__)
// Present only in sanitizer builds: lets the fuzzer save the input before the sanitizer ends the process.
extern "C" void __sanitizer_set_death_callback (void (*) (void)) __attribute__ ((weak));
#endif

namespace
{
    // ---- Saving the input that crashed -------------------------------------------
    // Kept in plain memory so the crash handler can write it without allocating.
    Buffer current;
    char crashPath[4096] = {};

    void dumpCurrent()
    {
#if ! defined(_WIN32)
        if (crashPath[0] == 0)
            return;
        const int fd = ::open (crashPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0)
        {
            [[maybe_unused]] const auto n = ::write (fd, current.data(), current.size());
            ::close (fd);
            static const char note[] = "\n[fuzz] the input is saved in the crash file\n";
            [[maybe_unused]] const auto m = ::write (2, note, sizeof (note) - 1);
        }
#endif
    }

    void onSignal (int sig)
    {
        dumpCurrent();
        std::signal (sig, SIG_DFL);
        std::raise (sig);
    }

    void installCrashHandlers()
    {
#if defined(__GNUC__) || defined(__clang__)
        if (__sanitizer_set_death_callback != nullptr)
            __sanitizer_set_death_callback (dumpCurrent);
#endif
        for (const int sig : { SIGSEGV, SIGABRT, SIGFPE, SIGILL
#if ! defined(_WIN32)
                               , SIGBUS
#endif
             })
            std::signal (sig, onSignal);
    }

    // ---- Seeds --------------------------------------------------------------------
    Buffer readFile (const fs::path& p)
    {
        std::ifstream in (p, std::ios::binary);
        return Buffer ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    }

    Buffer bytesOf (const std::string& s) { return Buffer (s.begin(), s.end()); }

    std::vector<fs::path> filesWith (const fs::path& folder, const std::vector<std::string>& extensions)
    {
        std::vector<fs::path> found;
        std::error_code ec;
        for (fs::recursive_directory_iterator it (folder, ec), end; ! ec && it != end; it.increment (ec))
            if (it->is_regular_file() && std::find (extensions.begin(), extensions.end(), it->path().extension().string()) != extensions.end())
                found.push_back (it->path());
        std::sort (found.begin(), found.end());
        return found;
    }

    Song smallSong()
    {
        Song s;
        s.title = "Seed";
        auto& t = edit::addTrack (s, "Piano");
        Clip c;
        c.length = 3840;
        c.notes = { { 0, 480, 60, 100 }, { 480, 240, 64, 80 }, { 960, 960, 67, 90 } };
        edit::addClip (s, t.id, c);
        edit::setMark (s, t.id, { ScoreMark::Kind::chord, 0, "Cmaj7" });
        edit::setMark (s, t.id, { ScoreMark::Kind::lyric, 0, "la" });
        return s;
    }

    struct SeedHost final : mcp::Host
    {
        Document doc;
        const Song& song() const override { return doc.song(); }
        std::uint64_t songRevision() const override { return doc.revision(); }
        void edit (const std::string& d, const std::function<void (Song&)>& fn) override { doc.edit (d, fn); }
        bool undo() override { return doc.undo(); }
        bool redo() override { return doc.redo(); }
        void play() override {}
        void stop() override {}
        void locate (Tick) override {}
        bool isPlaying() const override { return false; }
        double position() const override { return 0.0; }
        void launchScene (int) override {}
    };

    std::vector<Buffer> seedsFor (const std::string& target)
    {
        const fs::path root (ARRANGER_SOURCE_DIR);
        std::vector<Buffer> seeds;
        const auto addFiles = [&] (const fs::path& folder, const std::vector<std::string>& ext) {
            for (const auto& f : filesWith (folder, ext))
                seeds.push_back (readFile (f));
        };

        if (target == "project")
        {
            addFiles (root / "examples", { ".arranger" });
            seeds.push_back (bytesOf (project_file::toJson (test::kitchenSinkSong())));
            seeds.push_back (bytesOf (project_file::toJson (smallSong())));
        }
        else if (target == "midi")
        {
            addFiles (root / "examples", { ".mid" });
            seeds.push_back (midi_file::write (smallSong()));
            midi_file::WriteOptions o;
            o.singleTrack = true;
            o.lyrics = true;
            o.automation = true;
            o.xfChords = true;
            o.startSysEx = { { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 } };
            seeds.push_back (midi_file::write (test::kitchenSinkSong(), o));
        }
        else if (target == "musicxml")
        {
            seeds.push_back (bytesOf (musicxml::write (smallSong())));
            seeds.push_back (bytesOf (musicxml::write (test::kitchenSinkSong())));
        }
        else if (target == "style")
        {
            addFiles (root / "examples", { ".sty" });
            addFiles (root / "tests" / "fuzz" / "seeds" / "style", { ".sty", ".prs" });
        }
        else if (target == "wav")
        {
            Sample s;
            s.sampleRate = 44100.0;
            s.channels = { std::vector<float> (64, 0.25f), std::vector<float> (64, -0.5f) };
            for (const int bits : { 16, 24, 32 })
                seeds.push_back (wav::serialise (s, bits));
            s.channels.resize (1);
            seeds.push_back (wav::serialise (s, 16));
        }
        else if (target == "device")
        {
            addFiles (root / "resources" / "devices", { ".yaml" });
            seeds.push_back (bytesOf (R"({"maxVoices": 8, "multitimbral": true})"));
        }
        else if (target == "controller")
        {
            addFiles (root / "resources" / "controllers", { ".yaml" });
        }
        else if (target == "l6max")
        {
            seeds.push_back (bytesOf ("TRACK01_01.wav\nTRACK02_01.wav\nTRACK05_ST_01.wav\nTRACK05_ST_02.wav\nMASTER_01.wav\nTRACK01_01_001.wav\n"));
        }
        else if (target == "mcp")
        {
            // Every tool, called with no arguments and with typical ones, after an initialize.
            SeedHost host;
            mcp::Server server (host);
            const auto tools = nlohmann::json::parse (server.toolList());
            const auto list = tools.is_array() ? tools : tools.value ("tools", nlohmann::json::array());
            std::string init = R"({"jsonrpc":"2.0","id":0,"method":"initialize","params":{}})" "\n";
            int id = 1;
            for (const auto& tool : list)
            {
                const auto name = tool.value ("name", std::string());
                nlohmann::json args = nlohmann::json::object();
                // Fill each argument with a plausible value of its type.
                const auto props = tool.contains ("inputSchema") ? tool["inputSchema"].value ("properties", nlohmann::json::object()) : nlohmann::json::object();
                for (const auto& [key, schema] : props.items())
                {
                    const auto type = schema.value ("type", std::string ("string"));
                    if (type == "integer" || type == "number")
                        args[key] = 1;
                    else if (type == "boolean")
                        args[key] = true;
                    else if (type == "array")
                        args[key] = nlohmann::json::array ({ 1 });
                    else if (type == "object")
                        args[key] = nlohmann::json::object();
                    else
                        args[key] = "1";
                }
                for (const auto& a : { nlohmann::json::object(), args })
                {
                    const nlohmann::json call = { { "jsonrpc", "2.0" }, { "id", id++ }, { "method", "tools/call" },
                                                  { "params", { { "name", name }, { "arguments", a } } } };
                    seeds.push_back (bytesOf (init + call.dump() + "\n"));
                }
            }
            seeds.push_back (bytesOf (init + R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})" "\n"));
        }
        return seeds;
    }

    // ---- Mutation -------------------------------------------------------------------
    const std::vector<std::string> tokens {
        "0", "-1", "1", "127", "128", "255", "256", "65535", "65536", "2147483647", "-2147483648", "4294967296",
        "9223372036854775807", "-9223372036854775808", "1e308", "-1e308", "1e-308", "NaN", "Infinity", "0.0", "-0",
        "\"\"", "null", "true", "false", "[]", "{}", "[[[[[[[[", "{\"a\":", "\\u0000", "\xff\xfe", "\xc3\x28", "%s%n",
        "<", ">", "</", "<?xml", "<!DOCTYPE", "&amp;", "&#0;", "]]>", ":", "- ", "  ", "\n", "\t", "#", "&a *a", "!!binary",
        "\"start\"", "\"length\"", "\"pitch\"", "\"tracks\"", "\"clips\"", "\"tempo\"", "\"version\"", "\"id\"",
        "MThd", "MTrk", "CASM", "CSEG", "Sdec", "Ctab", "Ctb2", "Cntt", "OTSc", "MDB ", "RIFF", "WAVE", "fmt ", "data",
        "<note>", "<pitch>", "<duration>", "<divisions>", "<measure>", "<part", "<score-partwise>", "<tuplet", "<chord/>",
    };

    std::uint32_t interestingValue (std::mt19937& rng)
    {
        static const std::uint32_t values[] = { 0, 1, 2, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, 0x10000,
                                                0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF, 960, 3840, 44100, 48000 };
        return values[std::uniform_int_distribution<std::size_t> (0, std::size (values) - 1) (rng)];
    }

    /** For JSON inputs: change one value deep inside, so the file still parses and the reader gets past the syntax. */
    bool mutateJson (Buffer& b, std::mt19937& rng)
    {
        nlohmann::json doc;
        try
        {
            doc = nlohmann::json::parse (b.begin(), b.end());
        }
        catch (const std::exception&)
        {
            return false;
        }
        // Collect the paths of all values, then change one.
        std::vector<nlohmann::json::json_pointer> paths;
        const std::function<void (const nlohmann::json&, const nlohmann::json::json_pointer&)> walk =
            [&] (const nlohmann::json& j, const nlohmann::json::json_pointer& at) {
                paths.push_back (at);
                if (j.is_object())
                    for (const auto& [k, v] : j.items())
                        walk (v, at / k);
                else if (j.is_array())
                    for (std::size_t i = 0; i < j.size(); ++i)
                        walk (j[i], at / i);
            };
        walk (doc, nlohmann::json::json_pointer());
        if (paths.size() < 2)
            return false;
        const auto& path = paths[1 + rng() % (paths.size() - 1)];
        auto& node = doc[path];
        static const nlohmann::json values[] = { 0, -1, 1, 127, 128, 255, 65536, 2147483647, -2147483648LL, 9223372036854775807LL,
                                                 1e308, -1e308, 0.5, "", "x", nullptr, true, false, nlohmann::json::array(),
                                                 nlohmann::json::object(), std::string (5000, 'a') };
        switch (rng() % 5)
        {
            case 0: case 1: node = values[rng() % std::size (values)]; break;
            case 2: // remove it
                if (const auto parent = path.parent_pointer(); doc[parent].is_object())
                    doc[parent].erase (path.back());
                else if (doc[parent].is_array() && ! doc[parent].empty())
                    doc[parent].erase (static_cast<std::size_t> (std::stoul (path.back())));
                break;
            case 3: // copy it many times (arrays)
                if (const auto parent = path.parent_pointer(); doc[parent].is_array())
                {
                    const auto copy = node; // `node` points into the array, which may move as it grows
                    for (int i = 0; i < 1 + static_cast<int> (rng() % 50); ++i)
                        doc[parent].push_back (copy);
                }
                break;
            case 4: // a number a bit off
                if (node.is_number_integer())
                    node = node.get<long long>() + static_cast<long long> (rng() % 5) - 2;
                else if (node.is_number_float())
                    node = node.get<double>() * -1.0;
                break;
        }
        const auto text = doc.dump();
        b.assign (text.begin(), text.end());
        return true;
    }

    void mutate (Buffer& b, const std::vector<Buffer>& pool, std::mt19937& rng)
    {
        try
        {
            if (rng() % 2 == 0 && ! b.empty() && (b.front() == '{' || b.front() == '[') && mutateJson (b, rng))
                return;
        }
        catch (const std::exception&)
        {
        }
        const auto at = [&] (std::size_t size) { return size == 0 ? std::size_t (0) : std::uniform_int_distribution<std::size_t> (0, size - 1) (rng); };
        // Mostly one change: a file with one thing wrong gets deepest into a reader.
        int count = 1;
        while (count < 8 && rng() % 3 == 0)
            ++count;
        for (int i = 0; i < count; ++i)
        {
            switch (std::uniform_int_distribution<int> (0, 11) (rng))
            {
                case 0: if (! b.empty()) b[at (b.size())] ^= static_cast<std::uint8_t> (1u << (rng() % 8)); break;
                case 1: if (! b.empty()) b[at (b.size())] = static_cast<std::uint8_t> (rng()); break;
                case 2: if (! b.empty()) b[at (b.size())] = static_cast<std::uint8_t> (interestingValue (rng)); break;
                case 3: // a 16 or 32 bit number, either byte order
                    if (b.size() >= 4)
                    {
                        const auto v = interestingValue (rng);
                        const auto pos = at (b.size() - 3);
                        const bool big = rng() % 2 == 0;
                        const int width = rng() % 2 == 0 ? 2 : 4;
                        for (int k = 0; k < width; ++k)
                            b[pos + static_cast<std::size_t> (k)] = static_cast<std::uint8_t> (v >> (8 * (big ? width - 1 - k : k)));
                    }
                    break;
                case 4: // cut a piece
                    if (! b.empty())
                    {
                        const auto pos = at (b.size());
                        const auto len = std::min (b.size() - pos, std::size_t (1) + at (64));
                        b.erase (b.begin() + static_cast<std::ptrdiff_t> (pos), b.begin() + static_cast<std::ptrdiff_t> (pos + len));
                    }
                    break;
                case 5: // copy a piece somewhere else
                    if (! b.empty() && b.size() < 1'000'000)
                    {
                        const auto from = at (b.size());
                        const auto len = std::min (b.size() - from, std::size_t (1) + at (256));
                        const Buffer piece (b.begin() + static_cast<std::ptrdiff_t> (from), b.begin() + static_cast<std::ptrdiff_t> (from + len));
                        b.insert (b.begin() + static_cast<std::ptrdiff_t> (at (b.size() + 1)), piece.begin(), piece.end());
                    }
                    break;
                case 6: // random bytes
                {
                    Buffer piece (1 + at (16));
                    for (auto& x : piece)
                        x = static_cast<std::uint8_t> (rng());
                    b.insert (b.begin() + static_cast<std::ptrdiff_t> (at (b.size() + 1)), piece.begin(), piece.end());
                    break;
                }
                case 7: case 8: // a token (numbers, JSON, XML, YAML, chunk names)
                {
                    const auto& t = tokens[at (tokens.size())];
                    const auto pos = at (b.size() + 1);
                    if (rng() % 2 == 0 && pos + t.size() <= b.size())
                        std::copy (t.begin(), t.end(), b.begin() + static_cast<std::ptrdiff_t> (pos));
                    else
                        b.insert (b.begin() + static_cast<std::ptrdiff_t> (pos), t.begin(), t.end());
                    break;
                }
                case 9: // splice with another input
                    if (! pool.empty())
                    {
                        const auto& other = pool[at (pool.size())];
                        if (! other.empty())
                        {
                            const auto from = at (other.size());
                            const auto len = std::min (other.size() - from, std::size_t (1) + at (512));
                            b.insert (b.begin() + static_cast<std::ptrdiff_t> (at (b.size() + 1)), other.begin() + static_cast<std::ptrdiff_t> (from),
                                      other.begin() + static_cast<std::ptrdiff_t> (from + len));
                        }
                    }
                    break;
                case 10: if (! b.empty()) b.resize (at (b.size())); break; // truncate
                case 11: // a digit run made larger
                    if (! b.empty())
                    {
                        const auto pos = at (b.size());
                        if (b[pos] >= '0' && b[pos] <= '9')
                            b.insert (b.begin() + static_cast<std::ptrdiff_t> (pos), { '9', '9', '9', '9', '9', '9', '9', '9', '9' });
                    }
                    break;
                default: break;
            }
        }
    }

    // ---- Running ------------------------------------------------------------------
    enum class Result { ok, finding, slow };

    Result runOne (const fuzz::Target& t, const Buffer& input, double slowSeconds, std::string& message)
    {
        current = input;
        const auto start = std::chrono::steady_clock::now();
        try
        {
            t.run (fuzz::Bytes (current.data(), current.size()));
        }
        catch (const fuzz::Finding& f)
        {
            message = f.what();
            return Result::finding;
        }
        const double took = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        if (took > slowSeconds)
        {
            message = "took " + std::to_string (took) + " s";
            return Result::slow;
        }
        return Result::ok;
    }

    void save (const fs::path& path, const Buffer& b)
    {
        std::ofstream out (path, std::ios::binary);
        out.write (reinterpret_cast<const char*> (b.data()), static_cast<std::streamsize> (b.size()));
    }

    int usage()
    {
        std::cerr << "usage: arranger_fuzz <target> [--seconds N] [--seed N] [--out folder] [--slow N]\n"
                     "       arranger_fuzz <target> --replay file...\n"
                     "       arranger_fuzz --list\n";
        return 2;
    }
}

int main (int argc, char** argv)
{
    std::vector<std::string> args (argv + 1, argv + argc);
    if (args.empty())
        return usage();
    if (args[0] == "--list")
    {
        for (const auto& t : fuzz::targets())
            std::cout << t.name << "\t" << t.what << "\n";
        return 0;
    }

    const auto& all = fuzz::targets();
    const auto it = std::find_if (all.begin(), all.end(), [&] (const fuzz::Target& t) { return args[0] == t.name; });
    if (it == all.end())
    {
        std::cerr << "unknown target " << args[0] << "\n";
        return usage();
    }
    const auto& target = *it;

    double seconds = 10.0, slowSeconds = 10.0;
    unsigned seed = static_cast<unsigned> (std::chrono::steady_clock::now().time_since_epoch().count());
    fs::path out = fs::current_path();
    std::vector<fs::path> replay;
    for (std::size_t i = 1; i < args.size(); ++i)
    {
        const auto next = [&] { return i + 1 < args.size() ? args[++i] : std::string(); };
        if (args[i] == "--seconds") seconds = std::stod (next());
        else if (args[i] == "--seed") seed = static_cast<unsigned> (std::stoul (next()));
        else if (args[i] == "--out") out = next();
        else if (args[i] == "--slow") slowSeconds = std::stod (next());
        else if (args[i] == "--replay")
        {
            while (i + 1 < args.size())
                replay.push_back (args[++i]);
        }
        else
            return usage();
    }

    fs::create_directories (out);
    std::snprintf (crashPath, sizeof (crashPath), "%s", (out / ("crash-" + std::string (target.name) + ".bin")).string().c_str());
    installCrashHandlers();

    // Replay: every file (or every file in a folder) once.
    if (! replay.empty())
    {
        int failed = 0, ran = 0;
        for (const auto& r : replay)
        {
            std::vector<fs::path> files;
            if (fs::is_directory (r))
            {
                for (const auto& e : fs::directory_iterator (r))
                    if (e.is_regular_file())
                        files.push_back (e.path());
            }
            else if (fs::exists (r))
                files.push_back (r);
            std::sort (files.begin(), files.end());
            for (const auto& f : files)
            {
                std::string message;
                ++ran;
                if (runOne (target, readFile (f), slowSeconds, message) != Result::ok)
                {
                    std::cerr << f.string() << ": " << message << "\n";
                    ++failed;
                }
            }
        }
        std::cout << target.name << ": replayed " << ran << " inputs, " << failed << " failed\n";
        return failed == 0 ? 0 : 1;
    }

    std::mt19937 rng (seed);
    auto pool = seedsFor (target.name);
    if (pool.empty())
        pool.push_back ({});
    std::cout << target.name << ": seed " << seed << ", " << pool.size() << " seed inputs, " << seconds << " s\n" << std::flush;

    // The seeds themselves must pass.
    for (const auto& s : pool)
    {
        std::string message;
        if (runOne (target, s, slowSeconds, message) != Result::ok)
        {
            save (out / ("finding-" + std::string (target.name) + "-seed.bin"), s);
            std::cerr << target.name << ": a seed input fails: " << message << "\n";
            return 1;
        }
    }

    const auto seedCount = pool.size();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double> (seconds);
    std::size_t runs = 0;
    while (std::chrono::steady_clock::now() < deadline)
    {
        Buffer input = pool[std::uniform_int_distribution<std::size_t> (0, pool.size() - 1) (rng)];
        mutate (input, pool, rng);
        std::string message;
        const auto result = runOne (target, input, slowSeconds, message);
        ++runs;
        if (result != Result::ok)
        {
            const auto kind = result == Result::finding ? "finding" : "slow";
            const auto path = out / (std::string (kind) + "-" + target.name + "-" + std::to_string (seed) + "-" + std::to_string (runs) + ".bin");
            save (path, input);
            std::cerr << target.name << ": " << kind << ": " << message << "\n  input saved: " << path.string() << "\n";
            return 1;
        }
        // Keep a few mutated inputs, so later mutations build on them (no coverage here, only variety).
        if (pool.size() < seedCount + 256 && rng() % 64 == 0)
            pool.push_back (std::move (input));
    }
    std::cout << target.name << ": " << runs << " runs, " << fuzz::acceptedCount() << " reads accepted, no problems\n";
    return 0;
}

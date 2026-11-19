// Score layout snapshots: a set of hard bars (ties over bar lines, triplets,
// beams, dotted notes, accidentals, a grand staff, drums, 6/8, marks) laid out
// and compared with the last good layout in tests/golden/score-*.json. Any
// change to where a notehead, beam, tie or rest goes shows up here.
//
// To accept a change on purpose: run with ARRANGER_UPDATE_GOLDEN=1, look at the
// diff of the JSON files, and commit them.

#include "arranger/core/Notation.h"
#include "arranger/core/ScoreLayout.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace arranger;
using namespace arranger::notation;
using nlohmann::json;

namespace
{
    constexpr Tick kQ = kTicksPerQuarter;

    Note note (Tick start, Tick length, int pitch) { return { start, length, static_cast<std::uint8_t> (pitch), 100 }; }

    double r2 (float v) { return std::round (static_cast<double> (v) * 100.0) / 100.0; }

    /** A page as sorted lists of rounded numbers: small diffs when something moves. */
    json describe (const score::Layout& layout)
    {
        json pages = json::array();
        for (const auto& p : layout.pages)
        {
            std::vector<std::vector<double>> glyphs, lines, shapes;
            std::vector<json> texts;
            for (const auto& g : p.glyphs)
                glyphs.push_back ({ static_cast<double> (g.code), r2 (g.x), r2 (g.y), r2 (g.scale) });
            for (const auto& l : p.lines)
                lines.push_back ({ r2 (l.x1), r2 (l.y1), r2 (l.x2), r2 (l.y2), r2 (l.width) });
            for (const auto& sh : p.shapes)
            {
                std::vector<double> one;
                for (const auto& [x, y] : sh.points)
                {
                    one.push_back (r2 (x));
                    one.push_back (r2 (y));
                }
                shapes.push_back (one);
            }
            for (const auto& t : p.texts)
                texts.push_back ({ t.text, r2 (t.x), r2 (t.y) });
            for (auto* v : { &glyphs, &lines, &shapes })
                std::sort (v->begin(), v->end());
            std::sort (texts.begin(), texts.end());
            pages.push_back ({ { "size", { r2 (p.width), r2 (p.height) } }, { "glyphs", glyphs }, { "lines", lines }, { "shapes", shapes }, { "texts", texts } });
        }
        return { { "systems", layout.systems }, { "pages", pages } };
    }

    /** Equal within 0.05 spaces (0.09 mm): rounding differs a little between compilers, layout may not. */
    bool near (const json& a, const json& b)
    {
        if (a.is_number() && b.is_number())
            return std::abs (a.get<double>() - b.get<double>()) <= 0.05;
        if (a.is_array() && b.is_array())
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (! near (a[i], b[i]))
                    return false;
            return true;
        }
        return a == b;
    }

    void compareWithGolden (const std::string& name, const score::Layout& layout)
    {
        INFO ("snapshot " << name);
        const auto now = describe (layout);
        const auto file = std::filesystem::path (ARRANGER_TESTS_DIR) / "golden" / ("score-" + name + ".json");
        if (std::getenv ("ARRANGER_UPDATE_GOLDEN") != nullptr || ! std::filesystem::exists (file))
        {
            std::filesystem::create_directories (file.parent_path());
            std::ofstream out (file);
            // One item per line, so a diff shows what moved.
            out << "{\n \"systems\": " << now["systems"].dump() << ",\n \"pages\": [\n";
            for (std::size_t i = 0; i < now["pages"].size(); ++i)
            {
                const auto& pg = now["pages"][i];
                out << "  {\n   \"size\": " << pg["size"].dump();
                for (const char* what : { "glyphs", "lines", "shapes", "texts" })
                {
                    out << ",\n   \"" << what << "\": [";
                    for (std::size_t k = 0; k < pg[what].size(); ++k)
                        out << (k == 0 ? "\n    " : ",\n    ") << pg[what][k].dump();
                    out << "\n   ]";
                }
                out << "\n  }" << (i + 1 < now["pages"].size() ? "," : "") << "\n";
            }
            out << " ]\n}\n";
            WARN ("Wrote " << file.string());
            return;
        }
        std::ifstream in (file);
        const auto good = json::parse (in);
        CHECK (now["systems"] == good["systems"]);
        REQUIRE (now["pages"].size() == good["pages"].size());
        for (std::size_t i = 0; i < now["pages"].size(); ++i)
            for (const char* what : { "size", "glyphs", "lines", "shapes", "texts" })
            {
                INFO ("page " << i + 1 << ", " << what);
                const auto& a = now["pages"][i][what];
                const auto& b = good["pages"][i][what];
                if (near (a, b))
                    continue;
                // Show the first differences, not two walls of numbers.
                std::string diff;
                for (std::size_t k = 0, n = 0; k < std::max (a.size(), b.size()) && n < 6; ++k)
                    if (k >= a.size() || k >= b.size() || ! near (a[k], b[k]))
                    {
                        diff += "\n  now:  " + (k < a.size() ? a[k].dump() : std::string ("-")) + "\n  good: " + (k < b.size() ? b[k].dump() : std::string ("-"));
                        ++n;
                    }
                FAIL_CHECK ("layout changed (" << a.size() << " now, " << b.size() << " before):" << diff);
            }
    }

    score::PageSetup setup (const std::string& title)
    {
        score::PageSetup s;
        s.title = title;
        s.bpm = 96;
        return s;
    }
}

TEST_CASE ("Score snapshot: ties, dots, syncopation and accidentals", "[notation][snapshot]")
{
    const std::vector<Note> notes {
        // Bar 1: dotted quarter, eighth, a half tied over the bar line.
        note (0, kQ * 3 / 2, 64), note (kQ * 3 / 2, kQ / 2, 66), note (kQ * 2, kQ * 2, 67), note (kQ * 2, kQ * 3, 71),
        // Bar 2: syncopated eighths and a held note.
        note (kQ * 4 + kQ / 2, kQ, 69), note (kQ * 5 + kQ / 2, kQ, 68), note (kQ * 6 + kQ / 2, kQ * 3 / 2, 70),
        // Bar 3: accidentals that hold for the bar, then a natural.
        note (kQ * 8, kQ / 2, 61), note (kQ * 8 + kQ / 2, kQ / 2, 61), note (kQ * 9, kQ / 2, 60), note (kQ * 9 + kQ / 2, kQ / 2, 63),
        note (kQ * 10, kQ, 75), note (kQ * 11, kQ, 51),
        // Bar 4: a chord with a second (heads on both sides) and ledger lines.
        note (kQ * 12, kQ * 4, 84), note (kQ * 12, kQ * 4, 86), note (kQ * 12, kQ * 4, 88),
    };
    compareWithGolden ("ties-accidentals", score::layout (makePart ("Melody", notes, kQ * 16, {}, keyFifths ("Bb major")), setup ("Ties")));
}

TEST_CASE ("Score snapshot: beams, sixteenths, triplets and rests", "[notation][snapshot]")
{
    std::vector<Note> notes;
    for (int i = 0; i < 8; ++i) // bar 1: eighths beamed by the beat
        notes.push_back (note (i * kQ / 2, kQ / 2, 60 + i));
    for (int i = 0; i < 16; ++i) // bar 2: sixteenths, up and down
        notes.push_back (note (kQ * 4 + i * kQ / 4, kQ / 4, 72 - (i % 8)));
    for (int i = 0; i < 6; ++i) // bar 3: eighth triplets on beats 1 and 2, rests after
        notes.push_back (note (kQ * 8 + i * kQ / 3, kQ / 3, 67 + (i % 3)));
    notes.push_back (note (kQ * 12 + kQ / 4, kQ / 4, 74)); // bar 4: a 16th off the beat
    notes.push_back (note (kQ * 13 + kQ * 3 / 4, kQ / 4, 76));
    compareWithGolden ("beams-triplets", score::layout (makePart ("Lead", notes, kQ * 16, {}, 0), setup ("Beams")));
}

TEST_CASE ("Score snapshot: grand staff, 6/8, drums and marks", "[notation][snapshot]")
{
    // A grand staff with notes crossing the split.
    std::vector<Note> piano;
    for (int b = 0; b < 4; ++b)
    {
        piano.push_back (note (b * kQ * 4, kQ * 4, 36 + b * 2));
        piano.push_back (note (b * kQ * 4, kQ * 2, 55 + b));
        for (int i = 0; i < 4; ++i)
            piano.push_back (note (b * kQ * 4 + i * kQ, kQ, 72 + i - b));
    }
    auto part = makePart ("Piano", piano, kQ * 16, {}, keyFifths ("E major"));
    part.marks = { { ScoreMark::Kind::chord, 0, "E" }, { ScoreMark::Kind::chord, kQ * 4, "C#m7" }, { ScoreMark::Kind::dynamic, 0, "mf" },
                   { ScoreMark::Kind::articulation, kQ, "staccato" }, { ScoreMark::Kind::lyric, 0, "Hel-" }, { ScoreMark::Kind::lyric, kQ, "lo" } };
    compareWithGolden ("grand-staff-marks", score::layout (part, setup ("Piano")));

    // 6/8: dotted-quarter beats.
    std::vector<Note> compound;
    for (int i = 0; i < 12; ++i)
        compound.push_back (note (i * kQ / 2, kQ / 2, 64 + (i % 6)));
    compound.push_back (note (kQ * 6, kQ * 3 / 2, 69));
    compound.push_back (note (kQ * 6 + kQ * 3 / 2, kQ * 3 / 2, 67));
    compareWithGolden ("six-eight", score::layout (makePart ("Waltz", compound, kQ * 9, { 6, 8 }, keyFifths ("D major")), setup ("Six Eight")));

    // Drums: kick, snare, hats, two crashes at once.
    std::vector<Note> drums;
    for (int i = 0; i < 16; ++i)
        drums.push_back (note (i * kQ / 2, kQ / 4, 42));
    for (int b = 0; b < 2; ++b)
    {
        drums.push_back (note (b * kQ * 4, kQ / 4, 36));
        drums.push_back (note (b * kQ * 4 + kQ * 2, kQ / 4, 36));
        drums.push_back (note (b * kQ * 4 + kQ, kQ / 4, 38));
        drums.push_back (note (b * kQ * 4 + kQ * 3, kQ / 4, 38));
    }
    drums.push_back (note (0, kQ / 4, 49));
    drums.push_back (note (0, kQ / 4, 57));
    Options o;
    o.drums = true;
    compareWithGolden ("drums", score::layout (makePart ("Drums", drums, kQ * 8, {}, 0, o), setup ("Drums")));
}

TEST_CASE ("Score snapshot: a long part breaks into systems and pages the same way", "[notation][snapshot]")
{
    std::vector<Note> notes;
    for (int i = 0; i < 48 * 8; ++i)
        notes.push_back (note (i * kQ / 2, kQ / 2, 55 + (i * 7) % 24));
    const auto layout = score::layout (makePart ("Long", notes, kQ * 4 * 48, {}, keyFifths ("A minor")), setup ("Long"));
    CHECK (layout.pages.size() >= 2);
    compareWithGolden ("long", layout);
}

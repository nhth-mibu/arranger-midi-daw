// Files at the edges: cut short at every length, very large, odd paths, and a
// save that fails. A reader must refuse a bad file with its own error, never
// crash; a save that fails must leave the old file as it was.

#include "KitchenSink.h"

#include "arranger/core/MidiFile.h"
#include "arranger/core/MusicXml.h"
#include "arranger/core/ProjectFile.h"
#include "arranger/core/Sample.h"
#include "arranger/core/Style.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

using namespace arranger;
namespace fs = std::filesystem;

namespace
{
    struct TempFolder
    {
        fs::path path;
        explicit TempFolder (const std::string& name) : path (fs::temp_directory_path() / name)
        {
            fs::remove_all (path);
            fs::create_directories (path);
        }
        ~TempFolder()
        {
            std::error_code ec;
            fs::remove_all (path, ec);
        }
    };

    std::string readText (const fs::path& p)
    {
        std::ifstream in (p, std::ios::binary);
        std::ostringstream s;
        s << in.rdbuf();
        return s.str();
    }

    Song smallSong()
    {
        Song s;
        s.title = "Short";
        auto& t = edit::addTrack (s, "Piano");
        Clip c;
        c.length = 3840;
        c.notes = { { 0, 480, 60, 100 }, { 480, 480, 64, 90 }, { 960, 1920, 67, 80 } };
        c.controls = { { 0, ControlEvent::Kind::controller, 7, 100 }, { 240, ControlEvent::Kind::pitchBend, 0, 1000 } };
        edit::addClip (s, t.id, c);
        edit::setMark (s, t.id, { ScoreMark::Kind::lyric, 0, "la" });
        edit::setMark (s, t.id, { ScoreMark::Kind::chord, 0, "C" });
        return s;
    }

    /** Every prefix of `bytes`: the reader either reads it or throws, and what it reads saves and loads. */
    template <typename Read>
    void everyPrefix (const std::vector<std::uint8_t>& bytes, Read read)
    {
        int refused = 0, read_ = 0;
        for (std::size_t n = 0; n < bytes.size(); ++n)
        {
            try
            {
                const Song s = read (std::span<const std::uint8_t> (bytes.data(), n));
                CHECK (project_file::fromJson (project_file::toJson (s)) == s);
                ++read_;
            }
            catch (const std::bad_alloc&)
            {
                FAIL ("bad_alloc at length " << n);
            }
            catch (const std::exception&)
            {
                ++refused;
            }
        }
        CHECK (refused + read_ == static_cast<int> (bytes.size()));
    }

    std::vector<std::uint8_t> bytesOf (const std::string& s) { return { s.begin(), s.end() }; }

    /** A path from UTF-8 text (std::filesystem::u8path is deprecated in C++20). */
    fs::path upath (const std::string& s) { return fs::path (std::u8string (s.begin(), s.end())); }
}

TEST_CASE ("Files cut short at any length are refused cleanly", "[edge][files]")
{
    SECTION ("project file")
    {
        everyPrefix (bytesOf (project_file::toJson (smallSong())), [] (std::span<const std::uint8_t> b) {
            return project_file::fromJson (std::string_view (reinterpret_cast<const char*> (b.data()), b.size()));
        });
    }
    SECTION ("MIDI file")
    {
        midi_file::WriteOptions o;
        o.lyrics = true;
        o.xfChords = true;
        everyPrefix (midi_file::write (smallSong(), o), [] (std::span<const std::uint8_t> b) { return midi_file::read (b); });
    }
    SECTION ("MusicXML")
    {
        everyPrefix (bytesOf (musicxml::write (smallSong())), [] (std::span<const std::uint8_t> b) {
            return musicxml::read (std::string_view (reinterpret_cast<const char*> (b.data()), b.size()));
        });
    }
    SECTION ("style file")
    {
        const auto style = style_file::load (test::examplesFolder() / "demo-style.sty");
        const auto bytes = style_file::write (style);
        // Every length near the start and the end, and a sample in between (the file is large).
        for (std::size_t n = 0; n < bytes.size(); n += (n < 2000 || n > bytes.size() - 2000) ? 1 : 97)
        {
            try
            {
                (void) style_convert::toSong (style_file::read (std::span<const std::uint8_t> (bytes.data(), n)));
            }
            catch (const std::bad_alloc&)
            {
                FAIL ("bad_alloc at length " << n);
            }
            catch (const std::exception&)
            {
            }
        }
    }
    SECTION ("WAV file")
    {
        Sample s;
        s.channels = { std::vector<float> (100, 0.5f), std::vector<float> (100, -0.5f) };
        for (const int bits : { 16, 24, 32 })
        {
            const auto bytes = wav::serialise (s, bits);
            for (std::size_t n = 0; n < bytes.size(); ++n)
            {
                try
                {
                    const auto back = wav::parse (std::vector<std::uint8_t> (bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t> (n)));
                    CHECK (back.frames() <= 100);
                }
                catch (const wav::Error&)
                {
                }
            }
        }
    }
}

TEST_CASE ("A song with 1,000 tracks and 100,000 notes saves and loads", "[edge][files]")
{
    std::mt19937 rng (1);
    Song s;
    for (int t = 0; t < 1000; ++t)
    {
        auto& track = edit::addTrack (s, "Track " + std::to_string (t));
        Clip c;
        c.length = 3840 * 64;
        for (int n = 0; n < 100; ++n)
            c.notes.push_back ({ static_cast<Tick> (rng() % (3840 * 64 - 960)), 1 + static_cast<Tick> (rng() % 960), static_cast<std::uint8_t> (rng() % 128), 100 });
        edit::normalise (c);
        edit::addClip (s, track.id, c);
    }

    TempFolder folder ("arranger-big-song");
    const auto path = folder.path / "big.arranger";
    const auto start = std::chrono::steady_clock::now();
    project_file::save (s, path);
    const auto back = project_file::load (path);
    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    CHECK (back == s);
    INFO ("save and load took " << seconds << " s");
    CHECK (seconds < 30.0); // generous: sanitizer builds are slow

    // The same as MIDI.
    const auto midi = midi_file::read (midi_file::write (s));
    std::size_t notes = 0;
    for (const auto& t : midi.tracks)
        for (const auto& c : t.clips)
            notes += c->notes.size();
    CHECK (notes > 90'000); // tracks on the same channel may merge overlapping notes
}

TEST_CASE ("Paths with Unicode, spaces and dots work for every file type", "[edge][files]")
{
    int stepNo = 0;
    const auto step = [&] (const char* what, const auto& fn) {
        ++stepNo;
        try { fn(); }
        catch (const std::exception& e) { FAIL ("step " << stepNo << " (" << what << "): " << e.what()); }
    };
    TempFolder folder ("arranger-paths");
    fs::path base;
    step ("make folder", [&] {
        base = folder.path / upath ("Lieder \xc3\xbc\xc3\xa9 \xe2\x99\xab \xe6\x97\xa5\xe6\x9c\xac ..odd.. name ");
        fs::create_directories (base);
    });
    const auto song = smallSong();

    step ("project", [&] {
        project_file::save (song, base / upath ("My Song \xc3\xa9.v2.arranger"));
        CHECK (project_file::load (base / upath ("My Song \xc3\xa9.v2.arranger")) == song);
    });
    step ("midi", [&] {
        midi_file::save (song, base / upath ("\xc3\xa9t\xc3\xa9.mid"));
        CHECK_FALSE (midi_file::load (base / upath ("\xc3\xa9t\xc3\xa9.mid")).tracks.empty());
    });
    step ("musicxml", [&] {
        musicxml::save (song, base / upath ("score \xe2\x99\xab.musicxml"));
        CHECK (fs::file_size (base / upath ("score \xe2\x99\xab.musicxml")) > 100);
    });
    step ("style", [&] {
        const auto style = style_file::load (test::examplesFolder() / "demo-style.sty");
        style_file::save (style, base / upath ("Stil \xc3\xbc.sty"));
        CHECK (style_file::load (base / upath ("Stil \xc3\xbc.sty")) == style);
    });
    step ("wav", [&] {
        Sample s;
        s.channels = { std::vector<float> (10, 0.25f) };
        const auto wavPath = (base / upath ("take \xc3\xa9.wav")).u8string();
        wav::write (s, std::string (wavPath.begin(), wavPath.end()));
        CHECK (wav::read (std::string (wavPath.begin(), wavPath.end())).frames() == 10);
    });
}

TEST_CASE ("A save that fails leaves the old file as it was", "[edge][files]")
{
    TempFolder folder ("arranger-failed-save");
    const auto path = folder.path / "song.arranger";
    project_file::save (smallSong(), path);
    const auto before = readText (path);

    // Block the temporary file with a folder of the same name: the write must fail.
    auto temp = path;
    temp += ".saving";
    fs::create_directories (temp);

    Song changed = smallSong();
    changed.title = "Changed";
    CHECK_THROWS_AS (project_file::save (changed, path), project_file::Error);
    CHECK (readText (path) == before);
    CHECK (project_file::load (path).title == "Short");
}

TEST_CASE ("Saving into a folder that is not there, or onto a folder, fails with a clear error", "[edge][files]")
{
    TempFolder folder ("arranger-bad-target");
    CHECK_THROWS_AS (project_file::save (smallSong(), folder.path / "missing" / "song.arranger"), project_file::Error);
    fs::create_directories (folder.path / "a folder.arranger");
    CHECK_THROWS_AS (project_file::save (smallSong(), folder.path / "a folder.arranger"), project_file::Error);
    CHECK_THROWS (project_file::load (folder.path / "a folder.arranger"));
    CHECK_THROWS (project_file::load (folder.path / "nothing here.arranger"));
}

TEST_CASE ("A project that names missing audio and sample files still opens", "[edge][files]")
{
    auto s = test::kitchenSinkSong(); // its audio and sample paths do not exist on this computer
    TempFolder folder ("arranger-missing-audio");
    project_file::save (s, folder.path / "song.arranger");
    const auto back = project_file::load (folder.path / "song.arranger");
    CHECK (back.tracks.size() == s.tracks.size());
}

TEST_CASE ("Extreme values in a project file are clamped, not trusted", "[edge][files]")
{
    auto json = project_file::toJson (smallSong());
    // Replace numbers with extremes, one field at a time, and check the result is usable.
    for (const auto* field : { "\"pitch\": ", "\"velocity\": ", "\"length\": ", "\"start\": ", "\"channel\": ", "\"volume\": ", "\"bpm\": ", "\"numerator\": ", "\"denominator\": " })
        for (const auto* value : { "-1", "0", "2147483647", "-9223372036854775808", "1e308", "-1e308", "0.5" })
        {
            auto text = json;
            for (auto at = text.find (field); at != std::string::npos; at = text.find (field, at + 1))
            {
                const auto from = at + std::string (field).size();
                const auto to = text.find_first_of (",\n}", from);
                text.replace (from, to - from, value);
            }
            INFO (field << value);
            try
            {
                const auto s = project_file::fromJson (text);
                CHECK (s.timeSignature.isValid());
                CHECK (s.baseTempo() > 0.0);
                for (const auto& t : s.tracks)
                {
                    CHECK (t.output.channel >= 1);
                    CHECK (t.output.channel <= 16);
                    for (const auto& c : t.clips)
                        for (const auto& n : c->notes)
                        {
                            CHECK (n.pitch <= 127);
                            CHECK (n.length > 0);
                        }
                }
                CHECK (project_file::fromJson (project_file::toJson (s)) == s);
            }
            catch (const project_file::Error&)
            {
            }
        }
}

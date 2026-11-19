// The PSR-S970 song files the app writes keep the rules the keyboard's song
// player needs (PSR-S970 Reference Manual and Data List, MIDI Data Format):
// a strict Standard MIDI File, format 0, one track; XG System On first; each
// channel gets its voice (bank MSB, LSB, program) in the setup bar before its
// first note; every note ends; data bytes stay under 128; one End of Track.
// Checked for every demo song and the kitchen-sink song.

#include "KitchenSink.h"

#include "arranger/core/PsrExport.h"
#include "arranger/core/ProjectFile.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

using namespace arranger;
namespace fs = std::filesystem;

namespace
{
    DeviceLibrary library()
    {
        DeviceLibrary lib;
        std::ifstream in (fs::path (ARRANGER_DEVICES_DIR) / "yamaha-psr-s970.yaml", std::ios::binary);
        std::ostringstream s;
        s << in.rdbuf();
        lib.add (device_profile::parse (s.str()));
        return lib;
    }

    struct Event
    {
        std::uint64_t at = 0;
        std::uint8_t status = 0;
        std::vector<std::uint8_t> data; ///< for meta: the type first
    };

    /** A strict reader: any break of the SMF rules fails the test with where it is. */
    std::vector<Event> parseStrict (const std::vector<std::uint8_t>& f, int& division)
    {
        std::size_t i = 0;
        const auto need = [&] (std::size_t n) { REQUIRE (i + n <= f.size()); };
        const auto be = [&] (int n) {
            need (static_cast<std::size_t> (n));
            std::uint32_t v = 0;
            for (int k = 0; k < n; ++k)
                v = (v << 8) | f[i++];
            return v;
        };
        const auto vlq = [&] {
            std::uint32_t v = 0;
            for (int k = 0; k < 4; ++k)
            {
                need (1);
                const auto b = f[i++];
                v = (v << 7) | (b & 0x7f);
                if ((b & 0x80) == 0)
                    return v;
            }
            FAIL ("a variable-length number longer than 4 bytes at " << i);
            return v;
        };

        REQUIRE (be (4) == 0x4d546864); // MThd
        REQUIRE (be (4) == 6);
        CHECK (be (2) == 0);            // format 0
        CHECK (be (2) == 1);            // one track
        division = static_cast<int> (be (2));
        CHECK ((division & 0x8000) == 0); // ticks per quarter, not SMPTE
        REQUIRE (be (4) == 0x4d54726b); // MTrk
        const auto length = be (4);
        REQUIRE (i + length == f.size()); // nothing after the track

        std::vector<Event> out;
        std::uint64_t at = 0;
        std::uint8_t running = 0;
        bool ended = false;
        while (i < f.size())
        {
            REQUIRE_FALSE (ended); // End of Track is the last event
            at += vlq();
            need (1);
            Event e;
            e.at = at;
            if (f[i] < 0x80)
            {
                REQUIRE (running != 0); // running status needs an earlier status
                e.status = running;
            }
            else
                e.status = f[i++];
            if (e.status == 0xff)
            {
                need (1);
                e.data.push_back (f[i++]);
                const auto n = vlq();
                need (n);
                e.data.insert (e.data.end(), f.begin() + static_cast<long> (i), f.begin() + static_cast<long> (i + n));
                i += n;
                running = 0;
                if (e.data[0] == 0x2f)
                {
                    CHECK (n == 0);
                    ended = true;
                }
            }
            else if (e.status == 0xf0 || e.status == 0xf7)
            {
                const auto n = vlq();
                need (n);
                e.data.assign (f.begin() + static_cast<long> (i), f.begin() + static_cast<long> (i + n));
                i += n;
                CHECK (e.data.back() == 0xf7); // a whole SysEx message in one event
                for (std::size_t k = 0; k + 1 < e.data.size(); ++k)
                    CHECK (e.data[k] < 0x80);
                running = 0;
            }
            else
            {
                REQUIRE (e.status >= 0x80);
                REQUIRE (e.status < 0xf0);
                const int kind = e.status & 0xf0;
                const std::size_t n = (kind == 0xc0 || kind == 0xd0) ? 1 : 2;
                need (n);
                for (std::size_t k = 0; k < n; ++k)
                {
                    CHECK (f[i] < 0x80);
                    e.data.push_back (f[i++]);
                }
                running = e.status;
            }
            out.push_back (std::move (e));
        }
        CHECK (ended);
        return out;
    }

    void checkFile (const Song& song)
    {
        const auto lib = library();
        const auto plan = psr_export::plan (song, lib);
        int division = 0;
        const auto events = parseStrict (psr_export::write (song, plan), division);
        REQUIRE_FALSE (events.empty());
        CHECK (division > 0);
        CHECK (division <= 1920);
        const auto bar = static_cast<std::uint64_t> (division * 4 * song.timeSignature.numerator / song.timeSignature.denominator);

        // XG System On comes before any other message (only meta events, such as the name, may come first), at the start.
        const auto first = std::find_if (events.begin(), events.end(), [] (const Event& e) { return e.status != 0xff; });
        REQUIRE (first != events.end());
        CHECK (first->at == 0);
        CHECK (first->status == 0xf0);
        CHECK (first->data == std::vector<std::uint8_t> { 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0xf7 });

        // A tempo and a time signature before any note.
        bool tempo = false, meter = false;
        std::map<int, std::vector<int>> voice; // channel -> [msb, lsb, program] as sent
        std::map<std::pair<int, int>, int> down; // (channel, note) -> how many notes sound
        std::uint64_t firstNote = ~0ull;
        for (const auto& e : events)
        {
            if (e.status == 0xff && e.data[0] == 0x51) tempo = true;
            if (e.status == 0xff && e.data[0] == 0x58) meter = true;
            if (e.status >= 0xf0)
                continue;
            const int ch = (e.status & 0x0f) + 1, kind = e.status & 0xf0;
            if (kind == 0xb0 && e.data[0] == 0)
                voice[ch] = { e.data[1], -1, -1 };
            else if (kind == 0xb0 && e.data[0] == 32 && voice.count (ch) != 0)
                voice[ch][1] = e.data[1];
            else if (kind == 0xc0 && voice.count (ch) != 0)
                voice[ch][2] = e.data[0];
            else if (kind == 0x90 && e.data[1] > 0)
            {
                INFO ("channel " << ch << " note " << int (e.data[0]) << " at " << e.at);
                CHECK (tempo);
                CHECK (meter);
                // The voice came first, all three parts, in the setup bar.
                REQUIRE (voice.count (ch) == 1);
                CHECK (voice[ch][1] >= 0);
                CHECK (voice[ch][2] >= 0);
                CHECK (e.at >= bar); // the music starts at bar 2
                firstNote = std::min (firstNote, e.at);
                ++down[{ ch, e.data[0] }];
            }
            else if (kind == 0x80 || (kind == 0x90 && e.data[1] == 0))
                --down[{ ch, e.data[0] }];
        }
        CHECK (firstNote != ~0ull);
        for (const auto& [key, n] : down)
        {
            INFO ("channel " << key.first << " note " << key.second);
            CHECK (n == 0); // every note ends
        }

        // Each part plays a voice of the keyboard, on a channel of its own.
        std::map<int, int> perChannel;
        for (const auto& p : plan.parts)
        {
            ++perChannel[p.channel];
            const auto& profile = *lib.byId (psr_export::kProfileId);
            bool known = false;
            for (const auto& g : profile.groups)
                for (const auto& s : g.sounds)
                    known = known || (s.bankMsb == p.voice.bankMsb && s.bankLsb == p.voice.bankLsb && s.program == p.voice.program);
            INFO ("part on channel " << p.channel << ": " << p.voice.name);
            CHECK (known);
        }
        for (const auto& [ch, n] : perChannel)
        {
            CHECK (ch >= 1);
            CHECK (ch <= 16);
            CHECK (n == 1);
        }
    }
}

TEST_CASE ("PSR-S970 song files keep the keyboard's rules, for every demo song", "[psr]")
{
    for (const auto& e : fs::recursive_directory_iterator (test::examplesFolder()))
    {
        if (e.path().extension() != project_file::kExtension)
            continue;
        INFO (e.path().filename().string());
        const auto song = project_file::load (e.path());
        bool notes = false;
        for (const auto& t : song.tracks)
            notes = notes || (t.output.kind != TrackOutput::Kind::audio && ! t.clips.empty());
        if (notes)
            checkFile (song);
    }
}

TEST_CASE ("PSR-S970 song file of the kitchen-sink song keeps the keyboard's rules", "[psr]")
{
    checkFile (test::kitchenSinkSong());
}

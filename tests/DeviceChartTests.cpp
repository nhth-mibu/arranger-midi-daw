// The device profiles against each manual's MIDI chart: what the app sends to
// the keyboard for a sound (bank select MSB, LSB, program change), the mixer
// controllers, drum kits and the reset message.
//
// Each table row is copied from the manual (page given). The whole sound lists
// were also compared with the manuals when this test was written: all 384 D-05
// presets and all PSR-S970 Data List voices match the profiles.

#include "arranger/core/DeviceProfile.h"
#include "arranger/engine/Playback.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <tuple>

using namespace arranger;

namespace
{
    DeviceProfile load (const char* name)
    {
        std::ifstream in (std::filesystem::path (ARRANGER_DEVICES_DIR) / name, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return device_profile::parse (text.str());
    }

    /** A row of a manual's voice list: the program number as the manual prints it (1-128). */
    struct ChartRow
    {
        const char* name;
        int msb, lsb, program;
    };

    /** The bytes the app sends for a sound on a channel, as the engine makes them at song start. */
    std::vector<std::vector<std::uint8_t>> sentFor (const DeviceProfile& p, const ChartRow& row, int channel)
    {
        for (const auto& g : p.groups)
            for (const auto& s : g.sounds)
                if (s.name == row.name && s.bankMsb == row.msb && s.bankLsb == row.lsb && s.program + 1 == row.program)
                {
                    Track t;
                    t.output = { TrackOutput::Kind::midiPort, "port", channel };
                    t.sound = p.trackSound (g, s);
                    std::vector<std::vector<std::uint8_t>> out;
                    for (const auto& e : trackSetupMessages (t, { -1, -1, -1, -1 }))
                        out.emplace_back (e.bytes.begin(), e.bytes.begin() + e.size);
                    return out;
                }
        return {};
    }

    void checkChart (const DeviceProfile& p, const std::vector<ChartRow>& rows, int channel)
    {
        const auto ch = static_cast<std::uint8_t> (channel - 1);
        for (const auto& row : rows)
        {
            INFO (p.name << ": " << row.name << " " << row.msb << "/" << row.lsb << "/" << row.program);
            const auto sent = sentFor (p, row, channel);
            REQUIRE (sent.size() == 3);
            CHECK (sent[0] == std::vector<std::uint8_t> { static_cast<std::uint8_t> (0xb0 | ch), 0, static_cast<std::uint8_t> (row.msb) });
            CHECK (sent[1] == std::vector<std::uint8_t> { static_cast<std::uint8_t> (0xb0 | ch), 32, static_cast<std::uint8_t> (row.lsb) });
            CHECK (sent[2] == std::vector<std::uint8_t> { static_cast<std::uint8_t> (0xc0 | ch), static_cast<std::uint8_t> (row.program - 1) });
        }
    }

    /** No two sounds of a profile send the same bank and program under different names in one group. */
    void checkNoClashes (const DeviceProfile& p)
    {
        for (const auto& g : p.groups)
        {
            std::set<std::tuple<int, int, int>> seen;
            for (const auto& s : g.sounds)
            {
                INFO (p.name << " / " << g.name << " / " << s.name);
                CHECK (s.program >= 0);
                CHECK (s.program <= 127);
                CHECK (s.bankMsb <= 127);
                CHECK (s.bankLsb <= 127);
                CHECK (seen.insert ({ s.bankMsb, s.bankLsb, s.program }).second);
            }
        }
    }
}

TEST_CASE ("PSR-S970: voices send the bank and program of the Data List", "[devices][charts]")
{
    const auto p = load ("yamaha-psr-s970.yaml");
    // PSR-S970 Data List, Voice List (pages 2-13).
    checkChart (p,
                { { "ConcertGrand", 0, 122, 1 },
                  { "PopGrand", 104, 11, 1 },
                  { "GrandPiano", 0, 113, 1 },
                  { "Harpsichord", 8, 32, 113 },
                  { "SuitcaseEP", 0, 118, 5 },
                  { "OrganAccomp4", 0, 105, 18 },
                  { "WhiterBarsFast", 104, 0, 18 },
                  { "RockHero", 8, 35, 6 },
                  { "ConcertGuitar", 8, 32, 1 },
                  { "TenorSax", 0, 117, 67 },
                  { "OrchestraHit", 0, 112, 56 },
                  { "TeknoBass", 104, 7, 39 },
                  { "60sCleanGuitar", 0, 117, 28 },
                  { "ChurchOrgan", 0, 115, 20 } },
                3);
    // Drum kits: bank MSB 127 (and 126 for SFX kits), on any channel.
    checkChart (p, { { "StandardKit1", 127, 0, 1 }, { "StandardKit2", 127, 0, 2 }, { "HouseKit", 127, 0, 61 }, { "CymbalKit", 126, 0, 50 } }, 10);
    checkChart (p, { { "StandardKit1", 127, 0, 1 } }, 5);
    // GM2 sounds and kits.
    checkChart (p, { { "StandardSet", 120, 0, 1 }, { "RoomSet", 120, 0, 9 }, { "PowerSet", 120, 0, 17 } }, 10);

    // XG System On (Data List, MIDI Data Format: F0 43 1n 4C 00 00 7E 00 F7), and the XG mixer controllers.
    REQUIRE (p.reset.size() == 1);
    CHECK (p.reset[0] == std::vector<std::uint8_t> { 0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0xf7 });
    CHECK (p.mixer.volume == 7);
    CHECK (p.mixer.pan == 10);
    CHECK (p.mixer.reverb == 91);
    CHECK (p.mixer.chorus == 93);
    CHECK (p.drumChannels == std::vector<int> { 10 });
    CHECK (p.soundCount() > 1700);
    checkNoClashes (p);
}

TEST_CASE ("D-05: patches send bank MSB 87 and LSB 8-13 for Preset 1-6", "[devices][charts]")
{
    const auto p = load ("roland-d-05.yaml");
    // D-05 Parameter Guide, MIDI Implementation, Bank Select (page 22), and Sound List.
    checkChart (p,
                { { "Fantasia", 87, 8, 1 },
                  { "Metal Harp", 87, 8, 2 },
                  { "Slow Rotor", 87, 8, 8 },
                  { "DigitalNativeDance", 87, 8, 9 },
                  { "PressureMe Strings", 87, 8, 20 },
                  { "Piano-Fifty", 87, 8, 32 } },
                1);
    // All six preset banks, 64 patches each (patch numbers 11-88).
    std::map<int, int> perBank;
    for (const auto& g : p.groups)
        for (const auto& s : g.sounds)
        {
            CHECK (s.bankMsb == 87);
            CHECK (s.program >= 0);
            CHECK (s.program < 64);
            ++perBank[s.bankLsb];
        }
    CHECK (perBank == std::map<int, int> { { 8, 64 }, { 9, 64 }, { 10, 64 }, { 11, 64 }, { 12, 64 }, { 13, 64 } });

    // The D-05 receives Volume (CC 7) but no pan, reverb or chorus controllers; no drum channel, no reset.
    CHECK (p.mixer.volume == 7);
    CHECK (p.mixer.pan == -1);
    CHECK (p.mixer.reverb == -1);
    CHECK (p.mixer.chorus == -1);
    CHECK (p.drumChannels.empty());
    CHECK (p.reset.empty());
    checkNoClashes (p);
}

TEST_CASE ("XioSynth: the mixer uses the controllers of its MIDI chart", "[devices][charts]")
{
    const auto p = load ("novation-xiosynth.yaml");
    // XioSynth MIDI chart: CC 7 is transmit only; 119 = mix output level, 10 = pan,
    // 91 = reverb send, 93 = chorus send.
    CHECK (p.mixer.volume == 119);
    CHECK (p.mixer.pan == 10);
    CHECK (p.mixer.reverb == 91);
    CHECK (p.mixer.chorus == 93);
    CHECK (p.capabilities.polyphony == 8);
    checkNoClashes (p);
}

TEST_CASE ("The generic and Korg profiles send the standard reset messages", "[devices][charts]")
{
    using Bytes = std::vector<std::uint8_t>;
    // GS Reset (Roland), XG System On (Yamaha), GM System On (MIDI standard).
    CHECK (load ("generic-gs.yaml").reset == std::vector<Bytes> { { 0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7 } });
    CHECK (load ("generic-xg.yaml").reset == std::vector<Bytes> { { 0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0xf7 } });
    CHECK (load ("korg-pa.yaml").reset == std::vector<Bytes> { { 0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7 } });

    // The GS checksum: the low 7 bits of the address and data bytes plus it give 0.
    const auto gs = load ("generic-gs.yaml").reset[0];
    int sum = 0;
    for (std::size_t i = 5; i < gs.size() - 1; ++i)
        sum += gs[i];
    CHECK (sum % 128 == 0);

    for (const auto* file : { "generic-gs.yaml", "generic-xg.yaml", "korg-pa.yaml" })
    {
        const auto p = load (file);
        INFO (file);
        CHECK (p.drumChannels == std::vector<int> { 10 });
        CHECK (p.mixer.volume == 7);
        CHECK (p.mixer.pan == 10);
        CHECK (p.mixer.reverb == 91);
        CHECK (p.mixer.chorus == 93);
        checkNoClashes (p);
    }
}

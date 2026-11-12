#include "arranger/core/DeviceProfile.h"
#include "arranger/engine/Playback.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace arranger;

namespace
{
    std::string readFile (const std::filesystem::path& p)
    {
        std::ifstream in (p, std::ios::binary);
        std::ostringstream s;
        s << in.rdbuf();
        return s.str();
    }

    DeviceProfile load (const char* name) { return device_profile::parse (readFile (std::filesystem::path (ARRANGER_DEVICES_DIR) / name)); }

    const ProfileSound* find (const DeviceProfile& p, const std::string& name)
    {
        for (const auto& g : p.groups)
            for (const auto& s : g.sounds)
                if (s.name == name)
                    return &s;
        return nullptr;
    }
}

TEST_CASE ("Every bundled device profile loads", "[devices]")
{
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator (ARRANGER_DEVICES_DIR))
    {
        if (entry.path().extension() != ".yaml")
            continue;
        INFO (entry.path().filename().string());
        const auto p = device_profile::parse (readFile (entry.path()));
        CHECK (entry.path().stem().string() == p.id);
        CHECK (p.soundCount() > 0);
        ++count;
    }
    CHECK (count >= 5);
}

TEST_CASE ("PSR-S970 profile has the Data List voices", "[devices]")
{
    const auto p = load ("yamaha-psr-s970.yaml");
    CHECK (p.dialect == "xg");
    CHECK (p.soundCount() > 1500);
    const auto* grand = find (p, "ConcertGrand");
    REQUIRE (grand != nullptr);
    CHECK (grand->bankMsb == 0);
    CHECK (grand->bankLsb == 122);
    CHECK (grand->program == 0); // printed as 1
    CHECK (p.matchesPort ("Digital Workstation-1"));
    CHECK (p.reset.front().front() == 0xf0);
}

TEST_CASE ("D-05 profile skips controllers it doesn't take", "[devices]")
{
    const auto p = load ("roland-d-05.yaml");
    CHECK (p.soundCount() == 384);
    CHECK (p.mixer.volume == 7);
    CHECK (p.mixer.pan == -1);
    CHECK (p.mixer.reverb == -1);
    const auto* fantasia = find (p, "Fantasia");
    REQUIRE (fantasia != nullptr);
    CHECK (fantasia->bankMsb == 87);
    CHECK (fantasia->bankLsb == 8);
    CHECK (fantasia->program == 0);

    Track t;
    t.output.kind = TrackOutput::Kind::midiPort;
    t.output.port = "D-05";
    const auto msgs = trackSetupMessages (t, p.mixer);
    for (const auto& m : msgs)
        if ((m.bytes[0] & 0xf0) == 0xb0)
            CHECK ((m.bytes[1] == 7 || m.bytes[1] == 0 || m.bytes[1] == 32));
}

TEST_CASE ("XioSynth volume goes to CC 119", "[devices]")
{
    const auto p = load ("novation-xiosynth.yaml");
    CHECK (p.mixer.volume == 119);
    CHECK (p.groups.at (1).sounds.at (0).bankLsb == 1);
    CHECK (p.groups.at (1).sounds.at (0).bankMsb == -1);
}

TEST_CASE ("Library picks the profile for a port, General MIDI otherwise", "[devices]")
{
    DeviceLibrary lib;
    lib.add (load ("yamaha-psr-s970.yaml"));
    lib.add (load ("roland-d-05.yaml"));
    CHECK (lib.forPort ("PSR-S970 MIDI 1").id == "yamaha-psr-s970");
    CHECK (lib.forPort ("Boutique D-05").id == "roland-d-05");
    CHECK (lib.forPort ("Some Synth").id == "general-midi");
    CHECK (lib.forPort ("Some Synth").soundCount() == 128);
}

TEST_CASE ("Profile errors say where", "[devices]")
{
    CHECK_THROWS_AS (device_profile::parse ("id: x\nname: y\n"), device_profile::Error);
    try
    {
        device_profile::parse ("profile: 1\nid: x\nname: y\nsounds:\n  - group: A\n    items:\n      - [Piano, 0, 0, 200]\n");
        FAIL ("should throw");
    }
    catch (const device_profile::Error& e)
    {
        CHECK (std::string (e.what()).find ("Line 7") != std::string::npos);
    }
    CHECK (device_profile::parseHex ("F0 7E 7F 09 01 F7").size() == 6);
    CHECK_THROWS_AS (device_profile::parseHex ("F0 7E"), device_profile::Error);
}

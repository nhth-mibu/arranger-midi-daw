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
    REQUIRE (p.editor.pages.size() == 16);
    const auto* volume = p.editor.find ("XG Part 1/Volume");
    REQUIRE (volume != nullptr);
    CHECK (volume->address == MidiBytes { 0x08, 0x00, 0x0b });
    const auto* part16 = p.editor.find ("XG Part 16/Pan");
    REQUIRE (part16 != nullptr);
    CHECK (part16->address == MidiBytes { 0x08, 0x0f, 0x0c });
}

TEST_CASE ("D-05 patch MIDI settings are in the editor", "[devices]")
{
    const auto p = load ("roland-d-05.yaml");
    const auto* tx = p.editor.find ("Patch/Transmit CH");
    REQUIRE (tx != nullptr);
    CHECK (tx->number == 0);
    CHECK (tx->address == MidiBytes { 0x00, 0x03, 0x25 });
    CHECK (tx->choices.size() == 17);
    CHECK (tx->choiceValue (0) == 0);
    CHECK (tx->choiceValue (16) == 16);

    const auto* sep = p.editor.find ("Patch/Separate Receive CH");
    REQUIRE (sep != nullptr);
    CHECK (sep->address == MidiBytes { 0x00, 0x03, 0x26 });
    CHECK (sep->choices.size() == 17);
    CHECK (sep->choices.front() == "Off");

    const auto* pc = p.editor.find ("Patch/Transmit Program Change");
    REQUIRE (pc != nullptr);
    CHECK (pc->address == MidiBytes { 0x00, 0x03, 0x27 });
    CHECK (pc->choices.size() == 101);
    CHECK (pc->choices.back() == "100");
}

TEST_CASE ("D-05 profile skips controllers it doesn't take", "[devices]")
{
    const auto p = load ("roland-d-05.yaml");
    CHECK (p.soundCount() == 384);
    CHECK (p.mixer.volume == 7);
    CHECK (p.mixer.pan == -1);
    CHECK (p.mixer.reverb == -1);
    CHECK (p.receiveChannel == 1);
    CHECK (p.editor.nameFields.size() == 3);
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

TEST_CASE ("XioSynth has its factory patch names", "[devices]")
{
    // From Novation's factory bank dumps: 2 banks of 100 patches.
    const auto p = load ("novation-xiosynth.yaml");
    REQUIRE (p.groups.size() >= 2);
    CHECK (p.groups.at (0).name == "Bank A");
    CHECK (p.groups.at (0).sounds.size() == 100);
    CHECK (p.groups.at (1).sounds.size() == 100);

    const auto& a = p.groups.at (0).sounds;
    CHECK (a.front().name == "Big Bass");
    CHECK (a.front().bankLsb == 0);
    CHECK (a.front().program == 0);
    CHECK (a.back().name == "Init Program");
    CHECK (a.back().program == 99);

    const auto* b = find (p, "TB-303 Bass");
    REQUIRE (b != nullptr);
    CHECK (b->bankLsb == 1);
    CHECK (b->program == 23);
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

    // A profile chosen by hand wins over the name; an empty id goes back to the name.
    lib.assign ("USB MIDI Interface", "roland-d-05");
    CHECK (lib.forPort ("USB MIDI Interface").id == "roland-d-05");
    lib.assign ("PSR-S970 MIDI 1", "unknown-id");
    CHECK (lib.forPort ("PSR-S970 MIDI 1").id == "yamaha-psr-s970");
    lib.assign ("USB MIDI Interface", "");
    CHECK (lib.forPort ("USB MIDI Interface").id == "general-midi");
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

TEST_CASE ("Korg Pa profile uses the GM bank and drum channel 10", "[devices]")
{
    const auto p = load ("korg-pa.yaml");
    CHECK (p.dialect == "gm");
    CHECK (p.drumChannels == std::vector<int> { 10 });
    CHECK (p.matchesPort ("KORG PA5X SOUND"));
    CHECK (p.matchesPort ("Pa700 MIDI OUT"));
    CHECK (find (p, "Acoustic Grand Piano") != nullptr);
    CHECK (find (p, "Standard") != nullptr);
    CHECK (p.reset.front() == std::vector<std::uint8_t> { 0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7 });
}

TEST_CASE ("L6max controls are found by page and channel name", "[devices]")
{
    const auto p = load ("zoom-l6max.yaml");
    const SynthPage* page = nullptr;
    const auto* high = p.editor.find ("EQ High/CH3", &page);
    REQUIRE (high != nullptr);
    REQUIRE (page != nullptr);
    CHECK (page->name == "EQ High");
    CHECK (high->number == 3);

    const auto* mute = p.editor.find ("Mute/CH4");
    REQUIRE (mute != nullptr);
    CHECK (mute->number == 102);
    const auto* mono = p.editor.find ("MONO x2/CH5");
    REQUIRE (mono != nullptr);
    CHECK (mono->number == 109);
    const auto* type = p.editor.find ("EFX Type/Type");
    REQUIRE (type != nullptr);
    CHECK (type->values == std::vector<int> { 0, 21, 43, 64, 85, 107 });
    CHECK (p.editor.find ("Mute/CH9") == nullptr);

    // A user can change a CC number in ZOOM L6 Editor and in the profile. The
    // page/name lookup still finds it; the fixed-number table would not.
    auto text = readFile (std::filesystem::path (ARRANGER_DEVICES_DIR) / "zoom-l6max.yaml");
    const auto from = std::string ("name: \"CH1\", cc: 1,");
    const auto at = text.find (from);
    REQUIRE (at != std::string::npos);
    text.replace (at, from.size(), "name: \"CH1\", cc: 71,");
    const auto changed = device_profile::parse (text);
    const auto* moved = changed.editor.find ("EQ High/CH1");
    REQUIRE (moved != nullptr);
    CHECK (moved->number == 71);
}

TEST_CASE ("L6max profile has the full factory CC map", "[devices]")
{
    const auto p = load ("zoom-l6max.yaml");
    REQUIRE (p.editor.pages.size() == 15);

    std::map<int, const SynthParameter*> byCc;
    for (const auto& page : p.editor.pages)
        for (const auto& param : page.params)
        {
            CHECK (param.kind == SynthParameter::Kind::cc);
            CHECK (byCc.count (param.number) == 0);
            byCc[param.number] = &param;
        }

    CHECK (byCc.size() == 94);
    for (int base : { 1, 11, 21, 33, 41, 49, 57, 65, 73, 81 })
        for (int ch = 0; ch < 8; ++ch)
            CHECK (byCc.count (base + ch) == 1);

    for (int cc : { 93, 94, 95, 102, 103, 104, 105, 106 })
    {
        REQUIRE (byCc.count (cc) == 1);
        CHECK (byCc[cc]->values == std::vector<int> { 0, 127 });
    }
    CHECK (byCc.count (109) == 1);
    CHECK (byCc.count (110) == 1);
    CHECK (byCc.count (113) == 1);
    CHECK (byCc.count (114) == 1);
    REQUIRE (byCc.count (117) == 1);
    CHECK (byCc[117]->values == std::vector<int> { 0, 21, 43, 64, 85, 107 });
    REQUIRE (byCc.count (119) == 1);
    CHECK (byCc[119]->values == std::vector<int> { 0, 127 });
}

TEST_CASE ("L6max profile matches only the mixer control port", "[devices]")
{
    const auto p = load ("zoom-l6max.yaml");
    CHECK (p.matchesPort ("L6max Mixer Control Port"));
    CHECK (p.matchesPort ("MIDIOUT2 (ZOOM L6max)"));
    CHECK_FALSE (p.matchesPort ("L6max MIDI I/O Port"));   // the MIDI IN/OUT jacks
    CHECK_FALSE (p.matchesPort ("ZOOM L6max"));            // the same, on Windows
    CHECK_FALSE (p.matchesPort ("MIDIOUT3 (ZOOM L6max)")); // ZOOM L6 Editor's port
    CHECK (p.mixer.volume == -1);
    CHECK (p.mixer.pan == -1);

    const auto* sceneA = find (p, "Scene A");
    REQUIRE (sceneA != nullptr);
    CHECK (sceneA->program == 0);
    CHECK (sceneA->bankMsb == -1);

    // EQ HI LEVEL CH1-8 = CC 1-8, EQ MID FREQ = CC 11-18 (manual p. 129).
    REQUIRE (p.editor.pages.size() == 15);
    CHECK (p.editor.pages[0].params.size() == 8);
    CHECK (p.editor.pages[0].params[7].number == 8);
    CHECK (p.editor.pages[1].params[0].number == 11);
    CHECK (p.editor.pages[3].params[0].number == 33);
}

TEST_CASE ("Audio inputs use the interface's own channel names", "[devices]")
{
    DeviceLibrary lib;
    lib.add (load ("zoom-l6max.yaml"));

    const auto l6 = lib.forAudioDevice ("ZOOM L6max");
    REQUIRE (l6 != nullptr);
    CHECK (l6->audio.inputChannels() == 14);
    CHECK (lib.forAudioDevice ("MacBook Pro Microphone") == nullptr);

    // Multi Track mode: CH1-4 mono, CH5-8 and MASTER stereo, in the order of the manual.
    const auto in = lib.audioInputs ("ZOOM L6max", 14);
    REQUIRE (in.size() >= 9);
    CHECK (in[0] == AudioInputChoice { "CH1", 0, 1 });
    CHECK (in[3] == AudioInputChoice { "CH4", 3, 1 });
    CHECK (in[4] == AudioInputChoice { "CH5 (stereo)", 4, 2 });
    CHECK (in[7] == AudioInputChoice { "CH8 (stereo)", 10, 2 });
    CHECK (in[8] == AudioInputChoice { "MASTER (stereo)", 12, 2 });
    CHECK (in[9] == AudioInputChoice { "CH5 L (mono)", 4, 1 });

    // Stereo mix mode has 2 inputs: the names don't fit, so plain ones.
    const auto mix = lib.audioInputs ("ZOOM L6max", 2);
    REQUIRE (mix.size() == 3);
    CHECK (mix[0] == AudioInputChoice { "In 1+2", 0, 2 });
    CHECK (mix[1] == AudioInputChoice { "In 1 (mono)", 0, 1 });

    // Other devices: the driver's names when it has them.
    const auto named = lib.audioInputs ("Scarlett 2i2", 2, { "Mic 1", "Mic 2" });
    CHECK (named[0].name == "Mic 1 + Mic 2");
    CHECK (named[2].name == "Mic 2 (mono)");
    CHECK (lib.audioInputs ("Built-in", 1) == std::vector<AudioInputChoice> { { "In 1", 0, 1 } });
    CHECK (lib.audioInputs ("Nothing", 0).empty());
}

TEST_CASE ("An audio section needs device names", "[devices]")
{
    CHECK_THROWS_AS (device_profile::parse ("profile: 1\nid: x\nname: X\naudio:\n  inputs: [\"A\"]\n"), device_profile::Error);
    CHECK_THROWS_AS (device_profile::parse ("profile: 1\nid: x\nname: X\naudio:\n  devices: [X]\n  inputs:\n    - { name: A, channels: 3 }\n"), device_profile::Error);
}

TEST_CASE ("A monotimbral profile sets the track channel", "[devices]")
{
    DeviceLibrary lib;
    lib.add (load ("roland-d-05.yaml"));
    lib.add (load ("novation-xiosynth.yaml"));
    lib.add (load ("yamaha-psr-s970.yaml"));

    CHECK (lib.forPort ("D-05").receiveChannel == 1);
    CHECK (lib.forPort ("XioSynth").receiveChannel == 1);
    CHECK (lib.forPort ("PSR-S970 MIDI 1").receiveChannel == 0);
    CHECK (lib.channelForPort ("D-05", 6) == 1);
    CHECK (lib.channelForPort ("XioSynth", 3) == 1);
    CHECK (lib.channelForPort ("PSR-S970 MIDI 1", 6) == 6);
    CHECK (lib.freeChannelForPort ("D-05", {}, false) == 1);
    CHECK (lib.freeChannelForPort ("D-05", {}, true) == 0);
    CHECK (lib.freeChannelForPort ("D-05", { 1 }, false) == 2);
    CHECK (lib.freeChannelForPort ("D-05", { 1, 2 }, false) == 0);
    CHECK (lib.freeChannelForPort ("PSR-S970 MIDI 1", { 1 }, false) == 2);
    CHECK (lib.freeChannelForPort ("PSR-S970 MIDI 1", {}, true) == 10);
}

TEST_CASE ("A receive channel must be 1-16", "[devices]")
{
    const auto yaml = [] (int channel)
    {
        return "profile: 1\nid: x\nname: X\nreceiveChannel: " + std::to_string (channel) + "\n";
    };

    CHECK_NOTHROW (device_profile::parse (yaml (1)));
    CHECK_NOTHROW (device_profile::parse (yaml (16)));
    CHECK_THROWS_AS (device_profile::parse (yaml (0)), device_profile::Error);
    CHECK_THROWS_AS (device_profile::parse (yaml (17)), device_profile::Error);
}

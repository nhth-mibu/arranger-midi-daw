#include "arranger/core/DeviceProfile.h"
#include "arranger/core/SynthEditor.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace arranger;

namespace
{
    DeviceProfile load (const char* name)
    {
        std::ifstream in (std::filesystem::path (ARRANGER_DEVICES_DIR) / name, std::ios::binary);
        std::ostringstream s;
        s << in.rdbuf();
        return device_profile::parse (s.str());
    }

    std::uint8_t rolandSum (const MidiBytes& bytes)
    {
        int sum = 0;
        for (const auto b : bytes)
            sum += b;
        return static_cast<std::uint8_t> ((128 - (sum & 0x7f)) & 0x7f);
    }

    MidiBytes dt1 (std::uint8_t dev, const MidiBytes& addressAndData)
    {
        MidiBytes m { 0xf0, 0x41, dev, 0x14, 0x12 };
        m.insert (m.end(), addressAndData.begin(), addressAndData.end());
        m.push_back (rolandSum (addressAndData));
        m.push_back (0xf7);
        return m;
    }

    constexpr const char* kTiny = R"(
profile: 1
id: tiny
name: Tiny Synth
editor:
  sysex:
    write: "F0 41 {dev} 14 12 {addr} {value} {sum} F7"
    read: "F0 41 {dev} 14 11 {addr} {size} {sum} F7"
  sections:
    tone:
      - { name: Cutoff, at: "0D", max: 100 }
      - { name: Wave, at: "06", choices: [Square, Saw] }
  pages:
    - { name: Upper, base: "00 01 00", section: tone }
    - name: Knobs
      params:
        - { name: Attack, cc: 73 }
        - { name: Pan, cc: 10, offset: -64 }
        - { name: Filter Type, nrpn: 62, choices: [LP, BP, HP] }
        - { name: Sync, nrpn: 104, choices: ["Off", "On"], values: [10, 11] }
        - { name: Chorus Type, nrpn: 104, choices: [Chorus, Phaser], values: [34, 35] }
        - { name: Split, cc: 20, max: 60, noteFrom: C2 }
)";
}

TEST_CASE ("A device profile's editor builds the parameter messages", "[synth-editor]")
{
    const auto p = device_profile::parse (kTiny);
    REQUIRE (p.editor.pages.size() == 2);
    REQUIRE (p.editor.parameterCount() == 8);

    const auto* cutoff = p.editor.find ("Upper/Cutoff");
    REQUIRE (cutoff != nullptr);
    CHECK (cutoff->address == MidiBytes { 0x00, 0x01, 0x0d });
    const auto sysex = synth_edit::messages (p.editor, *cutoff, 50, 3);
    REQUIRE (sysex.size() == 1);
    CHECK (sysex[0] == dt1 (0x02, { 0x00, 0x01, 0x0d, 50 }));
    // Out of range values are clamped.
    CHECK (synth_edit::messages (p.editor, *cutoff, 120, 1)[0][8] == 100);

    const auto* attack = p.editor.find ("attack");
    REQUIRE (attack != nullptr);
    CHECK (synth_edit::messages (p.editor, *attack, 99, 2) == std::vector<MidiBytes> { { 0xb1, 73, 99 } });

    const auto* type = p.editor.find ("Filter Type");
    REQUIRE (type != nullptr);
    CHECK (type->max == 2);
    CHECK (type->display (1) == "BP");
    CHECK (synth_edit::messages (p.editor, *type, 2, 1) == std::vector<MidiBytes> { { 0xb0, 99, 0 }, { 0xb0, 98, 62 }, { 0xb0, 6, 2 } });

    const auto* sync = p.editor.find ("Sync");
    REQUIRE (sync != nullptr);
    CHECK (sync->clamp (12) == 11);
    CHECK (sync->display (11) == "On");

    CHECK (p.editor.find ("Pan")->display (64) == "0");
    CHECK (p.editor.find ("Pan")->display (70) == "+6");
    CHECK (p.editor.find ("Split")->display (0) == "C2");
    CHECK (p.editor.find ("Split")->display (13) == "C#3");
}

TEST_CASE ("The editor listener follows what the synth sends", "[synth-editor]")
{
    const auto p = device_profile::parse (kTiny);
    synth_edit::Listener listen (p.editor, 1);

    auto changes = listen.take ({ 0xb0, 73, 40 });
    REQUIRE (changes.size() == 1);
    CHECK (p.editor.pages[changes[0].page].params[changes[0].param].name == "Attack");
    CHECK (changes[0].value == 40);
    CHECK (listen.take ({ 0xb1, 73, 40 }).empty()); // another channel

    // NRPN 104 is shared: the value says which switch it is.
    CHECK (listen.take ({ 0xb0, 99, 0 }).empty());
    CHECK (listen.take ({ 0xb0, 98, 104 }).empty());
    changes = listen.take ({ 0xb0, 6, 35 });
    REQUIRE (changes.size() == 1);
    CHECK (p.editor.pages[changes[0].page].params[changes[0].param].name == "Chorus Type");

    // A SysEx dump of several bytes sets each parameter at its address.
    MidiBytes data { 0x00, 0x01, 0x06 };
    for (int i = 0x06; i <= 0x0d; ++i)
        data.push_back (i == 0x06 ? 1 : (i == 0x0d ? 77 : 0));
    changes = listen.take (dt1 (0x00, data));
    REQUIRE (changes.size() == 2);
    for (const auto& c : changes)
    {
        const auto& name = p.editor.pages[c.page].params[c.param].name;
        CHECK (c.value == (name == "Wave" ? 1 : 77));
    }

    const auto requests = synth_edit::readRequests (p.editor, 1);
    REQUIRE (requests.size() == 1);
    CHECK (requests[0] == MidiBytes { 0xf0, 0x41, 0x00, 0x14, 0x11, 0x00, 0x01, 0x06, 0x00, 0x00, 0x08, rolandSum ({ 0x00, 0x01, 0x06, 0x00, 0x00, 0x08 }), 0xf7 });
}

TEST_CASE ("Mistakes in the editor part are reported", "[synth-editor]")
{
    const auto bad = [] (const std::string& editor) {
        try
        {
            device_profile::parse ("profile: 1\nid: x\nname: X\neditor:\n" + editor);
        }
        catch (const device_profile::Error& e)
        {
            return std::string (e.what());
        }
        return std::string();
    };
    CHECK (bad ("  pages:\n    - name: A\n      params:\n        - { name: Cutoff }\n").find ("needs one of cc") != std::string::npos);
    CHECK (bad ("  pages:\n    - name: A\n      params:\n        - { name: Cutoff, at: \"10\" }\n").find ("sysex") != std::string::npos);
    CHECK (bad ("  pages:\n    - name: A\n      params:\n        - { name: C, cc: 200 }\n").find ("Line 8") != std::string::npos);
    CHECK (bad ("  pages:\n    - { name: A, section: nope }\n").find ("no section") != std::string::npos);
}

TEST_CASE ("The D-05 and XioSynth profiles have sound editors", "[synth-editor][devices]")
{
    const auto d05 = load ("roland-d-05.yaml");
    REQUIRE (d05.editor.pages.size() == 7);
    CHECK (d05.editor.parameterCount() > 250);

    // LFO-1 Rate of the Upper tone: 00 01 1A in the Parameter Guide.
    const auto* rate = d05.editor.find ("Upper Common/LFO-1 Rate");
    REQUIRE (rate != nullptr);
    CHECK (synth_edit::messages (d05.editor, *rate, 50, 1)[0] == dt1 (0x00, { 0x00, 0x01, 0x1a, 50 }));
    // Lower Partial 2 cutoff: 00 02 00 + 0D.
    CHECK (d05.editor.find ("Lower Partial 2/TVF Cutoff")->address == MidiBytes { 0x00, 0x02, 0x0d });
    CHECK (d05.editor.find ("Patch/Key Mode")->display (3) == "Separate");
    CHECK (d05.editor.find ("Upper Partial 1/Pitch Coarse")->display (24) == "C3");
    CHECK (d05.editor.find ("Upper Partial 1/Pitch Fine")->display (40) == "-10 cent");
    CHECK (synth_edit::readRequests (d05.editor, 1).size() == 7);

    const auto xio = load ("novation-xiosynth.yaml");
    CHECK (xio.editor.parameterCount() > 100);
    CHECK (xio.editor.sysex.empty());
    const auto* cutoff = xio.editor.find ("Filter/Frequency");
    REQUIRE (cutoff != nullptr);
    CHECK (cutoff->kind == SynthParameter::Kind::cc);
    CHECK (cutoff->number == 105);
}

TEST_CASE ("Sound names are read from the D-05", "[synth-editor][devices]")
{
    const auto d05 = load ("roland-d-05.yaml");
    REQUIRE (d05.editor.names);
    synth_edit::NameReader reader (d05.editor, 1);
    CHECK (reader.expected() == 8 * 64);

    // Bank select for Internal 1, one message at a time, then a request for each name.
    auto step = reader.next();
    REQUIRE (step);
    CHECK (step->bytes == MidiBytes { 0xb0, 0, 87 });
    step = reader.next();
    REQUIRE (step);
    CHECK (step->bytes == MidiBytes { 0xb0, 32, 0 });
    step = reader.next();
    REQUIRE (step);
    CHECK (step->bytes == MidiBytes { 0xc0, 0 });
    step = reader.next();
    REQUIRE (step);
    const MidiBytes first { 0x02, 0x03, 0x00, 0x00, 0x00, 18 };
    MidiBytes request { 0xf0, 0x41, 0x00, 0x14, 0x11 };
    request.insert (request.end(), first.begin(), first.end());
    request.push_back (rolandSum (first));
    request.push_back (0xf7);
    CHECK (step->bytes == request);

    // The answer for sound 2 (02 03 00 + 00 03 40): "Fat Pad" in the D-50 characters.
    const std::string charset = d05.editor.names->charset;
    MidiBytes answer { 0x02, 0x06, 0x40 };
    for (const char c : std::string ("Fat Pad           "))
        answer.push_back (static_cast<std::uint8_t> (charset.find (c)));
    reader.receive (dt1 (0x00, answer));
    REQUIRE (reader.found().size() == 1);
    CHECK (reader.found()[0].name == "Fat Pad");
    CHECK (reader.found()[0].group == "Internal 1");
    CHECK (reader.found()[0].program == 1);
    CHECK (reader.found()[0].lsb == 0);

    int steps = 4; // three bank messages and the first name request
    while (reader.next())
        ++steps;
    CHECK (steps == 8 * 67); // three bank messages and 64 names per bank
}

TEST_CASE ("The user's sounds join a device's sound list", "[devices]")
{
    auto xio = load ("novation-xiosynth.yaml");
    const auto count = xio.soundCount();

    SoundAddOn mine;
    mine.profileId = "novation-xiosynth";
    mine.merge ({ { "Bank A", { { "Analog Bass", -1, 0, 4 } } } });
    mine.merge ({ { "My Sounds", { { "Big \"Pad\"", -1, 1, 0 } } } });
    mine.merge ({ { "Bank A", { { "Analog Bass 2", -1, 0, 4 } } } }); // renamed again: still one entry
    REQUIRE (mine.groups.size() == 2);
    CHECK (mine.groups[0].sounds.size() == 1);

    // Written and read back the same.
    const auto text = device_profile::writeAddOn (mine);
    CHECK (device_profile::isSoundAddOn (text));
    const auto back = device_profile::parseAddOn (text);
    CHECK (back.profileId == "novation-xiosynth");
    REQUIRE (back.groups.size() == 2);
    CHECK (back.groups[1].sounds[0].name == "Big \"Pad\"");
    CHECK (back.groups[0].sounds[0].program == 4);

    DeviceLibrary lib;
    lib.add (xio);
    REQUIRE (lib.addSounds (back));
    const auto* p = lib.byId ("novation-xiosynth");
    CHECK (p->soundCount() == count + 1);
    CHECK (p->groups[0].sounds[4].name == "Analog Bass 2"); // A005 renamed
    CHECK (p->groups.back().name == "My Sounds");
}

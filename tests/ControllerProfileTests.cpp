#include "arranger/core/ControllerProfile.h"
#include "arranger/core/DeviceProfile.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace arranger;

namespace
{
    ControllerProfile load (const char* name)
    {
        std::ifstream in (std::filesystem::path (ARRANGER_CONTROLLERS_DIR) / name, std::ios::binary);
        std::ostringstream s;
        s << in.rdbuf();
        return controller_profile::parse (s.str());
    }
}

TEST_CASE ("Every bundled controller page loads", "[controllers]")
{
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator (ARRANGER_CONTROLLERS_DIR))
    {
        INFO (entry.path().filename().string());
        const auto p = load (entry.path().filename().string().c_str());
        CHECK (entry.path().stem().string() == p.id);
        CHECK_FALSE (p.bindings.empty());
        ++count;
    }
    CHECK (count == 3);
}

TEST_CASE ("Launchkey knobs follow the knob mode", "[controllers]")
{
    ControllerSurface s (load ("novation-launchkey-mini-mk3.yaml"));
    CHECK (s.profile().connect.front() == std::vector<std::uint8_t> { 0x9f, 0x0c, 0x7f });
    CHECK (s.profile().matchesPort ("Launchkey Mini MK3 MIDI 2"));
    CHECK_FALSE (s.profile().matchesPort ("Launchkey Mini MK3 MIDI 1"));

    auto e = s.handle ({ 0xbf, 23, 90 }); // knob 3, volume by default
    REQUIRE (e);
    CHECK (e->action == ControlAction::volume);
    CHECK (e->strip == 2);
    CHECK (e->value == 90);

    CHECK_FALSE (s.handle ({ 0xbf, 9, 3 })); // the keyboard says: knobs are now Pan
    CHECK (s.mode() == "pan");
    e = s.handle ({ 0xbf, 28, 64 });
    REQUIRE (e);
    CHECK (e->action == ControlAction::pan);
    CHECK (e->strip == 7);

    CHECK_FALSE (s.handle ({ 0xbf, 9, 1 })); // Device mode: knobs do nothing here
    CHECK_FALSE (s.handle ({ 0xbf, 21, 10 }));

    CHECK (s.handle ({ 0xbf, 115, 127 })->action == ControlAction::play);
    CHECK_FALSE (s.handle ({ 0xbf, 115, 0 })); // release
}

TEST_CASE ("X-Touch Mini layers A and B", "[controllers]")
{
    ControllerSurface s (load ("behringer-x-touch-mini.yaml"));
    CHECK (s.profile().owns (0x9a));
    CHECK (s.profile().padRows() == 4);

    auto e = s.handle ({ 0x9a, 8, 127 }); // Layer A, top row, first button
    REQUIRE (e);
    CHECK (e->action == ControlAction::launchClip);
    CHECK (e->strip == 0);
    CHECK (e->row == 2);

    e = s.handle ({ 0x9a, 15, 127 }); // top row, last button
    REQUIRE (e);
    CHECK (e->action == ControlAction::launchClip);
    CHECK (e->strip == 7);
    CHECK (e->row == 2);

    e = s.handle ({ 0x9a, 16, 127 }); // bottom row, first button
    REQUIRE (e);
    CHECK (e->action == ControlAction::launchClip);
    CHECK (e->strip == 0);
    CHECK (e->row == 3);

    e = s.handle ({ 0x9a, 23, 127 }); // bottom row, last button
    REQUIRE (e);
    CHECK (e->action == ControlAction::launchClip);
    CHECK (e->strip == 7);
    CHECK (e->row == 3);

    e = s.handle ({ 0xba, 1, 100 });
    REQUIRE (e);
    CHECK (e->action == ControlAction::volume);
    CHECK (e->strip == 0);

    CHECK (s.handle ({ 0x9a, 32, 127 })->action == ControlAction::mute);
    CHECK (s.handle ({ 0x9a, 32, 127 })->strip == 0);
    CHECK (s.handle ({ 0x9a, 40, 127 })->action == ControlAction::solo);
    CHECK (s.handle ({ 0x9a, 40, 127 })->strip == 0);
    CHECK (s.handle ({ 0xba, 18, 64 })->action == ControlAction::pan);
    CHECK (s.handle ({ 0xba, 9, 127 })->action == ControlAction::masterVolume);
    // A clip button that goes up says so (chord pads sound while held).
    const auto up = s.handle ({ 0x8a, 8, 0 });
    REQUIRE (up);
    CHECK (up->released);
    CHECK (up->action == ControlAction::launchClip);
    CHECK_FALSE (s.handle ({ 0x8a, 32, 0 }));    // a mute button going up: nothing
    CHECK_FALSE (s.handle ({ 0xb0, 1, 100 }));   // wrong channel
}

TEST_CASE ("XioSynth page only takes its own channel", "[controllers]")
{
    const auto p = load ("novation-xiosynth-49.yaml");
    CHECK (p.owns (0xbf));
    CHECK_FALSE (p.owns (0x90)); // notes on channel 1 still play tracks
}

TEST_CASE ("Controller page errors say where", "[controllers]")
{
    CHECK_THROWS_AS (controller_profile::parse ("id: x\n"), device_profile::Error);
    try
    {
        controller_profile::parse ("controller: 1\nid: x\nname: y\nports: [X]\ncontrols:\n  - { cc: 1, action: fly }\n");
        FAIL ("should throw");
    }
    catch (const device_profile::Error& e)
    {
        CHECK (std::string (e.what()).find ("Line 6") != std::string::npos);
    }
}

TEST_CASE ("Launchkey pads start clips and show the Clip grid", "[controllers]")
{
    ControllerSurface s (load ("novation-launchkey-mini-mk3.yaml"));
    CHECK (s.profile().padRows() == 2);

    auto e = s.handle ({ 0x90, 98, 127 }); // top row, pad 3
    REQUIRE (e);
    CHECK (e->action == ControlAction::launchClip);
    CHECK (e->strip == 2);
    CHECK (e->row == 0);
    e = s.handle ({ 0x90, 119, 127 }); // bottom row, pad 8
    REQUIRE (e);
    CHECK (e->strip == 7);
    CHECK (e->row == 1);
    CHECK (s.handle ({ 0xbf, 104, 127 })->action == ControlAction::launchScene);
    CHECK (s.handle ({ 0xbf, 105, 127 })->action == ControlAction::stopClips);

    Song song;
    for (const char* colour : { "coral", "teal" })
    {
        Track t;
        t.id = song.nextId++;
        t.colour = colour;
        t.slots = { nullptr, std::make_shared<const Clip>(), std::make_shared<const Clip>() };
        song.tracks.push_back (t);
    }
    // Pads show scenes 2 and 3. Track 1 plays scene 2; track 2 waits for scene 3.
    const auto lights = padLights (s.profile(), song, 0, 1, [] (int track) {
        return track == 0 ? SlotStatus { 1, SlotStatus::kNone, 0.0 } : SlotStatus { -1, 2, 0.0 };
    });
    REQUIRE (lights.size() == 16);
    CHECK (lights[0] == PadLight { 3, 96, 21 });   // plays: pulse green
    CHECK (lights[1] == PadLight { 1, 97, 33 });   // a clip: the track colour (teal)
    CHECK (lights[2] == PadLight { 1, 98, 0 });    // no track: off
    CHECK (lights[8] == PadLight { 1, 112, 5 });   // coral
    CHECK (lights[9] == PadLight { 2, 113, 21 });  // waits: flash
}

TEST_CASE ("Launchkey and X-Touch cover four scene rows without sharing a pad", "[controllers]")
{
    const auto launchkey = load ("novation-launchkey-mini-mk3.yaml");
    const auto xtouch = load ("behringer-x-touch-mini.yaml");
    CHECK (launchkey.padRows() == 2);
    CHECK (xtouch.padRows() == 4);

    std::vector<int> launchNotes, xtouchNotes;
    for (const auto& b : launchkey.bindings)
        if (b.action == ControlAction::launchClip)
            for (int i = 0; i < b.count; ++i)
                launchNotes.push_back (b.number + i);
    for (const auto& b : xtouch.bindings)
        if (b.action == ControlAction::launchClip)
            for (int i = 0; i < b.count; ++i)
                xtouchNotes.push_back (b.number + i);

    CHECK (launchNotes.size() == 16);
    CHECK (xtouchNotes.size() == 16);
    for (const int n : launchNotes)
        CHECK (std::find (xtouchNotes.begin(), xtouchNotes.end(), n) == xtouchNotes.end());
}


TEST_CASE ("A binding can point at one strip", "[controllers]")
{
    const auto p = controller_profile::parse (
        "controller: 1\n"
        "id: test-strip\n"
        "name: Test Strip\n"
        "ports: [\"Test\"]\n"
        "channel: 1\n"
        "strips: 8\n"
        "controls:\n"
        "  - { cc: 20, channel: 1, strip: 3, action: volume }\n");
    ControllerSurface s (p);
    const auto e = s.handle ({ 0xb0, 20, 100 });
    REQUIRE (e);
    CHECK (e->action == ControlAction::volume);
    CHECK (e->strip == 3);
}


TEST_CASE ("A controller page survives a write and read", "[controllers]")
{
    const auto original = load ("behringer-x-touch-mini.yaml");
    const auto again = controller_profile::parse (controller_profile::write (original));
    CHECK (again.id == original.id);
    CHECK (again.name == original.name);
    CHECK (again.ports == original.ports);
    CHECK (again.channel == original.channel);
    CHECK (again.strips == original.strips);
    REQUIRE (again.bindings.size() == original.bindings.size());
    for (std::size_t i = 0; i < again.bindings.size(); ++i)
    {
        CHECK (again.bindings[i].isNote == original.bindings[i].isNote);
        CHECK (again.bindings[i].number == original.bindings[i].number);
        CHECK (again.bindings[i].count == original.bindings[i].count);
        CHECK (again.bindings[i].firstStrip == original.bindings[i].firstStrip);
        CHECK (again.bindings[i].action == original.bindings[i].action);
        CHECK (again.bindings[i].row == original.bindings[i].row);
        CHECK (again.bindings[i].mode == original.bindings[i].mode);
    }
    CHECK (again.pads.channel == original.pads.channel);
    CHECK (again.pads.empty == original.pads.empty);
    CHECK (again.pads.clip == original.pads.clip);
    CHECK (again.pads.colours == original.pads.colours);
}

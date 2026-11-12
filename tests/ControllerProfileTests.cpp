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
    auto e = s.handle ({ 0xba, 1, 100 });
    REQUIRE (e);
    CHECK (e->action == ControlAction::volume);
    CHECK (e->strip == 0);
    CHECK (s.handle ({ 0x9a, 9, 127 })->action == ControlAction::mute);
    CHECK (s.handle ({ 0x9a, 9, 127 })->strip == 1);
    CHECK (s.handle ({ 0x9a, 23, 127 })->action == ControlAction::solo);
    CHECK (s.handle ({ 0xba, 18, 64 })->action == ControlAction::pan);
    CHECK (s.handle ({ 0xba, 9, 127 })->action == ControlAction::masterVolume);
    CHECK_FALSE (s.handle ({ 0x8a, 9, 0 }));     // note off
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

#include "arranger/core/ControllerProfile.h"
#include "arranger/core/DeviceProfile.h"
#include "arranger/core/MidiInputPolicy.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace arranger;

namespace
{
    std::string read (const std::filesystem::path& file)
    {
        std::ifstream in (file, std::ios::binary);
        std::ostringstream s;
        s << in.rdbuf();
        return s.str();
    }

    /** What the app counts as known: the bundled controller pages and device profiles. */
    bool known (const std::string& port)
    {
        static const auto pages = [] {
            std::vector<ControllerProfile> list;
            for (const auto& e : std::filesystem::directory_iterator (ARRANGER_CONTROLLERS_DIR))
                list.push_back (controller_profile::parse (read (e.path())));
            return list;
        }();
        static const auto profiles = [] {
            std::vector<DeviceProfile> list;
            for (const auto& e : std::filesystem::directory_iterator (ARRANGER_DEVICES_DIR))
                list.push_back (device_profile::parse (read (e.path())));
            return list;
        }();
        for (const auto& c : pages)
            if (c.isPortOfDevice (port))
                return true;
        for (const auto& p : profiles)
            if (p.matchesPort (port))
                return true;
        return false;
    }

    const std::function<bool (const std::string&)> knownFn = known;
}

TEST_CASE ("Known instruments and controllers are recognised by port name", "[midi-inputs]")
{
    // macOS names
    CHECK (known ("X-TOUCH MINI"));
    CHECK (known ("Launchkey Mini MK3 MIDI Port"));
    CHECK (known ("Launchkey Mini MK3 DAW Port"));
    CHECK (known ("D-05"));
    CHECK (known ("Digital Workstation Port1"));
    CHECK (known ("XioSynth 49"));
    CHECK (known ("L6max Mixer Control"));
    // Windows and Linux names
    CHECK (known ("LKMK3 MIDI"));
    CHECK (known ("MIDIIN2 (Launchkey Mini MK3)"));
    CHECK (known ("Launchkey Mini MK3:Launchkey Mini MK3 MIDI 1 20:0"));
    CHECK_FALSE (known ("IAC Driver Bus 1"));
}

TEST_CASE ("Software MIDI ports are told apart from instruments", "[midi-inputs]")
{
    for (const auto* name : { "IAC Driver Bus 1", "Network Session 1", "Midi Through Port-0", "Midi Through:Midi Through Port-0 14:0",
                              "loopMIDI Port", "LoopBe Internal MIDI", "rtpMIDI session" })
    {
        INFO (name);
        CHECK (MidiInputPolicy::looksVirtual (name));
    }
    for (const auto* name : { "X-TOUCH MINI", "Digital Piano", "KOMPLETE KONTROL M32", "Arturia KeyStep 37", "USB MIDI Interface", "nanoKONTROL2 SLIDER/KNOB" })
    {
        INFO (name);
        CHECK_FALSE (MidiInputPolicy::looksVirtual (name));
    }
}

TEST_CASE ("Any instrument comes on, with or without a profile", "[midi-inputs]")
{
    // A keyboard the app has never heard of, plugged in after the settings were saved.
    MidiInputPolicy policy;
    const auto nothingKnown = [] (const std::string&) { return false; };
    auto on = policy.update ({ { "Arturia KeyStep 37", false }, { "IAC Driver Bus 1", false } }, nothingKnown);
    CHECK (on == std::vector<std::string> { "Arturia KeyStep 37" });

    // A software port comes on when a profile or controller page names it (the user wrote one for it).
    MidiInputPolicy withPage;
    const auto page = controller_profile::parse (R"(controller: 1
id: my-bus-controller
name: Controller over IAC
ports: ["IAC Driver Bus 2"]
controls:
  - { cc: 7, action: masterVolume }
)");
    on = withPage.update ({ { "IAC Driver Bus 1", false }, { "IAC Driver Bus 2", false } },
                          [&] (const std::string& port) { return page.isPortOfDevice (port); });
    CHECK (on == std::vector<std::string> { "IAC Driver Bus 2" });
}

TEST_CASE ("A controller page added later is recognised by its own port names", "[midi-inputs]")
{
    // A future controller: nothing in the app names it, only its page.
    const auto page = controller_profile::parse (R"(controller: 1
id: future-controller
name: Future Controller
ports: ["FUTURE CTRL DAW"]
keyPorts: ["FUTURE CTRL KEYS"]
controls:
  - { cc: 9, channel: 1, action: masterVolume }
)");
    CHECK (page.isPortOfDevice ("FUTURE CTRL DAW Port"));
    CHECK (page.isPortOfDevice ("FUTURE CTRL KEYS Port"));
    CHECK_FALSE (page.matchesPort ("FUTURE CTRL KEYS Port"));
    ControllerSurface s (page);
    const auto e = s.handle ({ 0xb0, 9, 100 });
    REQUIRE (e);
    CHECK (e->action == ControlAction::masterVolume);
}

TEST_CASE ("Every bundled controller page's fader works as soon as it is plugged in", "[midi-inputs]")
{
    // For each page: its port is turned on, and each value control it maps reacts to its message.
    for (const auto& entry : std::filesystem::directory_iterator (ARRANGER_CONTROLLERS_DIR))
    {
        const auto page = controller_profile::parse (read (entry.path()));
        INFO (page.id);
        MidiInputPolicy policy;
        const std::string port = page.ports.front() + " Port";
        CHECK (policy.update ({ { port, false } }, [&] (const std::string& p) { return page.isPortOfDevice (p); }).size() == 1);
        for (const auto& b : page.bindings)
        {
            if (b.isNote || ! b.mode.empty() || b.action != ControlAction::masterVolume)
                continue;
            ControllerSurface s (page);
            const auto channel = static_cast<std::uint8_t> (b.channel == 0 ? 0 : b.channel - 1);
            const auto e = s.handle ({ static_cast<std::uint8_t> (0xb0 | channel), static_cast<std::uint8_t> (b.number), 64 });
            REQUIRE (e);
            CHECK (e->action == ControlAction::masterVolume);
            CHECK (page.owns (static_cast<std::uint8_t> (0xb0 | channel)));
        }
    }
}

TEST_CASE ("A known input that is off comes on when it appears", "[midi-inputs]")
{
    MidiInputPolicy policy;
    // At start: the X-Touch was plugged in after the settings were saved, so it is off.
    auto on = policy.update ({ { "X-TOUCH MINI", false }, { "IAC Driver Bus 1", false }, { "D-05", true } }, knownFn);
    REQUIRE (on.size() == 1);
    CHECK (on.front() == "X-TOUCH MINI");

    // Nothing changes: nothing to do.
    CHECK (policy.update ({ { "X-TOUCH MINI", true }, { "IAC Driver Bus 1", false }, { "D-05", true } }, knownFn).empty());

    // A Launchkey is plugged in while the app runs: both of its ports come on.
    on = policy.update ({ { "X-TOUCH MINI", true }, { "IAC Driver Bus 1", false }, { "D-05", true },
                          { "Launchkey Mini MK3 MIDI Port", false }, { "Launchkey Mini MK3 DAW Port", false } }, knownFn);
    CHECK (on == std::vector<std::string> { "Launchkey Mini MK3 MIDI Port", "Launchkey Mini MK3 DAW Port" });
    CHECK (policy.turnedOff().empty());
}

TEST_CASE ("An input the user turns off stays off", "[midi-inputs]")
{
    MidiInputPolicy policy;
    CHECK (policy.update ({ { "D-05", false } }, knownFn).size() == 1);
    // The user turns it off in the settings.
    CHECK (policy.update ({ { "D-05", false } }, knownFn).empty());
    CHECK (policy.turnedOff().count ("D-05") == 1);

    // Unplugged and plugged in again: still off.
    CHECK (policy.update ({}, knownFn).empty());
    CHECK (policy.update ({ { "D-05", false } }, knownFn).empty());

    // Kept in the settings: the next start leaves it off too.
    MidiInputPolicy next;
    next.setTurnedOff (policy.turnedOff());
    CHECK (next.update ({ { "D-05", false } }, knownFn).empty());

    // The user turns it on again: it is forgotten, and comes on by itself again later.
    CHECK (next.update ({ { "D-05", true } }, knownFn).empty());
    CHECK (next.turnedOff().empty());
    CHECK (next.update ({}, knownFn).empty());
    CHECK (next.update ({ { "D-05", false } }, knownFn).size() == 1);
}

TEST_CASE ("An input that could not be opened is not taken for the user's choice", "[midi-inputs]")
{
    MidiInputPolicy policy;
    REQUIRE (policy.update ({ { "X-TOUCH MINI", false } }, knownFn).size() == 1);
    policy.couldNotEnable ("X-TOUCH MINI");
    CHECK (policy.update ({ { "X-TOUCH MINI", false } }, knownFn).empty());
    CHECK (policy.turnedOff().empty());
}

TEST_CASE ("Key ports survive a write and read of the controller page", "[midi-inputs]")
{
    const auto page = controller_profile::parse (read (std::filesystem::path (ARRANGER_CONTROLLERS_DIR) / "novation-launchkey-mini-mk3.yaml"));
    REQUIRE_FALSE (page.keyPorts.empty());
    CHECK (page.isPortOfDevice ("Launchkey Mini MK3 MIDI Port"));
    CHECK_FALSE (page.matchesPort ("Launchkey Mini MK3 MIDI Port")); // the keys still play tracks
    const auto again = controller_profile::parse (controller_profile::write (page));
    CHECK (again.keyPorts == page.keyPorts);
}

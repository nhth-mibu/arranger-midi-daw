#pragma once

#include "arranger/core/Song.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace arranger
{

/**
    Which controller number carries each mixer value on a device. -1 means the
    device doesn't take it, so it isn't sent.
*/
struct MixerControls
{
    int volume = 7;
    int pan = 10;
    int reverb = 91;
    int chorus = 93;

    bool operator== (const MixerControls&) const = default;
};

struct ProfileSound
{
    std::string name;
    int bankMsb = -1;  ///< -1: not sent
    int bankLsb = -1;
    int program = -1;  ///< 0..127 (profile files use the 1-based numbers printed in manuals)
};

struct SoundGroup
{
    std::string name;
    std::vector<ProfileSound> sounds;
};

/**
    What the app knows about one MIDI device: its sound list, how it selects
    banks, which controllers its mixer listens to, and how to reset it.

    Profiles are YAML files (in resources/devices, plus the user's own
    folder), written by hand or by an AI from the device's MIDI chart. See
    docs/device-profiles.md for the format.
*/
struct DeviceProfile
{
    std::string id;            ///< "yamaha-psr-s970"
    std::string name;          ///< "Yamaha PSR-S970"
    std::string maker;
    std::string notes;
    std::string dialect = "gm"; ///< gm, gs, xg or none
    std::vector<std::string> ports;  ///< parts of MIDI port names that mean this device
    std::vector<int> drumChannels { 10 };
    MixerControls mixer;
    std::vector<std::vector<std::uint8_t>> reset; ///< SysEx messages that reset the device to its MIDI defaults
    std::vector<SoundGroup> groups;

    /** True when the port name contains one of `ports` (case is ignored). */
    bool matchesPort (const std::string& portName) const;

    std::size_t soundCount() const noexcept;

    /** The track sound for an entry: "Group > Name" plus its bank and program. */
    TrackSound trackSound (const SoundGroup&, const ProfileSound&) const;
};

namespace device_profile
{
    struct Error : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    /** Reads a profile from YAML text. Throws Error with the line number on mistakes. */
    DeviceProfile parse (const std::string& yaml);

    /** "F0 43 10 4C 00 00 7E 00 F7" to bytes. Throws Error. */
    std::vector<std::uint8_t> parseHex (const std::string&);

    /** General MIDI: the fallback when no profile matches a port. */
    DeviceProfile generalMidi();
}

/** All loaded profiles, and which one a port uses. */
class DeviceLibrary
{
public:
    /** Adds a profile, replacing one with the same id (so user files override built-ins). */
    void add (DeviceProfile);

    const DeviceProfile* byId (const std::string&) const;

    /** The profile for a port name; the General MIDI profile if none matches. */
    const DeviceProfile& forPort (const std::string& portName) const;

    /** Mixer controllers for a port (for buildSnapshot). */
    MixerControls mixerFor (const std::string& portName) const { return forPort (portName).mixer; }

    const std::vector<DeviceProfile>& all() const noexcept { return profiles; }

private:
    std::vector<DeviceProfile> profiles;
    DeviceProfile gm = device_profile::generalMidi();
};

} // namespace arranger

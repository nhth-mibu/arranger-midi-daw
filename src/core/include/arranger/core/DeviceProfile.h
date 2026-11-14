#pragma once

#include "arranger/core/Song.h"
#include "arranger/core/SynthEditor.h"

#include <cstdint>
#include <stdexcept>
#include <map>
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
    SynthEditor editor;  ///< the synth's sound settings, for the sound editor; empty: none

    /** True when the port name contains one of `ports` (case is ignored). */
    bool matchesPort (const std::string& portName) const;

    std::size_t soundCount() const noexcept;

    /** The track sound for an entry: "Group > Name" plus its bank and program. */
    TrackSound trackSound (const SoundGroup&, const ProfileSound&) const;

    /**
        Adds the user's own sounds. A group with the name of an existing one
        joins it: a sound with the same bank and program renames the one
        there, others are added. New groups come after the built-in ones.
    */
    void addSounds (const std::vector<SoundGroup>&);
};

/**
    The user's own sounds for a device (read from the synth, or named by
    hand or by an AI), kept apart from the profile so a new app version can
    still update the built-in list. A YAML file with `soundsFor: <profile id>`.
*/
struct SoundAddOn
{
    std::string profileId;
    std::vector<SoundGroup> groups;

    /** Adds or renames sounds in the same way as DeviceProfile::addSounds. */
    void merge (const std::vector<SoundGroup>&);
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

    /** True when the YAML text is a sound add-on (has a soundsFor: line). */
    bool isSoundAddOn (const std::string& yaml);
    SoundAddOn parseAddOn (const std::string& yaml);
    std::string writeAddOn (const SoundAddOn&);

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

    /** Adds the user's sounds to the profile with that id. False if there is none. */
    bool addSounds (const SoundAddOn&);

    /** The profile for a port name; the General MIDI profile if none matches. */
    const DeviceProfile& forPort (const std::string& portName) const;

    /** Mixer controllers for a port (for buildSnapshot). */
    MixerControls mixerFor (const std::string& portName) const { return forPort (portName).mixer; }

    /** Chooses the profile for one MIDI port by hand (exact port name), over the name match.
        An empty id goes back to the name match. */
    void assign (const std::string& portName, const std::string& profileId);
    /** The ports with a profile chosen by hand: port name -> profile id. */
    const std::map<std::string, std::string>& assignments() const noexcept { return assigned; }

    const std::vector<DeviceProfile>& all() const noexcept { return profiles; }
    const DeviceProfile& generalMidi() const noexcept { return gm; }

private:
    std::vector<DeviceProfile> profiles;
    std::map<std::string, std::string> assigned;
    DeviceProfile gm = device_profile::generalMidi();
};

} // namespace arranger

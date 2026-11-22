#pragma once

#include "arranger/core/Song.h"
#include "arranger/core/SynthEditor.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <map>
#include <set>
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

/**
    What a device can play at once, for the hardware check (see HardwareFit.h).
    0 means "not known": the check then says so and does not guess.
*/
struct DeviceCapabilities
{
    int polyphony = 0;    ///< notes that can sound at once over all parts
    int parts = 0;        ///< MIDI channels that can play different sounds at once
    int lowestNote = 0;   ///< MIDI note numbers the device plays
    int highestNote = 127;
    std::vector<int> controllers; ///< controller numbers it reacts to; empty = not known (not checked)
    std::string source;   ///< where the numbers come from: "manual", "user", "estimate"

    bool known() const noexcept { return polyphony > 0 || parts > 0; }
    bool operator== (const DeviceCapabilities&) const = default;
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

/** A named group of audio channels on an audio interface: one mono input, or a stereo pair. */
struct AudioChannelGroup
{
    std::string name;  ///< "CH5", "MASTER"
    int first = 0;     ///< first channel, 0-based, in the order the computer lists them
    int count = 1;     ///< 1 (mono) or 2 (stereo)

    bool operator== (const AudioChannelGroup&) const = default;
};

/**
    The audio side of a device that is also an audio interface (the `audio:`
    map of a profile): the names of its USB inputs and outputs. The names are
    used only when the open audio device matches `devices` and has exactly
    as many input channels as the list, because some interfaces have a
    second mode with fewer channels (the L6max "Stereo mix" mode has two).
*/
struct AudioInterfaceInfo
{
    std::vector<std::string> devices;      ///< parts of the audio device name, case ignored
    std::vector<AudioChannelGroup> inputs;  ///< in channel order, one after the other
    std::vector<AudioChannelGroup> outputs;
    std::string notes;

    bool empty() const noexcept { return devices.empty(); }
    bool matchesDevice (const std::string& deviceName) const;
    /** All input channels together (14 on the L6max). */
    int inputChannels() const noexcept;
};

/** One audio input that an audio track can record from. */
struct AudioInputChoice
{
    std::string name;  ///< "CH5 (stereo)", "In 1+2"
    int first = 0;     ///< first device channel, 0-based
    int count = 2;     ///< 1 or 2

    bool operator== (const AudioInputChoice&) const = default;
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
    int receiveChannel = 0; ///< 1..16 when the device is monotimbral; 0 keeps the track's channel
    std::vector<int> drumChannels { 10 };
    MixerControls mixer;
    std::vector<std::vector<std::uint8_t>> reset; ///< SysEx messages that reset the device to its MIDI defaults
    std::vector<SoundGroup> groups;
    SynthEditor editor;  ///< the synth's sound settings, for the sound editor; empty: none
    DeviceCapabilities capabilities; ///< polyphony, parts and note range (the `capabilities:` map)
    AudioInterfaceInfo audio;  ///< names of its audio inputs and outputs, if it is an audio interface

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

    /** Capabilities as JSON ({"polyphony":16,"parts":1,...}) for settings files and the AI tools, and back. */
    std::string capabilitiesJson (const DeviceCapabilities&);
    DeviceCapabilities parseCapabilitiesJson (const std::string&); ///< throws Error
}

/** All loaded profiles, and which one a port uses. */
class DeviceLibrary
{
public:
    /** Adds a profile, replacing one with the same id (so user files override built-ins). */
    void add (DeviceProfile);

    const DeviceProfile* byId (const std::string&) const;

    /** Sets what a device can play (the user's own numbers win over the profile's). False if there is no such profile. */
    bool setCapabilities (const std::string& profileId, const DeviceCapabilities&);

    /** Adds the user's sounds to the profile with that id. False if there is none. */
    bool addSounds (const SoundAddOn&);

    /** The profile for a port name; the General MIDI profile if none matches. */
    const DeviceProfile& forPort (const std::string& portName) const;

    /** Mixer controllers for a port (for buildSnapshot). */
    MixerControls mixerFor (const std::string& portName) const { return forPort (portName).mixer; }

    /** A monotimbral device's receive channel, or the current channel for a multitimbral one. */
    int channelForPort (const std::string& portName, int currentChannel) const
    {
        const auto channel = forPort (portName).receiveChannel;
        return channel > 0 ? channel : currentChannel;
    }

    /** A free channel on a device, or 0 when none fits. Monotimbral devices use their receive channel. */
    int freeChannelForPort (const std::string& portName, const std::set<int>& taken, bool drums) const
    {
        const auto& profile = forPort (portName);
        const auto isDrum = [&] (int channel) {
            return std::find (profile.drumChannels.begin(), profile.drumChannels.end(), channel) != profile.drumChannels.end();
        };
        if (profile.receiveChannel > 0)
        {
            // A device may take more than one part in another key mode. Its
            // receive channel is the first; the next ones follow it.
            const int parts = std::max (1, profile.capabilities.parts);
            for (int channel = profile.receiveChannel; channel <= 16 && channel < profile.receiveChannel + parts; ++channel)
                if (! taken.contains (channel) && isDrum (channel) == drums)
                    return channel;
            return 0;
        }
        for (int channel = 1; channel <= 16; ++channel)
            if (! taken.contains (channel) && isDrum (channel) == drums)
                return channel;
        return 0;
    }

    /** Chooses the profile for one MIDI port by hand (exact port name), over the name match.
        An empty id goes back to the name match. */
    void assign (const std::string& portName, const std::string& profileId);
    /** The ports with a profile chosen by hand: port name -> profile id. */
    const std::map<std::string, std::string>& assignments() const noexcept { return assigned; }

    /** The profile whose `audio:` part matches an audio device name, or null. */
    const DeviceProfile* forAudioDevice (const std::string& deviceName) const;

    /**
        The inputs an audio track can choose on the open audio device. With a
        matching profile (and the same channel count): its names, stereo pairs
        first as in the list. Otherwise each stereo pair, then each channel,
        named from `channelNames` (what the driver calls them) or "In 1".
    */
    std::vector<AudioInputChoice> audioInputs (const std::string& deviceName, int deviceInputs,
                                               const std::vector<std::string>& channelNames = {}) const;

    const std::vector<DeviceProfile>& all() const noexcept { return profiles; }
    const DeviceProfile& generalMidi() const noexcept { return gm; }

private:
    std::vector<DeviceProfile> profiles;
    std::map<std::string, std::string> assigned;
    DeviceProfile gm = device_profile::generalMidi();
};

} // namespace arranger

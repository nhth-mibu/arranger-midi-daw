#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace arranger
{

using MidiBytes = std::vector<std::uint8_t>;

/**
    One setting of a hardware synth's sound (cutoff, attack, waveform...) and
    the MIDI message that changes it. Comes from the `editor:` part of a
    device profile, so a new synth needs a YAML list, not code.
*/
struct SynthParameter
{
    enum class Kind { cc, nrpn, rpn, sysex };

    std::string name;
    Kind kind = Kind::cc;
    int number = 0;             ///< CC 0..127, or NRPN/RPN 0..16383 (MSB * 128 + LSB)
    MidiBytes address;          ///< SysEx: the full address (page base + offset)
    int min = 0;
    int max = 127;
    int defaultValue = 0;
    std::vector<std::string> choices; ///< names of the values; empty: a number
    std::vector<int> values;          ///< the value of each choice; empty: min, min + 1, ...
    int offset = 0;             ///< the number shown is value + offset (-64 shows 0..127 as -64..63)
    std::string unit;
    int noteFrom = -1;          ///< >= 0: show values as note names, min being this MIDI note

    bool isChoice() const noexcept { return ! choices.empty(); }
    /** The MIDI value of choice i. */
    int choiceValue (std::size_t i) const;
    /** Which choice a MIDI value is, or -1. */
    int choiceIndex (int value) const;
    /** How the value looks to the user: a choice name, or the number with offset and unit. */
    std::string display (int value) const;
    /** Puts a value inside the range (or on the nearest choice). */
    int clamp (int value) const;
};

struct SynthPage
{
    std::string name;
    std::vector<SynthParameter> params;
};

/** How a device's SysEx messages are built. Templates are hex bytes and {fields}. */
struct SysExFormat
{
    std::string write;   ///< e.g. "F0 41 {dev} 14 12 {addr} {value} {sum} F7"
    std::string read;    ///< e.g. "F0 41 {dev} 14 11 {addr} {size} {sum} F7"; empty: the synth can't be read
    int device = -1;     ///< {dev}: -1 = the MIDI channel - 1, else this number
    int addressBytes = 3;
    int sizeBytes = 3;

    bool empty() const noexcept { return write.empty(); }
};

/** A text field in a synth's memory, such as a patch or tone name. */
struct SynthNameField
{
    std::string name;
    MidiBytes address;
    int length = 16;
    std::string charset; ///< character for each byte value; empty: ASCII
};

/** How to read the sound names stored in the synth (its user memory). */
struct NameDump
{
    MidiBytes first;     ///< address of the first name
    MidiBytes stride;    ///< address step from one sound to the next (7-bit bytes)
    int count = 64;      ///< sounds in a bank
    int length = 16;     ///< characters in a name
    std::string charset; ///< character for each byte value; empty: ASCII

    struct Bank
    {
        std::string group;
        int msb = -1, lsb = -1;
    };
    /** The banks to read. Each is selected first, then read. Empty: the bank the synth is on now. */
    std::vector<Bank> banks;
};

/** All of a synth's sound settings, as pages of parameters. */
struct SynthEditor
{
    SysExFormat sysex;
    std::vector<SynthPage> pages;
    std::vector<SynthNameField> nameFields;
    std::optional<NameDump> names;

    bool empty() const noexcept { return pages.empty(); }
    std::size_t parameterCount() const noexcept;

    /** A parameter by "Page/Name" or just "Name" (case is ignored). Null if none. */
    const SynthParameter* find (const std::string& name, const SynthPage** page = nullptr) const;
};

namespace synth_edit
{
    /** The messages that set a parameter to a value. `channel` is 1..16. */
    std::vector<MidiBytes> messages (const SynthEditor&, const SynthParameter&, int value, int channel);

    /** SysEx requests that make the synth send all its current settings back. Empty if it can't. */
    std::vector<MidiBytes> readRequests (const SynthEditor&, int channel);

    /** The SysEx messages that write a name, one byte at a time. */
    std::vector<MidiBytes> nameMessages (const SynthEditor&, const SynthNameField&, const std::string& text, int channel);
    /** The text in a name answer, or empty when the message is not for that field. */
    std::string nameFromAnswer (const SynthEditor&, const SynthNameField&, const MidiBytes&, int channel);

    /** A parameter's new value, found in a message from the synth. */
    struct Change
    {
        std::size_t page = 0, param = 0;
        int value = 0;
    };

    /**
        Turns what the synth sends (knobs turned on the synth, or answers to
        readRequests) into parameter values. Keeps NRPN state between calls.
    */
    class Listener
    {
    public:
        Listener (const SynthEditor&, int channel);
        std::vector<Change> take (const MidiBytes&);

    private:
        const SynthEditor& editor;
        int channel;
        int nrpnMsb = -1, nrpnLsb = -1, rpnMsb = -1, rpnLsb = -1;
        bool rpnActive = false;
    };

    /** The data of a SysEx write message: its address and value bytes. False when it isn't one. */
    bool parseWrite (const SynthEditor&, const MidiBytes& message, int channel, MidiBytes& address, MidiBytes& data);

    /** Adds two 7-bit addresses of the same length. */
    MidiBytes addAddress (const MidiBytes& a, const MidiBytes& b);
    /** A 7-bit address times n. */
    MidiBytes multiplyAddress (const MidiBytes& a, int n);
    /** The address as one number (7 bits per byte). */
    long addressValue (const MidiBytes&);
    MidiBytes addressBytes (long value, int bytes);

    /**
        Reads the sound names stored in a synth, one bank after the other.
        The app sends each step (waiting `waitMs` before it) and gives every
        SysEx that comes back to receive().
    */
    class NameReader
    {
    public:
        NameReader (const SynthEditor&, int channel);

        struct Step
        {
            MidiBytes bytes;
            int waitMs = 0;
        };
        /** The next message to send, or nothing when done. */
        std::optional<Step> next();
        void receive (const MidiBytes&);

        struct Found
        {
            std::string group;
            std::string name;
            int msb = -1, lsb = -1, program = 0; ///< program 0..127
        };
        const std::vector<Found>& found() const noexcept { return names; }
        int expected() const noexcept;

    private:
        const SynthEditor& editor;
        int channel;
        std::size_t bank = 0, requestBank = 0;
        int index = 0, bankStage = 0;
        bool bankSelected = false;
        std::vector<Found> names;
    };
}

} // namespace arranger

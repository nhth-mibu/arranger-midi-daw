#pragma once

#include "arranger/core/Song.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace arranger
{

/** The eight parts of a Yamaha style. They play on MIDI channels 9 to 16. */
enum class StylePart : std::uint8_t { rhythm1, rhythm2, bass, chord1, chord2, pad, phrase1, phrase2 };

inline constexpr int kStylePartCount = 8;
inline constexpr const char* kStylePartNames[] = { "Rhythm 1", "Rhythm 2", "Bass", "Chord 1", "Chord 2", "Pad", "Phrase 1", "Phrase 2" };
inline constexpr int stylePartChannel (StylePart p) noexcept { return 9 + static_cast<int> (p); }

/** The section names a PSR-S970 knows, in the keyboard's order. */
inline constexpr const char* kStyleSectionNames[] = {
    "Intro A", "Intro B", "Intro C", "Intro D",
    "Main A", "Main B", "Main C", "Main D",
    "Fill In AA", "Fill In BB", "Fill In CC", "Fill In DD", "Fill In BA",
    "Ending A", "Ending B", "Ending C", "Ending D",
};

/** How one note range of a channel follows the chord (Yamaha's names). */
struct StyleTranspose
{
    enum class Ntr : std::uint8_t { rootTransposition, rootFixed, guitar };
    enum class Ntt : std::uint8_t { bypass, melody, chord, melodicMinor, melodicMinor5th, harmonicMinor, harmonicMinor5th, naturalMinor, naturalMinor5th, dorian, dorian5th };
    enum class Rtr : std::uint8_t { stop, pitchShift, pitchShiftToRoot, retrigger, retriggerToRoot, noteGenerator };

    Ntr ntr = Ntr::rootTransposition;
    Ntt ntt = Ntt::chord;
    bool bass = false;       ///< follow the bass note of an on-bass chord
    int highKey = 6;         ///< highest root (0 = C) before the part drops an octave
    int lowLimit = 0, highLimit = 127;
    Rtr rtr = Rtr::pitchShift;

    bool operator== (const StyleTranspose&) const = default;
};

/**
    The chord rules for one source channel (a "Ctab" in the CASM part of a
    style file): what chord the notes were written in, where they play, and
    how they follow the chord.
*/
struct StyleChannelRule
{
    int sourceChannel = 9;       ///< 1..16: the channel of the notes in the file
    std::string name;            ///< up to 8 characters
    int destChannel = 9;         ///< 9..16: the style part that plays them
    bool editable = true;
    std::uint16_t noteMute = 0x0fff;          ///< bit n set: plays when the chord root is note n (0 = C)
    std::uint64_t chordMute = 0xffffffffffULL; ///< the file's 5 chord-mute bytes; a set bit lets that chord type play
    int sourceRoot = 0;          ///< 0 = C
    int sourceChordType = 2;     ///< Yamaha chord type number; 2 = major 7th
    StyleTranspose transpose;    ///< the middle (or only) note range

    // SFF2 only: notes below `middleLow` use `lowRange`, notes above `middleHigh` use `highRange`.
    int middleLow = 0, middleHigh = 127;
    StyleTranspose lowRange, highRange;
    std::vector<std::uint8_t> special; ///< bytes after the known fields, kept as they are

    bool operator== (const StyleChannelRule&) const = default;
};

/** One "CSEG" of the CASM part: rules shared by the sections it names. */
struct StyleRuleSet
{
    std::vector<std::string> sections;
    std::vector<StyleChannelRule> rules;
    bool operator== (const StyleRuleSet&) const = default;
};

/** A MIDI message kept as it is: a channel message, a SysEx (F0 ...) or a meta event (FF type data). */
struct StyleEvent
{
    Tick at = 0;
    std::vector<std::uint8_t> bytes;
    bool operator== (const StyleEvent&) const = default;
};

/** One section of a style: a short clip for each source channel. */
struct StyleSection
{
    std::string name;
    Tick length = kTicksPerQuarter * 4;
    std::map<int, Clip> parts;      ///< by source channel 1..16; notes start at 0
    bool operator== (const StyleSection&) const = default;
};

/**
    An arranger style in our own model. It reads from and writes to Yamaha
    style files (SFF1 and SFF2/"SFF GE"), but nothing here is tied to that
    layout, so the file code can change without touching editors.
*/
struct Style
{
    std::string name = "New Style";
    int format = 2;                  ///< 1 = SFF1, 2 = SFF2 (PSR-S970)
    double tempo = 120.0;
    TimeSignature timeSignature;
    std::vector<StyleEvent> setup;   ///< the "SInt" bar: voices, volumes, effects, SysEx
    std::vector<StyleSection> sections;
    std::vector<StyleRuleSet> ruleSets;
    std::vector<std::uint8_t> extra; ///< chunks after CASM (One Touch Settings, Music Finder), kept as they are

    const StyleSection* section (const std::string& name) const;
    StyleSection* section (const std::string& name);

    /** The rule for a channel in a section; null when the file has none (the keyboard then uses defaults). */
    const StyleChannelRule* rule (const std::string& section, int sourceChannel) const;

    bool operator== (const Style&) const = default;
};

namespace style_file
{
    inline constexpr const char* kExtension = ".sty";

    struct Error : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    /** Reads a Yamaha style file. Throws Error. */
    Style read (std::span<const std::uint8_t> bytes);
    Style load (const std::filesystem::path&);

    /** Writes a Yamaha style file in the style's format. */
    std::vector<std::uint8_t> write (const Style&);
    void save (const Style&, const std::filesystem::path&);
}

} // namespace arranger

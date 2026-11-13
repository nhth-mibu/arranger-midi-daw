#pragma once

#include "arranger/core/Time.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace arranger
{

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

/** What a song keeps to be an arranger style, next to the notes in its Clip grid. */
struct StyleData
{
    int format = 2;                  ///< 1 = SFF1, 2 = SFF2 (PSR-S970)
    std::vector<StyleEvent> setup;   ///< SysEx and other setup messages; voices and mixer values live on the tracks
    std::vector<StyleRuleSet> ruleSets;
    std::map<std::string, std::vector<StyleEvent>> sectionEvents; ///< by section name: SysEx, tempo changes and text inside a section, kept as they are
    std::vector<std::uint8_t> extra; ///< One Touch Settings and other chunks, kept as they are
    bool operator== (const StyleData&) const = default;
};

} // namespace arranger

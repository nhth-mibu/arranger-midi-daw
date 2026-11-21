#include "arranger/core/PsrStyle.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace arranger::psr_style
{

const std::vector<Section>& sections()
{
    static const std::vector<Section> list {
        { 0x00, "Intro 1" }, { 0x01, "Intro 2" }, { 0x02, "Intro 3" },
        { 0x08, "Main A" }, { 0x09, "Main B" }, { 0x0a, "Main C" }, { 0x0b, "Main D" },
        { 0x10, "Fill In AA" }, { 0x11, "Fill In BB" }, { 0x12, "Fill In CC" }, { 0x13, "Fill In DD" },
        { 0x18, "Break Fill" },
        { 0x20, "Ending 1" }, { 0x21, "Ending 2" }, { 0x22, "Ending 3" },
    };
    return list;
}

MidiBytes section (int code, bool on)
{
    if (code < 0 || code > 0x7f)
        throw std::invalid_argument ("section code is 0-127");
    return { 0xf0, 0x43, 0x7e, 0x00, static_cast<std::uint8_t> (code), static_cast<std::uint8_t> (on ? 0x7f : 0x00), 0xf7 };
}

std::vector<int> tempoBytes (double bpm)
{
    if (! (bpm >= 5.0 && bpm <= 500.0))
        throw std::invalid_argument ("tempo is 5-500 BPM");
    const auto microseconds = static_cast<long> (60000000.0 / bpm);
    return { static_cast<int> ((microseconds >> 21) & 0x7f), static_cast<int> ((microseconds >> 14) & 0x7f),
             static_cast<int> ((microseconds >> 7) & 0x7f), static_cast<int> (microseconds & 0x7f) };
}

MidiBytes tempo (double bpm)
{
    const auto bytes = tempoBytes (bpm);
    return { 0xf0, 0x43, 0x7e, 0x01, static_cast<std::uint8_t> (bytes[0]), static_cast<std::uint8_t> (bytes[1]),
             static_cast<std::uint8_t> (bytes[2]), static_cast<std::uint8_t> (bytes[3]), 0xf7 };
}

const std::vector<ChordType>& chordTypes()
{
    static const std::vector<ChordType> list {
        { 0x00, "Major" }, { 0x01, "Major 6" }, { 0x02, "Major 7" }, { 0x03, "Major 7 #11" }, { 0x04, "Major 9" },
        { 0x05, "Major 7 9" }, { 0x06, "Major 6 9" }, { 0x07, "Augmented" }, { 0x08, "Minor" }, { 0x09, "Minor 6" },
        { 0x0a, "Minor 7" }, { 0x0b, "Minor 7 b5" }, { 0x0c, "Minor 9" }, { 0x0d, "Minor 7 9" }, { 0x0e, "Minor 7 11" },
        { 0x0f, "Minor Major 7" }, { 0x10, "Minor Major 7 9" }, { 0x11, "Diminished" }, { 0x12, "Diminished 7" },
        { 0x13, "Dominant 7" }, { 0x14, "7 sus4" }, { 0x15, "7 b5" }, { 0x16, "7 9" }, { 0x17, "7 #11" },
        { 0x18, "7 13" }, { 0x19, "7 b9" }, { 0x1a, "7 b13" }, { 0x1b, "7 #9" }, { 0x1c, "Major 7 aug" },
        { 0x1d, "7 aug" }, { 0x1e, "1 + 8" }, { 0x1f, "1 + 5" }, { 0x20, "Sus 4" }, { 0x21, "1 + 2 + 5" },
        { 0x22, "cc" },
    };
    return list;
}

namespace
{
    std::uint8_t rootByte (int pc)
    {
        static const int noteByPc[] = { 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6, 7 }; // C C# D D# E F F# G G# A A# B
        static const int accidentalByPc[] = { 3, 4, 3, 4, 3, 3, 4, 3, 4, 3, 4, 3 }; // natural, sharp
        pc = ((pc % 12) + 12) % 12;
        return static_cast<std::uint8_t> ((accidentalByPc[pc] << 4) | noteByPc[pc]);
    }
}

MidiBytes chord (int root, int type, int bassRoot)
{
    if (root < 0 || root > 11)
        throw std::invalid_argument ("root is a pitch class 0-11");
    if (type < 0 || type > 0x22)
        throw std::invalid_argument ("chord type is 0-34");
    if (bassRoot > 11)
        throw std::invalid_argument ("bass root is a pitch class 0-11");
    const auto bass = bassRoot < 0 ? 127 : static_cast<int> (rootByte (bassRoot));
    // bt is the bass chord type. 7F means "no bass chord", so a plain slash
    // chord sends the bass note in bn and no chord type in bt.
    return { 0xf0, 0x43, 0x7e, 0x02, rootByte (root), static_cast<std::uint8_t> (type),
             static_cast<std::uint8_t> (bass), static_cast<std::uint8_t> (bassRoot < 0 ? 127 : 0x7f), 0xf7 };
}

MidiBytes styleNumber (int number)
{
    if (number < 0 || number > 0x3fff)
        throw std::invalid_argument ("style number is 0-16383");
    return { 0xf0, 0x43, 0x73, 0x01, 0x51, 0x05, 0x00, 0x03, 0x04, 0x00, 0x00,
             static_cast<std::uint8_t> ((number >> 7) & 0x7f), static_cast<std::uint8_t> (number & 0x7f), 0xf7 };
}

MidiBytes splitPoint (int note)
{
    if (note < 0 || note > 127)
        throw std::invalid_argument ("split point is a MIDI note");
    return { 0xf0, 0x43, 0x73, 0x01, 0x51, 0x00, 0x00, 0x00, 0x03, 0x10, 0x00, static_cast<std::uint8_t> (note), 0xf7 };
}

} // namespace arranger::psr_style

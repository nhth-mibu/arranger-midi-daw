#pragma once

#include "arranger/core/SynthEditor.h"

#include <string>
#include <vector>

/** Remote control of a Yamaha PSR-S970/S770 style player. */
namespace arranger::psr_style
{

struct Section
{
    int code = 0;          ///< the ss byte
    const char* name = "";
};

/** The sections the S970/S770 can play, in the order of the Data List. */
const std::vector<Section>& sections();

/** Section Control F0 43 7E 00 ss dd F7. dd is 7F for on and 00 for off. */
MidiBytes section (int code, bool on = true);

/** Tempo Control F0 43 7E 01 t4 t3 t2 t1 F7, four 7-bit bytes, MSB first. */
MidiBytes tempo (double bpm);

/** One chord type from the Data List: its byte and the name shown to the user. */
struct ChordType
{
    int code = 0;
    const char* name = "";
};

const std::vector<ChordType>& chordTypes();

/** Chord Control F0 43 7E 02 cr ct bn bt F7. Roots are MIDI pitch classes 0-11. */
MidiBytes chord (int root, int type, int bassRoot = -1);

/** Style No. F0 43 73 01 51 05 00 03 04 00 00 dd dd F7. The numbering is unverified. */
MidiBytes styleNumber (int number);

/** Split Point F0 43 73 01 51 00 00 00 03 10 00 dd F7. */
MidiBytes splitPoint (int note);

/** The four 7-bit bytes of a tempo in microseconds per quarter note. */
std::vector<int> tempoBytes (double bpm);

} // namespace arranger::psr_style

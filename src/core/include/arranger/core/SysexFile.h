#pragma once

#include "arranger/core/SynthEditor.h"

#include <string>
#include <vector>

namespace arranger::sysex_file
{

/** Splits a .syx file into complete F0...F7 messages. Throws std::runtime_error. */
std::vector<MidiBytes> parse (const std::string& bytes);

/** Joins complete SysEx messages into a .syx file. Throws std::runtime_error. */
std::string encode (const std::vector<MidiBytes>& messages);

} // namespace arranger::sysex_file

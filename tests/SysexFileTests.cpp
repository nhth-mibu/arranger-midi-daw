#include "arranger/core/SysexFile.h"

#include <catch2/catch_test_macros.hpp>

using namespace arranger;

TEST_CASE ("SysEx files keep complete messages", "[sysex]")
{
    const MidiBytes a { 0xf0, 0x41, 0x00, 0x42, 0xf7 };
    const MidiBytes b { 0xf0, 0x43, 0x10, 0x11, 0xf7 };
    const auto bytes = sysex_file::encode ({ a, b });
    CHECK (bytes.size() == a.size() + b.size());
    CHECK (sysex_file::parse (bytes) == std::vector<MidiBytes> { a, b });
    CHECK_THROWS (sysex_file::parse (""));
    CHECK_THROWS (sysex_file::parse (std::string ("\xf0\x41")));
    CHECK_THROWS (sysex_file::parse (std::string ("\xf7")));
    CHECK_THROWS (sysex_file::encode ({}));
    CHECK_THROWS (sysex_file::encode ({ MidiBytes { 0x41, 0xf7 } }));
}

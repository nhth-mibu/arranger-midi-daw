#include "arranger/core/ControllerProfile.h"
#include "arranger/core/DeviceProfile.h"
#include "arranger/core/MidiFile.h"
#include "arranger/core/MusicXml.h"
#include "arranger/core/PsrExport.h"
#include "arranger/core/RecorderImport.h"
#include "arranger/core/SongEdit.h"
#include "arranger/core/Style.h"
#include "arranger/core/SynthEditor.h"
#include "arranger/core/SysexFile.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace arranger;

namespace
{
    DeviceProfile loadDeviceProfile (const char* name)
    {
        std::ifstream in (std::filesystem::path (ARRANGER_DEVICES_DIR) / name, std::ios::binary);
        std::ostringstream s;
        s << in.rdbuf();
        return device_profile::parse (s.str());
    }

    std::vector<std::uint8_t> makeChunk (const char (&tag)[5], const std::vector<std::uint8_t>& body)
    {
        std::vector<std::uint8_t> out {
            static_cast<std::uint8_t> (tag[0]),
            static_cast<std::uint8_t> (tag[1]),
            static_cast<std::uint8_t> (tag[2]),
            static_cast<std::uint8_t> (tag[3]),
            static_cast<std::uint8_t> ((body.size() >> 24) & 0xff),
            static_cast<std::uint8_t> ((body.size() >> 16) & 0xff),
            static_cast<std::uint8_t> ((body.size() >> 8) & 0xff),
            static_cast<std::uint8_t> (body.size() & 0xff),
        };
        out.insert (out.end(), body.begin(), body.end());
        return out;
    }
}

TEST_CASE ("H15: midi_file::write respects options.lyrics and options.offset without duplicating lyrics", "[audit][midi]")
{
    Song s;
    auto& track = edit::addTrack (s, "Vocal");
    track.output.channel = 1;
    Clip clip;
    clip.start = 0;
    clip.length = kTicksPerQuarter * 4;
    clip.notes = { { 0, kTicksPerQuarter, 60, 100 }, { kTicksPerQuarter, kTicksPerQuarter, 62, 100 } };
    edit::addClip (s, track.id, clip);
    s.tracks[0].marks = {
        { ScoreMark::Kind::lyric, 0, "Hel-" },
        { ScoreMark::Kind::lyric, kTicksPerQuarter, "lo" },
    };

    SECTION ("options.lyrics = false omits lyrics completely")
    {
        midi_file::WriteOptions opts;
        opts.lyrics = false;
        const auto back = midi_file::read (midi_file::write (s, opts));
        REQUIRE (back.tracks.size() == 1);
        CHECK (back.tracks[0].marks.empty());
    }

    SECTION ("options.lyrics = true with options.offset emits each lyric once at shifted position")
    {
        midi_file::WriteOptions opts;
        opts.lyrics = true;
        opts.offset = kTicksPerQuarter * 4;
        const auto back = midi_file::read (midi_file::write (s, opts));
        REQUIRE (back.tracks.size() == 1);
        const auto& marks = back.tracks[0].marks;
        REQUIRE (marks.size() == 2);
        CHECK (marks[0].text == "Hel-");
        CHECK (marks[0].at == kTicksPerQuarter * 4);
        CHECK (marks[1].text == "lo");
        CHECK (marks[1].at == kTicksPerQuarter * 5);
    }

    SECTION ("PSR export emits each lyric once after the 1-bar setup offset")
    {
        DeviceLibrary lib;
        lib.add (loadDeviceProfile ("yamaha-psr-s970.yaml"));
        const auto plan = psr_export::plan (s, lib);
        const auto bytes = psr_export::write (s, plan);
        const auto back = midi_file::read (bytes);
        REQUIRE (back.tracks.size() == 1);
        const auto& marks = back.tracks[0].marks;
        REQUIRE (marks.size() == 2);
        CHECK (marks[0].at == kTicksPerQuarter * 4);
        CHECK (marks[1].at == kTicksPerQuarter * 5);
    }
}

TEST_CASE ("H16: StyleFile handles short Ctb2 chunks and invalid timeSignature / large setupEnd in O(1)", "[audit][style]")
{
    SECTION ("Short Ctb2 chunk (26..39 bytes) does not throw and is preserved in extra")
    {
        Style base;
        StyleSection mainA;
        mainA.name = "Main A";
        mainA.length = kTicksPerQuarter * 4;
        mainA.parts[10].length = mainA.length;
        mainA.parts[10].notes.push_back ({ 0, kTicksPerQuarter, 36, 100 });
        base.sections.push_back (mainA);
        auto bytes = style_file::write (base);

        const std::vector<std::uint8_t> sdecText { 'M', 'a', 'i', 'n', ' ', 'A' };
        const auto sdec = makeChunk ("Sdec", sdecText);
        // 30 bytes: >= 26 (Ctab size) but < 40 (Ctb2 size).
        const std::vector<std::uint8_t> shortCtb2Bytes (30, 0);
        const auto shortCtb2 = makeChunk ("Ctb2", shortCtb2Bytes);
        std::vector<std::uint8_t> csegBody = sdec;
        csegBody.insert (csegBody.end(), shortCtb2.begin(), shortCtb2.end());
        const auto casm = makeChunk ("CASM", makeChunk ("CSEG", csegBody));
        bytes.insert (bytes.end(), casm.begin(), casm.end());

        const auto loaded = style_file::read (bytes);
        REQUIRE (loaded.ruleSets.size() == 1);
        CHECK (loaded.ruleSets[0].rules.empty());
        CHECK (loaded.ruleSets[0].extra == shortCtb2);
    }

    SECTION ("Invalid timeSignature and large setup event tick do not divide by zero or hang")
    {
        Style style;
        style.timeSignature = { 0, 0 };
        style.setup.push_back ({ kTicksPerQuarter * 4 * 500'000, { 0xb0, 7, 100 } });
        StyleSection sec;
        sec.name = "Main A";
        sec.length = kTicksPerQuarter * 4;
        sec.parts[9].length = sec.length;
        sec.parts[9].notes.push_back ({ 0, 240, 36, 100 });
        style.sections.push_back (sec);

        const auto t0 = std::chrono::steady_clock::now();
        const auto bytes = style_file::write (style);
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now() - t0).count();
        CHECK (! bytes.empty());
        CHECK (elapsedMs < 500);

        style.timeSignature = { -4, 4 };
        CHECK (! style_file::write (style).empty());
    }
}

TEST_CASE ("H17: MusicXML openTies keyed by (staff, voice, pitch) with adjacency check", "[audit][musicxml]")
{
    SECTION ("Cross-voice and cross-staff <backup> with <tie type=\"stop\"/> does not create negative lengths or break ties")
    {
        constexpr const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
  <part id="P1">
    <measure number="1">
      <attributes>
        <divisions>1</divisions>
        <time><beats>4</beats><beat-type>4</beat-type></time>
      </attributes>
      <forward><duration>2</duration></forward>
      <note>
        <pitch><step>C</step><octave>4</octave></pitch>
        <duration>2</duration>
        <tie type="start"/>
        <voice>1</voice>
        <staff>1</staff>
      </note>
      <backup><duration>4</duration></backup>
      <note>
        <pitch><step>C</step><octave>4</octave></pitch>
        <duration>1</duration>
        <tie type="stop"/>
        <voice>2</voice>
        <staff>2</staff>
      </note>
    </measure>
    <measure number="2">
      <note>
        <pitch><step>C</step><octave>4</octave></pitch>
        <duration>2</duration>
        <tie type="stop"/>
        <voice>1</voice>
        <staff>1</staff>
      </note>
    </measure>
  </part>
</score-partwise>)";

        const auto song = musicxml::read (xml);
        REQUIRE (song.tracks.size() == 1);
        REQUIRE (song.tracks[0].clips.size() == 1);
        const auto& notes = song.tracks[0].clips[0]->notes;
        REQUIRE (notes.size() == 2);
        // Voice 2 note at tick 0 (duration 1 beat = 960) and Voice 1 tied note at tick 1920 (duration 4 beats = 3840).
        CHECK (notes[0].start == 0);
        CHECK (notes[0].length == kTicksPerQuarter);
        CHECK (notes[1].start == kTicksPerQuarter * 2);
        CHECK (notes[1].length == kTicksPerQuarter * 4);
    }

    SECTION ("Same-voice <backup> to an earlier tick cannot merge backwards into a later open tie")
    {
        constexpr const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <part-list><score-part id="P1"><part-name>Lead</part-name></score-part></part-list>
  <part id="P1">
    <measure number="1">
      <attributes>
        <divisions>1</divisions>
        <time><beats>4</beats><beat-type>4</beat-type></time>
      </attributes>
      <forward><duration>2</duration></forward>
      <note>
        <pitch><step>E</step><octave>4</octave></pitch>
        <duration>2</duration>
        <tie type="start"/>
      </note>
      <backup><duration>4</duration></backup>
      <note>
        <pitch><step>E</step><octave>4</octave></pitch>
        <duration>1</duration>
        <tie type="stop"/>
      </note>
    </measure>
    <measure number="2">
      <note>
        <pitch><step>E</step><octave>4</octave></pitch>
        <duration>1</duration>
        <tie type="stop"/>
      </note>
    </measure>
  </part>
</score-partwise>)";

        const auto song = musicxml::read (xml);
        REQUIRE (song.tracks.size() == 1);
        REQUIRE (song.tracks[0].clips.size() == 1);
        const auto& notes = song.tracks[0].clips[0]->notes;
        REQUIRE (notes.size() == 2);
        CHECK (notes[0].start == 0);
        CHECK (notes[0].length == kTicksPerQuarter);
        CHECK (notes[1].start == kTicksPerQuarter * 2);
        CHECK (notes[1].length == kTicksPerQuarter * 3);
    }
}

TEST_CASE ("H18: SysexFile rejects any interior byte >= 0x80 in both encode and parse", "[audit][sysex]")
{
    for (int bad : { 0x80, 0x90, 0xc0, 0xf0, 0xf1, 0xf6, 0xf7, 0xf8, 0xfe, 0xff })
    {
        INFO ("bad byte: " << bad);
        const MidiBytes msg { 0xf0, 0x41, static_cast<std::uint8_t> (bad), 0x00, 0xf7 };
        CHECK_THROWS (sysex_file::encode ({ msg }));

        if (bad != 0xf7)
        {
            const std::string raw (reinterpret_cast<const char*> (msg.data()), msg.size());
            CHECK_THROWS (sysex_file::parse (raw));
        }
    }
}

TEST_CASE ("H19: RecorderImport rejects TRACK09 and does not collide with MASTER", "[audit][recorder]")
{
    CHECK (recorder_import::zoomL6Project ({ "TRACK00_01.wav", "TRACK09_01.wav", "TRACK10_01.wav" }).empty());

    const auto tracks = recorder_import::zoomL6Project ({ "MASTER_01.wav", "TRACK09_01.wav", "TRACK09_02.wav" });
    REQUIRE (tracks.size() == 1);
    CHECK (tracks[0].name == "Master");
    CHECK (tracks[0].channel == 0);
    CHECK (tracks[0].take == 1);
    CHECK (tracks[0].files == std::vector<std::string> { "MASTER_01.wav" });
}

TEST_CASE ("H20: DeviceProfile, SynthEditor, and ControllerProfile fixes", "[audit][devices][synth-editor][controllers]")
{
    SECTION ("DeviceLibrary::freeChannelForPort treats capabilities.parts <= 0 as 1 part when receiveChannel > 0")
    {
        const auto profile = device_profile::parse (
            "profile: 1\n"
            "id: mono-synth\n"
            "name: Mono Synth\n"
            "ports: [\"MonoSynth\"]\n"
            "receiveChannel: 3\n");
        CHECK (profile.capabilities.parts == 0);

        DeviceLibrary lib;
        lib.add (profile);
        CHECK (lib.freeChannelForPort ("MonoSynth", {}, false) == 3);
        CHECK (lib.freeChannelForPort ("MonoSynth", { 3 }, false) == 0);
        CHECK (lib.freeChannelForPort ("MonoSynth", {}, true) == 0);
    }

    SECTION ("parseCapabilitiesJson wraps non-string source in device_profile::Error and rejects 64-bit integer truncation")
    {
        CHECK_THROWS_AS (device_profile::parseCapabilitiesJson (R"({"source": 123})"), device_profile::Error);
        CHECK_THROWS_AS (device_profile::parseCapabilitiesJson (R"({"source": ["bad"]})"), device_profile::Error);
        CHECK_THROWS_AS (device_profile::parseCapabilitiesJson (R"({"source": true})"), device_profile::Error);

        // 4294967312 == (1LL << 32) + 16, which would truncate to 16 if narrowed to int32_t before range check.
        CHECK_THROWS_AS (device_profile::parseCapabilitiesJson (R"({"polyphony": 4294967312})"), device_profile::Error);
        CHECK_THROWS_AS (device_profile::parseCapabilitiesJson (R"({"parts": 4294967297})"), device_profile::Error);
        CHECK_THROWS_AS (device_profile::parseCapabilitiesJson (R"({"lowest": -4294967295})"), device_profile::Error);
        CHECK_THROWS_AS (device_profile::parseCapabilitiesJson (R"({"highest": 18446744073709551615})"), device_profile::Error);

        // Controllers list also ignores out-of-range 64-bit ints instead of truncating them into 0..127.
        const auto caps = device_profile::parseCapabilitiesJson (R"({"controllers": [1, 4294967303, 64], "source": null})");
        CHECK (caps.controllers == std::vector<int> { 1, 64 });
        CHECK (caps.source == "user");
    }

    SECTION ("writeAddOn and parseAddOn round-trip empty groups, empty items, and program == -1")
    {
        SoundAddOn emptyGroups;
        emptyGroups.profileId = "test-synth";
        const auto emptyBack = device_profile::parseAddOn (device_profile::writeAddOn (emptyGroups));
        CHECK (emptyBack.profileId == "test-synth");
        CHECK (emptyBack.groups.empty());

        SoundAddOn withEmptyAndNoProgram;
        withEmptyAndNoProgram.profileId = "test-synth";
        withEmptyAndNoProgram.groups.push_back ({ "Empty Group", {} });
        withEmptyAndNoProgram.groups.push_back ({ "Bank", { { "Bank Only", 87, 64, -1 }, { "Patch 1", 87, 64, 0 } } });
        const auto text = device_profile::writeAddOn (withEmptyAndNoProgram);
        const auto back = device_profile::parseAddOn (text);
        REQUIRE (back.groups.size() == 2);
        CHECK (back.groups[0].name == "Empty Group");
        CHECK (back.groups[0].sounds.empty());
        REQUIRE (back.groups[1].sounds.size() == 2);
        CHECK (back.groups[1].sounds[0].name == "Bank Only");
        CHECK (back.groups[1].sounds[0].bankMsb == 87);
        CHECK (back.groups[1].sounds[0].bankLsb == 64);
        CHECK (back.groups[1].sounds[0].program == -1);
        CHECK (back.groups[1].sounds[1].program == 0);
    }

    SECTION ("SynthEditor supports 14-bit NRPN and RPN messages and Listener parsing while keeping 7-bit NRPN behavior")
    {
        constexpr const char* yaml = R"(
profile: 1
id: nrpn-14
name: NRPN 14
editor:
  pages:
    - name: Main
      params:
        - { name: Cutoff14, nrpn: [1, 20], max: 16383 }
        - { name: BendRange14, rpn: [0, 0], max: 3072 }
        - { name: Mode7, nrpn: [0, 62], max: 2 }
)";
        const auto p = device_profile::parse (yaml);
        const auto* cutoff14 = p.editor.find ("Main/Cutoff14");
        const auto* bend14 = p.editor.find ("Main/BendRange14");
        const auto* mode7 = p.editor.find ("Main/Mode7");
        REQUIRE (cutoff14 != nullptr);
        REQUIRE (bend14 != nullptr);
        REQUIRE (mode7 != nullptr);

        // 7-bit NRPN continues to send 3 messages (CC 99, 98, 6).
        const auto msgs7 = synth_edit::messages (p.editor, *mode7, 2, 1);
        CHECK (msgs7 == std::vector<MidiBytes> { { 0xb0, 99, 0 }, { 0xb0, 98, 62 }, { 0xb0, 6, 2 } });

        // 14-bit NRPN sends 4 messages (CC 99, 98, 6 MSB, 38 LSB).
        const int val14 = 0x1234; // MSB = 0x24, LSB = 0x34
        const auto msgs14 = synth_edit::messages (p.editor, *cutoff14, val14, 2);
        CHECK (msgs14 == std::vector<MidiBytes> {
            { 0xb1, 99, 1 },
            { 0xb1, 98, 20 },
            { 0xb1, 6, 0x24 },
            { 0xb1, 38, 0x34 },
        });

        // 14-bit RPN sends 4 messages (CC 101, 100, 6 MSB, 38 LSB).
        const auto rpn14 = synth_edit::messages (p.editor, *bend14, 1536, 1);
        CHECK (rpn14 == std::vector<MidiBytes> {
            { 0xb0, 101, 0 },
            { 0xb0, 100, 0 },
            { 0xb0, 6, 12 },
            { 0xb0, 38, 0 },
        });

        // Listener round-trips 14-bit NRPN, 14-bit RPN, and 7-bit NRPN cleanly.
        synth_edit::Listener listen2 (p.editor, 2);
        CHECK (listen2.take (msgs14[0]).empty());
        CHECK (listen2.take (msgs14[1]).empty());
        CHECK (listen2.take (msgs14[2]).empty());
        auto changes = listen2.take (msgs14[3]);
        REQUIRE (changes.size() == 1);
        CHECK (changes[0].param == 0);
        CHECK (changes[0].value == val14);

        synth_edit::Listener listen1 (p.editor, 1);
        for (std::size_t i = 0; i < 3; ++i)
            CHECK (listen1.take (rpn14[i]).empty());
        changes = listen1.take (rpn14[3]);
        REQUIRE (changes.size() == 1);
        CHECK (changes[0].param == 1);
        CHECK (changes[0].value == 1536);

        CHECK (listen1.take (msgs7[0]).empty());
        CHECK (listen1.take (msgs7[1]).empty());
        changes = listen1.take (msgs7[2]);
        REQUIRE (changes.size() == 1);
        CHECK (changes[0].param == 2);
        CHECK (changes[0].value == 2);
    }

    SECTION ("NameReader::next and multiplyAddress do not truncate 1-byte stride before adding to multi-byte first address")
    {
        CHECK (synth_edit::multiplyAddress ({ 0x40 }, 3) == MidiBytes { 0x01, 0x40 });

        constexpr const char* yaml = R"(
profile: 1
id: stride-test
name: Stride Test
editor:
  sysex:
    write: "F0 7D {dev} 12 {addr} {value} F7"
    read: "F0 7D {dev} 11 {addr} {size} F7"
    device: 0
    addressBytes: 3
    sizeBytes: 1
    checksum: none
  pages:
    - name: Main
      params:
        - { name: Cutoff, cc: 74 }
  names:
    first: "01 00 00"
    stride: "40"
    count: 4
    length: 8
)";
        const auto p = device_profile::parse (yaml);
        synth_edit::NameReader reader (p.editor, 1);
        const auto s0 = reader.next();
        const auto s1 = reader.next();
        const auto s2 = reader.next();
        const auto s3 = reader.next();
        REQUIRE (s0);
        REQUIRE (s1);
        REQUIRE (s2);
        REQUIRE (s3);
        CHECK (s0->bytes == MidiBytes { 0xf0, 0x7d, 0x00, 0x11, 0x01, 0x00, 0x00, 0x08, 0xf7 });
        CHECK (s1->bytes == MidiBytes { 0xf0, 0x7d, 0x00, 0x11, 0x01, 0x00, 0x40, 0x08, 0xf7 });
        CHECK (s2->bytes == MidiBytes { 0xf0, 0x7d, 0x00, 0x11, 0x01, 0x01, 0x00, 0x08, 0xf7 });
        CHECK (s3->bytes == MidiBytes { 0xf0, 0x7d, 0x00, 0x11, 0x01, 0x01, 0x40, 0x08, 0xf7 });
    }

    SECTION ("ControllerProfile prevents signed 32-bit overflow in number + count and firstStrip + count")
    {
        CHECK_THROWS_AS (controller_profile::parse (
            "controller: 1\n"
            "id: overflow-count\n"
            "name: Overflow Count\n"
            "ports: [\"Test\"]\n"
            "strips: 8\n"
            "controls:\n"
            "  - { note: 60, count: 2147483647, action: launchClip }\n"),
            device_profile::Error);

        CHECK_THROWS_AS (controller_profile::parse (
            "controller: 1\n"
            "id: overflow-strip\n"
            "name: Overflow Strip\n"
            "ports: [\"Test\"]\n"
            "strips: 8\n"
            "controls:\n"
            "  - { note: 60, count: 2, strip: 2147483647, action: launchClip }\n"),
            device_profile::Error);
    }
}

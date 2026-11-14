#include "arranger/core/MidiFile.h"
#include "arranger/core/PsrExport.h"
#include "arranger/core/SongEdit.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace arranger;

namespace
{
    DeviceProfile load (const char* name)
    {
        std::ifstream in (std::filesystem::path (ARRANGER_DEVICES_DIR) / name, std::ios::binary);
        std::ostringstream s;
        s << in.rdbuf();
        return device_profile::parse (s.str());
    }

    Track& withNotes (Song& song, const std::string& name)
    {
        auto& t = edit::addTrack (song, name);
        Clip c;
        c.notes.push_back ({ 0, 480, 60, 100 });
        edit::addClip (song, t.id, c);
        return song.tracks.back();
    }

    bool contains (const std::vector<std::uint8_t>& bytes, std::initializer_list<std::uint8_t> part)
    {
        return std::search (bytes.begin(), bytes.end(), part.begin(), part.end()) != bytes.end();
    }
}

TEST_CASE ("PSR-S970 export: voices, channels and the file", "[psr]")
{
    DeviceLibrary library;
    CHECK_THROWS_AS (psr_export::plan (Song {}, library), psr_export::Error);
    for (const char* f : { "yamaha-psr-s970.yaml", "roland-d-05.yaml", "novation-xiosynth.yaml" })
        library.add (load (f));

    Song song;
    song.title = "Loop";
    auto& pad = withNotes (song, "Pad");
    pad.output = { TrackOutput::Kind::midiPort, "D-05", 1 };
    pad.sound = { "Pads > Fat Warm Pad", 87, 9, 4 };

    auto& bass = withNotes (song, "Bass");
    bass.output = { TrackOutput::Kind::midiPort, "XioSynth 49", 1 }; // the same channel as the pad
    bass.sound = { "Bank A > Analog Bass", -1, -1, 11 };

    auto& piano = withNotes (song, "Keys");
    piano.output = { TrackOutput::Kind::soundFont, "", 2 };
    piano.sound = { "Piano > Bright Acoustic Piano", 0, 0, 1 };

    auto& drums = withNotes (song, "Drums");
    drums.output = { TrackOutput::Kind::sampler, "", 3 };

    auto& yamaha = withNotes (song, "Organ");
    yamaha.output = { TrackOutput::Kind::midiPort, "Digital Workstation", 4 };
    yamaha.sound = { "Organ > Jazz", 0, 117, 17 };

    auto& vocals = edit::addTrack (song, "Vocals");
    vocals.output.kind = TrackOutput::Kind::audio;
    auto& empty = edit::addTrack (song, "Empty");
    (void) empty;

    const auto plan = psr_export::plan (song, library);
    REQUIRE (plan.parts.size() == 5);
    CHECK (plan.skipped.size() == 2);

    const auto& padPart = plan.parts[0];
    CHECK (padPart.how == "from the name");
    CHECK (padPart.voice.program == 89);
    CHECK (padPart.channel == 1);

    const auto& bassPart = plan.parts[1];
    CHECK (bassPart.how == "same name");
    CHECK (bassPart.voice.name.find ("AnalogBass") != std::string::npos);
    CHECK (bassPart.channel != 1); // moved: the pad has channel 1
    CHECK (bassPart.channel != 10);

    CHECK (plan.parts[2].how == "same GM sound");
    CHECK (plan.parts[2].voice.program == 1);
    CHECK (plan.parts[3].how == "drum kit");
    CHECK (plan.parts[3].channel == 10);
    CHECK (plan.parts[3].voice.bankMsb == 127);
    CHECK (plan.parts[4].how == "kept");
    CHECK (plan.parts[4].voice == yamaha.sound);

    const auto bytes = psr_export::write (song, plan);
    REQUIRE (bytes.size() > 22);
    CHECK (bytes[9] == 0); // format 0
    CHECK (bytes[11] == 1); // one track
    CHECK (contains (bytes, { 0xf0, 0x08, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0xf7 }));

    // The music moves one bar later; the file reads back.
    const auto back = midi_file::read (bytes);
    Tick first = -1;
    for (const auto& t : back.tracks)
        for (const auto& c : t.clips)
            for (const auto& n : c->notes)
                if (first < 0 || c->start + n.start < first)
                    first = c->start + n.start;
    CHECK (first == kTicksPerQuarter * 4);

    CHECK (psr_export::gmProgramFromName ("Analog Bass") == 38);
    CHECK (psr_export::gmProgramFromName ("Fat Warm Pad") == 89);
    CHECK (psr_export::gmProgramFromName ("Choir Pad") == 91);
    CHECK (psr_export::gmProgramFromName ("Nylon Guitar") == 24);
    CHECK (psr_export::gmProgramFromName ("A001") == -1);
}

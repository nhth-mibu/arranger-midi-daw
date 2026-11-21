#include "arranger/core/Arranging.h"
#include "arranger/core/Document.h"
#include "arranger/core/HardwareFit.h"
#include "arranger/core/McpServer.h"
#include "arranger/core/SongEdit.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace arranger;
using namespace arranger::arranging;

namespace
{
    constexpr Tick kBeat = kTicksPerQuarter;
    constexpr Tick kBar = kBeat * 4;

    /** A track with one clip over `bars` bars; `fill` adds notes to it (clip times). */
    TrackId addTrack (Song& s, const std::string& name, int channel, int bars, const std::function<void (std::vector<Note>&)>& fill)
    {
        auto& t = edit::addTrack (s, name);
        t.output = { TrackOutput::Kind::soundFont, {}, channel };
        const auto id = t.id;
        Clip c;
        c.start = 0;
        c.length = kBar * bars;
        fill (c.notes);
        edit::normalise (c);
        edit::addClip (s, id, c);
        return id;
    }

    void chordEvery (std::vector<Note>& notes, int bars, Tick step, std::initializer_list<int> pitches, Tick from = 0)
    {
        for (Tick t = from; t < kBar * bars; t += step)
            for (int p : pitches)
                notes.push_back ({ t, step, static_cast<std::uint8_t> (p), 90 });
    }

    /** Drums, bass, a piano and a melody: a little band. */
    Song band (int bars = 16)
    {
        Song s;
        s.tracks.clear();
        addTrack (s, "Drums", 10, bars, [&] (auto& n) {
            for (Tick t = 0; t < kBar * bars; t += kBeat / 2)
                n.push_back ({ t, 60, 42, 80 });
            for (Tick t = 0; t < kBar * bars; t += kBar)
                n.push_back ({ t, 60, 36, 100 });
        });
        addTrack (s, "Bass", 2, bars, [&] (auto& n) { chordEvery (n, bars, kBeat, { 36 }); });
        addTrack (s, "Piano", 3, bars, [&] (auto& n) { chordEvery (n, bars, kBeat / 2, { 60, 64, 67 }); });
        addTrack (s, "Melody", 4, bars, [&] (auto& n) {
            const int tune[] = { 64, 67, 69, 67 };
            for (Tick t = 0, i = 0; t < kBar * bars; t += kBeat, ++i)
                n.push_back ({ t, kBeat, static_cast<std::uint8_t> (tune[i % 4]), 100 });
        });
        return s;
    }

    TrackId idOf (const Song& s, const std::string& name)
    {
        for (const auto& t : s.tracks)
            if (t.name == name)
                return t.id;
        return 0;
    }

    std::string readFile (const std::filesystem::path& p)
    {
        std::ifstream in (p, std::ios::binary);
        std::ostringstream s;
        s << in.rdbuf();
        return s.str();
    }
}

TEST_CASE ("Track roles are guessed from names and notes", "[arranging]")
{
    const auto s = band();
    const auto roles = guessRoles (s);
    CHECK (roles.at (idOf (s, "Drums")) == Role::drums);
    CHECK (roles.at (idOf (s, "Bass")) == Role::bass);
    CHECK (roles.at (idOf (s, "Piano")) == Role::chords);
    CHECK (roles.at (idOf (s, "Melody")) == Role::melody);
}

TEST_CASE ("Played notes follow looping clips", "[arranging]")
{
    Song s;
    s.tracks.clear();
    auto& t = edit::addTrack (s, "Loop");
    Clip c;
    c.length = kBar * 4;
    c.loopLength = kBar;
    c.notes = { { 0, kBeat, 60, 100 } };
    edit::addClip (s, t.id, c);
    CHECK (playedNotes (s.tracks.front()).size() == 4);
    CHECK (playedNotes (s.tracks.front(), kBar, kBar * 2).size() == 1);
}

TEST_CASE ("The doctor finds a melody that competes with a part", "[arranging]")
{
    auto s = band();
    const auto melody = idOf (s, "Melody");
    const auto piano = idOf (s, "Piano");
    const auto report = diagnose (s, { Goal::clearMelody, 0, 0, 0, nullptr, nullptr, 6 });
    CHECK (report.melody == melody);
    REQUIRE_FALSE (report.proposals.empty());
    const auto& p = report.proposals.front();
    CHECK (p.kind == "overlap");
    CHECK (p.tracks == std::vector<TrackId> { piano });
    REQUIRE (p.options.size() == 2);

    // The melody never changes; the piano moves an octave down.
    const auto after = withOption (s, p.options[0]);
    CHECK (*edit::findTrack (after, melody) == *edit::findTrack (s, melody));
    for (const auto& n : playedNotes (*edit::findTrack (after, piano)))
        CHECK (n.pitch < 64);

    // Taking it is checked against the song as it is now.
    apply (s, p, 1);
    CHECK_THROWS (apply (s, p, 0)); // the piano changed since the proposal
}

TEST_CASE ("The doctor thins busy bars", "[arranging]")
{
    Song s = band (8);
    // Bars 5-6: the piano plays sixteenths with more notes.
    const auto busy = addTrack (s, "Strings", 5, 8, [&] (auto& n) { chordEvery (n, 6, kBeat / 4, { 55, 59, 62, 65 }, kBar * 4); });
    const auto report = diagnose (s, { Goal::lessClutter, 0, 0, 0, nullptr, nullptr, 6 });
    REQUIRE_FALSE (report.proposals.empty());
    const auto& p = report.proposals.front();
    CHECK (p.kind == "density");
    CHECK (p.tracks.front() == busy);
    CHECK (p.from == kBar * 4);
    REQUIRE_FALSE (p.options.empty());
    CHECK (p.options.front().notesChanged > 0);
    CHECK (problemWith (s, p.options.front()).empty());
}

TEST_CASE ("The doctor varies parts that repeat", "[arranging]")
{
    const auto s = band (16);
    const auto report = diagnose (s, { Goal::moreVariation, 0, 0, 0, nullptr, nullptr, 6 });
    REQUIRE_FALSE (report.findings.empty());
    REQUIRE_FALSE (report.proposals.empty());
    for (const auto& p : report.proposals)
    {
        CHECK (p.kind == "repetition");
        CHECK (std::find (p.tracks.begin(), p.tracks.end(), report.melody) == p.tracks.end());
        CHECK_FALSE (p.options.empty());
    }
}

TEST_CASE ("The doctor lifts a section", "[arranging]")
{
    const auto s = band (16);
    CHECK_THROWS (diagnose (s, { Goal::standOut, 0, 0, 0, nullptr, nullptr, 6 }));
    const auto report = diagnose (s, { Goal::standOut, kBar * 8, kBar * 16, 0, nullptr, nullptr, 6 });
    REQUIRE_FALSE (report.findings.empty());
    CHECK (report.findings.front().kind == "contrast");
    REQUIRE (report.proposals.size() >= 2);
    bool lighterBefore = false, harder = false;
    for (const auto& p : report.proposals)
    {
        lighterBefore |= p.kind == "contrast" && p.to == kBar * 8;
        harder |= p.kind == "energy";
        for (auto id : p.tracks)
            CHECK (id != report.melody);
    }
    CHECK (lighterBefore);
    CHECK (harder);
}

TEST_CASE ("Preferences put the kinds the user takes first", "[arranging]")
{
    Proposal a, b;
    a.kind = "density";
    a.options.resize (2);
    b.kind = "overlap";
    b.options.resize (2);
    b.options[1].label = "second";
    Preferences prefs;
    prefs.dismissed (a);
    prefs.took (b, 1);
    std::vector<Proposal> list { a, b };
    rank (list, &prefs);
    CHECK (list.front().kind == "overlap");
    CHECK (list.front().options.front().label == "second");

    const auto again = Preferences::fromJson (prefs.toJson());
    CHECK (again.weight ("overlap") > 1.0);
    CHECK (again.weight ("density") < 1.0);
}

// ---- hardware ----------------------------------------------------------------------
namespace
{
    DeviceLibrary library()
    {
        DeviceLibrary lib;
        for (const auto* f : { "roland-d-05.yaml", "yamaha-psr-s970.yaml", "novation-xiosynth.yaml" })
            lib.add (device_profile::parse (readFile (std::filesystem::path (ARRANGER_DEVICES_DIR) / f)));
        return lib;
    }

    void onPort (Song& s, TrackId id, const std::string& port, int channel)
    {
        edit::findTrack (s, id)->output = { TrackOutput::Kind::midiPort, port, channel };
    }
}

TEST_CASE ("Device profiles carry capabilities", "[hardware]")
{
    const auto lib = library();
    CHECK (lib.byId ("roland-d-05")->capabilities.polyphony == 8);
    CHECK (lib.byId ("roland-d-05")->capabilities.parts == 2);
    CHECK (lib.byId ("yamaha-psr-s970")->capabilities.polyphony == 128);
    CHECK (lib.generalMidi().capabilities.polyphony == 24);

    DeviceCapabilities mine { 12, 1, 36, 96, { 1, 7, 64 }, "user" };
    const auto back = device_profile::parseCapabilitiesJson (device_profile::capabilitiesJson (mine));
    CHECK (back == mine);
    CHECK_THROWS (device_profile::parseCapabilitiesJson ("{\"lowest\": 90, \"highest\": 10}"));
    CHECK_THROWS (device_profile::parse ("profile: 1\nid: x\nname: X\ncapabilities: { polyphony: -3 }\n"));
}

TEST_CASE ("The sustain pedal holds voices", "[hardware]")
{
    Song s;
    s.tracks.clear();
    auto& t = edit::addTrack (s, "Piano");
    Clip c;
    c.length = kBar * 2;
    for (int i = 0; i < 8; ++i)
        c.notes.push_back ({ i * kBeat / 2, kBeat / 4, static_cast<std::uint8_t> (60 + i), 90 });
    edit::addClip (s, t.id, c);
    CHECK (hardware::peakVoices ({ &s.tracks.front() }).first == 1);

    edit::modifyClip (s, s.tracks.front().clips.front()->id, [] (Clip& clip) {
        clip.controls = { { 0, ControlEvent::Kind::controller, 64, 127 }, { kBar, ControlEvent::Kind::controller, 64, 0 } };
    });
    CHECK (hardware::peakVoices ({ &s.tracks.front() }).first == 8);
}

TEST_CASE ("Too many voices on a synth get repairs", "[hardware]")
{
    auto lib = library();
    Song s = band (4);
    // Piano: six-note chords on the D-05 with the bass. The D-05 plays 8 voices.
    const auto piano = idOf (s, "Piano");
    const auto bass = idOf (s, "Bass");
    edit::modifyClip (s, edit::findTrack (s, piano)->clips.front()->id, [] (Clip& c) {
        c.notes.clear();
        chordEvery (c.notes, 4, kBar, { 48, 55, 60, 64, 67, 72, 76, 79 });
    });
    onPort (s, piano, "D-05", 1);
    onPort (s, bass, "D-05", 1);

    const auto report = hardware::check (s, lib);
    const auto d05 = std::find_if (report.devices.begin(), report.devices.end(), [] (auto& d) { return d.profileId == "roland-d-05"; });
    REQUIRE (d05 != report.devices.end());
    CHECK (d05->peakVoices == 9);
    CHECK_FALSE (d05->overloaded.empty());
    CHECK_FALSE (d05->problems.empty());

    const auto poly = std::find_if (report.proposals.begin(), report.proposals.end(), [] (auto& p) { return p.kind == "polyphony"; });
    REQUIRE (poly != report.proposals.end());
    CHECK (poly->tracks == std::vector<TrackId> { piano }); // the bass is protected
    REQUIRE (poly->options.size() == 2);

    // A: doubled notes go (C, E, G are doubled), the most at once fits.
    {
        const auto after = withOption (s, poly->options[0]);
        CHECK (hardware::peakVoices ({ edit::findTrack (after, piano), edit::findTrack (after, bass) }).first <= 8);
        CHECK (*edit::findTrack (after, bass) == *edit::findTrack (s, bass));
    }
    // B: the piano plays on the built-in SoundFont.
    {
        const auto after = withOption (s, poly->options[1]);
        CHECK (edit::findTrack (after, piano)->output.kind == TrackOutput::Kind::soundFont);
        auto again = hardware::check (after, lib);
        for (const auto& p : again.proposals)
            CHECK (p.kind != "polyphony");
    }
}

TEST_CASE ("Parts, channels, drums and range are checked", "[hardware]")
{
    auto lib = library();
    lib.setCapabilities ("novation-xiosynth", { 8, 1, 36, 84, {}, "user" });
    Song s = band (2);
    const auto piano = idOf (s, "Piano");
    const auto melody = idOf (s, "Melody");
    const auto drums = idOf (s, "Drums");
    onPort (s, piano, "XioSynth", 1);
    onPort (s, melody, "XioSynth", 2);
    onPort (s, drums, "D-05", 10);
    edit::modifyClip (s, edit::findTrack (s, melody)->clips.front()->id, [] (Clip& c) { c.notes.push_back ({ 0, kBeat, 96, 90 }); });

    const auto report = hardware::check (s, lib);
    std::set<std::string> kinds;
    for (const auto& p : report.proposals)
        kinds.insert (p.kind);
    CHECK (kinds.count ("parts") == 1);
    CHECK (kinds.count ("drums") == 1);
    CHECK (kinds.count ("range") == 1);

    // The melody stays on the one-part XioSynth; the piano moves.
    const auto parts = std::find_if (report.proposals.begin(), report.proposals.end(), [] (auto& p) { return p.kind == "parts"; });
    CHECK (parts->tracks == std::vector<TrackId> { piano });
    const auto drumFix = std::find_if (report.proposals.begin(), report.proposals.end(), [] (auto& p) { return p.kind == "drums"; });
    const auto after = withOption (s, drumFix->options.front());
    CHECK (edit::findTrack (after, drums)->output.kind == TrackOutput::Kind::soundFont);
    CHECK (edit::findTrack (after, drums)->output.channel == 10);
}

// ---- the AI tools ----------------------------------------------------------------
namespace
{
    struct ArrangeHost final : mcp::Host
    {
        Document doc;
        DeviceLibrary lib = library();
        Preferences prefs;
        std::map<std::string, DeviceCapabilities> saved;
        const Song& song() const override { return doc.song(); }
        std::uint64_t songRevision() const override { return doc.revision(); }
        void edit (const std::string& d, const std::function<void (Song&)>& fn) override { doc.edit (d, fn); }
        bool undo() override { return doc.undo(); }
        const DeviceLibrary* devices() const override { return &lib; }
        const Preferences* arrangingPreferences() const override { return &prefs; }
        void rememberArrangingChoice (const Proposal& p, int option) override { prefs.took (p, option); }
        void setDeviceCapabilities (const std::string& id, const DeviceCapabilities& c) override
        {
            saved[id] = c;
            lib.setCapabilities (id, c);
        }
    };
}

TEST_CASE ("The AI tools run the doctor and the hardware check", "[arranging][mcp]")
{
    ArrangeHost host;
    host.doc.edit ("band", [] (Song& s) { s = band (8); });
    mcp::Server server (host);

    const auto report = nlohmann::json::parse (server.call ("arrangement_doctor", R"({"goal": "clear_melody"})"));
    REQUIRE_FALSE (report["proposals"].empty());
    CHECK (report["roles"]["Melody"] == "melody");
    const auto id = report["proposals"][0]["proposal"].get<std::string>();
    CHECK_THROWS (server.call ("arrangement_doctor", R"({"goal": "stand_out"})"));

    const auto before = host.doc.song();
    const auto applied = nlohmann::json::parse (server.call ("apply_arrangement_change", nlohmann::json ({ { "goal", "clear_melody" }, { "proposal", id }, { "option", 2 } }).dump()));
    CHECK (applied.contains ("applied"));
    CHECK_FALSE (host.doc.song() == before);
    CHECK (host.prefs.weight ("overlap") > 1.0);
    host.doc.undo();
    CHECK (host.doc.song() == before);

    // Hardware: the piano on the D-05 with 8-note chords.
    host.doc.edit ("d05", [] (Song& s) {
        auto& t = *edit::findTrack (s, idOf (s, "Piano"));
        t.output = { TrackOutput::Kind::midiPort, "D-05", 1 };
        edit::modifyClip (s, t.clips.front()->id, [] (Clip& c) {
            c.notes.clear();
            chordEvery (c.notes, 8, kBar, { 48, 52, 55, 60, 64, 67, 72, 76, 79 });
        });
    });
    const auto hw = nlohmann::json::parse (server.call ("check_hardware", "{}"));
    bool d05 = false;
    for (const auto& d : hw["devices"])
        if (d["profile"] == "roland-d-05")
        {
            d05 = true;
            CHECK (d["most_notes_at_once"] == 9);
            CHECK_FALSE (d["problems"].empty());
        }
    CHECK (d05);
    REQUIRE_FALSE (hw["proposals"].empty());
    server.call ("apply_hardware_change", nlohmann::json ({ { "proposal", "polyphony:D-05" }, { "option", 1 } }).dump());
    CHECK (host.doc.song().tracks.size() == 4);

    // The user's own numbers.
    server.call ("set_device_capabilities", R"({"device": "roland-d-05", "polyphony": 16})");
    CHECK (host.saved["roland-d-05"].polyphony == 16);
    CHECK (host.saved["roland-d-05"].parts == 2);
    CHECK_THROWS (server.call ("set_device_capabilities", R"({"device": "nope", "polyphony": 16})"));
}

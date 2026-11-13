#include "arranger/core/Style.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace arranger;

namespace
{
    constexpr Tick kBeat = kTicksPerQuarter;

    Style demoStyle (int format)
    {
        Style s;
        s.name = "TestBeat";
        s.format = format;
        s.tempo = 96.0;
        s.timeSignature = { 4, 4 };
        s.setup = {
            { 0, { 0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7 } },              // GM on
            { 240, { 0xb8, 0x00, 0x7f } }, { 240, { 0xb8, 0x20, 0x00 } }, { 240, { 0xc8, 0x00 } }, // drum kit on channel 9
            { 240, { 0xba, 0x07, 100 } },
        };

        StyleSection main;
        main.name = "Main A";
        main.length = kBeat * 8;
        Clip drums;
        drums.name = "Main A";
        drums.length = main.length;
        drums.notes = { { 0, 120, 36, 110 }, { kBeat, 120, 38, 100 }, { kBeat * 2, 120, 36, 110 }, { kBeat * 3, 120, 38, 100 } };
        Clip bass;
        bass.name = "Main A";
        bass.length = main.length;
        bass.notes = { { 0, kBeat, 36, 100 }, { kBeat * 2, kBeat * 6, 43, 90 } }; // the last note runs to the end
        bass.controls = { { kBeat, ControlEvent::Kind::controller, 11, 90 }, { kBeat * 2, ControlEvent::Kind::pitchBend, 0, -512 } };
        main.parts[9] = drums;
        main.parts[11] = bass;

        StyleSection fill;
        fill.name = "Fill In AA";
        fill.length = kBeat * 4;
        Clip f;
        f.name = "Fill In AA";
        f.length = fill.length;
        f.notes = { { 0, 60, 38, 120 }, { 240, 60, 38, 90 }, { 480, 60, 45, 100 } };
        fill.parts[9] = f;
        s.sections = { main, fill };

        StyleRuleSet set;
        set.sections = { "Main A", "Fill In AA" };
        StyleChannelRule drumRule;
        drumRule.sourceChannel = 9;
        drumRule.name = "Rhythm";
        drumRule.destChannel = 9;
        drumRule.transpose.ntr = StyleTranspose::Ntr::rootFixed;
        drumRule.transpose.ntt = StyleTranspose::Ntt::bypass;
        drumRule.transpose.rtr = StyleTranspose::Rtr::stop;
        StyleChannelRule bassRule;
        bassRule.sourceChannel = 11;
        bassRule.name = "Bass";
        bassRule.destChannel = 11;
        bassRule.sourceRoot = 0;
        bassRule.sourceChordType = 2;
        bassRule.noteMute = 0x0fff;
        bassRule.transpose.ntt = StyleTranspose::Ntt::melody;
        bassRule.transpose.bass = true;
        bassRule.transpose.highKey = 7;
        bassRule.transpose.lowLimit = 28;
        bassRule.transpose.highLimit = 60;
        bassRule.transpose.rtr = StyleTranspose::Rtr::pitchShiftToRoot;
        if (format == 2)
        {
            bassRule.lowRange = bassRule.transpose;
            bassRule.highRange = bassRule.transpose;
            drumRule.lowRange = drumRule.transpose;
            drumRule.highRange = drumRule.transpose;
        }
        set.rules = { drumRule, bassRule };
        s.ruleSets = { set };
        return s;
    }
}

TEST_CASE ("A style survives writing and reading, in both Yamaha formats", "[style]")
{
    for (const int format : { 1, 2 })
    {
        const auto style = demoStyle (format);
        const auto bytes = style_file::write (style);
        REQUIRE (bytes.size() > 100);
        CHECK (std::equal (bytes.begin(), bytes.begin() + 4, "MThd"));

        auto back = style_file::read (bytes);
        // Reading fills in the one "no special features" byte the writer adds.
        for (auto& set : back.ruleSets)
            for (auto& r : set.rules)
            {
                CHECK (r.special == std::vector<std::uint8_t> { 0 });
                r.special.clear();
            }
        CHECK_THAT (back.tempo, Catch::Matchers::WithinAbs (96.0, 0.001));
        back.tempo = style.tempo;
        CHECK (back.name == style.name);
        CHECK (back.format == format);
        CHECK (back.setup == style.setup);
        REQUIRE (back.sections.size() == 2);
        CHECK (back.sections[0] == style.sections[0]);
        CHECK (back.sections[1] == style.sections[1]);
        CHECK (back.ruleSets == style.ruleSets);
        CHECK (back == style);

        // And the second write gives the same bytes: nothing drifts.
        CHECK (style_file::write (style_file::read (bytes)) == bytes);
    }
}

TEST_CASE ("Style lookups find sections and chord rules", "[style]")
{
    const auto style = demoStyle (2);
    REQUIRE (style.section ("Fill In AA") != nullptr);
    CHECK (style.section ("Fill In AA")->length == kBeat * 4);
    CHECK (style.section ("Ending A") == nullptr);
    REQUIRE (style.rule ("Main A", 11) != nullptr);
    CHECK (style.rule ("Main A", 11)->transpose.bass);
    CHECK (style.rule ("Main A", 12) == nullptr);
    CHECK (style.rule ("Intro A", 11) == nullptr);
}

TEST_CASE ("Chunks after the chord rules are kept, and bad files say why", "[style]")
{
    auto style = demoStyle (2);
    style.extra = { 'O', 'T', 'S', 'c', 0, 0, 0, 3, 1, 2, 3 };
    const auto back = style_file::read (style_file::write (style));
    CHECK (back.extra == style.extra);

    CHECK_THROWS_AS (style_file::read (std::vector<std::uint8_t> { 1, 2, 3 }), style_file::Error);

    // A plain MIDI file is not a style.
    auto plain = demoStyle (1);
    auto bytes = style_file::write (plain);
    const std::string marker = "SFF1";
    const auto it = std::search (bytes.begin(), bytes.end(), marker.begin(), marker.end());
    REQUIRE (it != bytes.end());
    *it = 'X';
    CHECK_THROWS_AS (style_file::read (bytes), style_file::Error);

    // A file cut short does not crash.
    bytes = style_file::write (demoStyle (2));
    for (std::size_t cut : { bytes.size() / 4, bytes.size() / 2, bytes.size() - 5 })
    {
        try { style_file::read (std::span (bytes).first (cut)); } catch (const style_file::Error&) {}
    }
}

#include "arranger/core/StyleChord.h"

TEST_CASE ("Chord names", "[style]")
{
    CHECK (style_chord::name ({ 0, ChordType::major, -1 }) == "C");
    CHECK (style_chord::name ({ 6, ChordType::minor7, -1 }) == "F#m7");
    CHECK (style_chord::name ({ 10, ChordType::seventhSus4, -1 }) == "Bb7sus4");
    CHECK (style_chord::name ({ 0, ChordType::major, 4 }) == "C/E");
    CHECK (style_chord::tones (ChordType::minor7) == std::vector<int> { 0, 3, 7, 10 });
    CHECK (style_chord::tones (ChordType::seventh9) == std::vector<int> { 0, 4, 7, 10, 14 });
}

TEST_CASE ("Style notes follow the chord by the channel's rule", "[style]")
{
    using style_chord::transpose;

    // A bass line written over CMaj7: the root moves, up to the high key and then down an octave.
    StyleChannelRule bass;
    bass.sourceRoot = 0;
    bass.sourceChordType = static_cast<int> (ChordType::major7);
    bass.transpose.ntt = StyleTranspose::Ntt::melody;
    bass.transpose.highKey = 6; // F#
    bass.transpose.lowLimit = 28;
    bass.transpose.highLimit = 60;
    CHECK (transpose (36, bass, { 0, ChordType::major, -1 }) == 36);  // C stays
    CHECK (transpose (36, bass, { 5, ChordType::major, -1 }) == 41);  // F: up
    CHECK (transpose (36, bass, { 7, ChordType::major, -1 }) == 31);  // G: above the high key, down
    CHECK (transpose (40, bass, { 9, ChordType::minor, -1 }) == 36);  // the third (E) becomes A minor's C, down an octave
    CHECK (transpose (43, bass, { 2, ChordType::minor7Flat5, -1 }) == 44); // the fifth becomes the flat fifth of D (Ab)
    CHECK (transpose (38, bass, { 0, ChordType::minor, -1 }) == 38);  // D is in both scales

    // Bass on: an on-bass chord moves the line to the bass note.
    bass.transpose.bass = true;
    CHECK (transpose (36, bass, { 0, ChordType::major, 4 }) == 40);   // C/E: E
    bass.transpose.bass = false;

    // The limits fold the result back by octaves.
    bass.transpose.lowLimit = 36;
    bass.transpose.highLimit = 60;
    CHECK (transpose (36, bass, { 7, ChordType::major, -1 }) == 43);

    // A chord part with a fixed root keeps its place on the keyboard and takes the nearest chord notes.
    StyleChannelRule chord;
    chord.sourceRoot = 0;
    chord.sourceChordType = static_cast<int> (ChordType::major7);
    chord.transpose.ntr = StyleTranspose::Ntr::rootFixed;
    chord.transpose.ntt = StyleTranspose::Ntt::chord;
    CHECK (transpose (52, chord, { 9, ChordType::minor, -1 }) == 48);  // E -> C (third of A minor), nearest below
    CHECK (transpose (55, chord, { 9, ChordType::minor, -1 }) == 52);  // G -> E (fifth)
    CHECK (transpose (59, chord, { 0, ChordType::major, -1 }) == 60);  // B (the seventh) -> C, a triad has no seventh
    CHECK (transpose (59, chord, { 0, ChordType::seventh, -1 }) == 58); // ... or Bb in C7
    CHECK (transpose (52, chord, { 0, ChordType::sus4, -1 }) == 53);   // E -> F

    // Drums are never moved, and they go on when the chord is cancelled. Other parts stop.
    StyleChannelRule drums;
    drums.transpose.ntr = StyleTranspose::Ntr::rootFixed;
    drums.transpose.ntt = StyleTranspose::Ntt::bypass;
    CHECK (transpose (38, drums, { 7, ChordType::minor, -1 }) == 38);
    CHECK (transpose (38, drums, { 0, ChordType::cancel, -1 }) == 38);
    CHECK_FALSE (transpose (52, chord, { 0, ChordType::cancel, -1 }).has_value());

    // SFF2: notes outside the middle range use their own rule.
    StyleChannelRule split = chord;
    split.middleLow = 48;
    split.lowRange = bass.transpose;
    split.lowRange.lowLimit = 0;
    split.lowRange.highLimit = 127;
    CHECK (transpose (36, split, { 5, ChordType::major, -1 }) == 41);  // low range: root transposition
    CHECK (transpose (52, split, { 9, ChordType::minor, -1 }) == 48);  // middle range: fixed root

    // Mutes.
    StyleChannelRule muted = chord;
    muted.noteMute = static_cast<std::uint16_t> (0x0fff & ~(1 << 7)); // silent on G
    muted.chordMute &= ~(1ULL << static_cast<int> (ChordType::diminished));
    CHECK (style_chord::plays (muted, { 0, ChordType::major, -1 }));
    CHECK_FALSE (style_chord::plays (muted, { 7, ChordType::major, -1 }));
    CHECK_FALSE (style_chord::plays (muted, { 0, ChordType::diminished, -1 }));
}

TEST_CASE ("Held keys name a chord", "[style]")
{
    const auto chord = [] (std::initializer_list<int> keys) {
        const std::vector<int> v (keys);
        const auto c = style_chord::detect (v);
        return c ? style_chord::name (*c) : std::string ("?");
    };
    CHECK (chord ({ 48, 52, 55 }) == "C");
    CHECK (chord ({ 52, 55, 60 }) == "C/E");          // first inversion: on bass
    CHECK (chord ({ 45, 48, 52 }) == "Am");
    CHECK (chord ({ 48, 52, 55, 58 }) == "C7");
    CHECK (chord ({ 48, 52, 58 }) == "C7");           // no fifth
    CHECK (chord ({ 50, 53, 57, 60 }) == "Dm7");
    CHECK (chord ({ 53, 57, 60, 64 }) == "FM7");
    CHECK (chord ({ 55, 60, 62 }) == "Gsus4");
    CHECK (chord ({ 47, 50, 53 }) == "Bdim");
    CHECK (chord ({ 48, 52, 56 }) == "Caug");
    CHECK (chord ({ 48 }) == "C1+8");
    CHECK (chord ({ 48, 55 }) == "C1+5");
    CHECK (chord ({ 48, 51, 54, 57 }) == "Cdim7");
    CHECK (chord ({ 48, 49, 50 }) == "?");
    CHECK (chord ({}) == "?");
}

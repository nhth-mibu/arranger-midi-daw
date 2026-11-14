#include "arranger/core/SongEdit.h"
#include "arranger/core/Style.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <set>
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

#include "arranger/core/ProjectFile.h"

TEST_CASE ("A style opens as a song with its sections in the Clip grid, and goes back", "[style]")
{
    const auto style = demoStyle (2);
    const auto song = style_convert::toSong (style);

    CHECK (song.title == "TestBeat");
    CHECK (song.scenes == std::vector<std::string> { "Main A", "Fill In AA" });
    REQUIRE (song.tracks.size() == 2);
    CHECK (song.tracks[0].styleChannel == 9);
    CHECK (song.tracks[0].name == "Rhythm");
    CHECK (song.tracks[0].sound.bankMsb == 127);
    CHECK (song.tracks[0].sound.program == 0);
    CHECK (song.tracks[1].styleChannel == 11);
    CHECK (song.tracks[1].output.channel == 11);
    CHECK (song.tracks[1].mixer.volume == 100);
    REQUIRE (song.tracks[1].slots.size() == 2);
    CHECK (song.tracks[1].slots[1] == nullptr); // the bass has no fill
    CHECK (song.tracks[1].slots[0]->notes.size() == 2);
    REQUIRE (song.style.has_value());
    CHECK (song.style->setup.size() == 1); // only the SysEx: voices and volumes are on the tracks now

    // The project file keeps the style.
    const auto reopened = project_file::fromJson (project_file::toJson (song));
    CHECK (reopened == song);

    const auto back = style_convert::fromSong (reopened);
    CHECK (back.name == style.name);
    CHECK (back.format == 2);
    CHECK (back.sections == style.sections);
    CHECK (back.ruleSets == style.ruleSets);
    // The setup bar has the SysEx, then a full set of voice and mixer messages for each part.
    REQUIRE (back.setup.size() > 5);
    CHECK (back.setup[0] == style.setup[0]);
    const auto has = [&] (std::vector<std::uint8_t> bytes) {
        return std::any_of (back.setup.begin(), back.setup.end(), [&] (const StyleEvent& e) { return e.bytes == bytes; });
    };
    CHECK (has ({ 0xb8, 0x00, 0x7f }));
    CHECK (has ({ 0xc8, 0x00 }));
    CHECK (has ({ 0xba, 0x07, 100 }));

    // And the whole thing survives the trip through a .sty file.
    const auto again = style_convert::toSong (style_file::read (style_file::write (back)));
    CHECK (again.tracks[1].slots[0]->notes == song.tracks[1].slots[0]->notes);
    CHECK (again.tracks[0].sound == song.tracks[0].sound);
}

TEST_CASE ("Playing a chord moves the style's slots and nothing else", "[style]")
{
    auto song = style_convert::toSong (demoStyle (2));
    Clip timeline;
    timeline.notes = { { 0, 100, 36, 100 } };
    timeline.id = song.nextId++;
    song.tracks[1].clips.push_back (std::make_shared<const Clip> (timeline));

    const auto inF = style_convert::applyChord (song, { 5, ChordType::major, -1 });
    CHECK (inF.tracks[0].slots[0]->notes == song.tracks[0].slots[0]->notes);   // drums: as recorded
    CHECK (inF.tracks[1].slots[0]->notes[0].pitch == 41);                      // bass C -> F
    CHECK (inF.tracks[1].slots[0]->notes[1].pitch == 48);                      // G -> C
    CHECK (inF.tracks[1].slots[0]->id == song.tracks[1].slots[0]->id);
    CHECK (inF.tracks[1].clips[0]->notes[0].pitch == 36);                      // the Timeline stays

    const auto stopped = style_convert::applyChord (song, { 0, ChordType::cancel, -1 });
    CHECK (stopped.tracks[0].slots[0]->notes.size() == 4);
    CHECK (stopped.tracks[1].slots[0]->notes.empty());

    // A song with no style is not touched.
    Song plain;
    CHECK (style_convert::applyChord (plain, { 5, ChordType::major, -1 }) == plain);
}

TEST_CASE ("Any Clip grid can become a style", "[style]")
{
    Song song;
    for (const int channel : { 10, 1, 2 })
    {
        Track t;
        t.id = song.nextId++;
        t.output.channel = channel;
        Clip c;
        c.id = song.nextId++;
        c.length = kBeat * 4;
        c.notes = { { 0, 100, 60, 100 } };
        t.slots = { std::make_shared<const Clip> (c), nullptr, std::make_shared<const Clip> (c) };
        song.tracks.push_back (t);
    }
    song.tracks.push_back (Track {}); // no slots: stays an ordinary track
    song.scenes = { "Verse" };

    style_convert::makeStyle (song);
    REQUIRE (song.style.has_value());
    CHECK (song.tracks[0].styleChannel == 10);
    CHECK (song.tracks[1].styleChannel == 11);
    CHECK (song.tracks[2].styleChannel == 12);
    CHECK (song.tracks[2].output.channel == 12);
    CHECK (song.tracks[3].styleChannel == 0);
    CHECK (song.scenes[0] == "Main A");
    CHECK (song.scenes[2] == "Main B");

    // A one-bar slot in a two-bar section repeats in the file.
    Clip longer = *song.tracks[1].slots[0];
    longer.length = kBeat * 8;
    longer.id = song.nextId++;
    song.tracks[1].slots[0] = std::make_shared<const Clip> (longer);
    const auto style = style_convert::fromSong (song);
    REQUIRE (style.sections.size() == 2);
    CHECK (style.sections[0].length == kBeat * 8);
    CHECK (style.sections[0].parts.at (10).notes.size() == 2);
    CHECK (style.sections[0].parts.at (11).notes.size() == 1);
    CHECK (style_convert::ruleFor (song, song.tracks[0], 0).transpose.ntt == StyleTranspose::Ntt::bypass);
    CHECK (style_convert::ruleFor (song, song.tracks[1], 0).transpose.bass);
}

TEST_CASE ("A chord rule changes for all sections or for one", "[style]")
{
    auto song = style_convert::toSong (demoStyle (2));
    const auto bass = *std::find_if (song.tracks.begin(), song.tracks.end(), [] (const Track& t) { return t.styleChannel == 11; });
    const int mainA = static_cast<int> (std::find (song.scenes.begin(), song.scenes.end(), "Main A") - song.scenes.begin());
    const int fill = static_cast<int> (std::find (song.scenes.begin(), song.scenes.end(), "Fill In AA") - song.scenes.begin());
    REQUIRE (mainA < static_cast<int> (song.scenes.size()));
    REQUIRE (fill < static_cast<int> (song.scenes.size()));

    auto rule = style_convert::ruleFor (song, bass, mainA);
    rule.transpose.highKey = 3;
    style_convert::setRule (song, bass, -1, rule);
    CHECK (style_convert::ruleFor (song, bass, mainA).transpose.highKey == 3);
    CHECK (style_convert::ruleFor (song, bass, fill).transpose.highKey == 3);

    rule.transpose.highKey = 9;
    style_convert::setRule (song, bass, fill, rule);
    CHECK (style_convert::ruleFor (song, bass, mainA).transpose.highKey == 3);
    CHECK (style_convert::ruleFor (song, bass, fill).transpose.highKey == 9);

    // The rules go to the file and come back.
    const auto again = style_convert::toSong (style_file::read (style_file::write (style_convert::fromSong (song))));
    const auto bass2 = *std::find_if (again.tracks.begin(), again.tracks.end(), [] (const Track& t) { return t.styleChannel == 11; });
    CHECK (style_convert::ruleFor (again, bass2, mainA).transpose.highKey == 3);
    CHECK (style_convert::ruleFor (again, bass2, fill).transpose.highKey == 9);

    // A new style has no rule sets: the first change makes one.
    Song fresh;
    Track t;
    t.id = fresh.nextId++;
    t.styleChannel = 12;
    t.output.channel = 12;
    fresh.tracks.push_back (t);
    fresh.scenes = { "Main A", "Main B" };
    fresh.style = StyleData {};
    auto r = style_convert::ruleFor (fresh, t, 0);
    r.transpose.ntt = StyleTranspose::Ntt::melody;
    style_convert::setRule (fresh, t, -1, r);
    CHECK (style_convert::ruleFor (fresh, t, 1).transpose.ntt == StyleTranspose::Ntt::melody);
    REQUIRE (fresh.style->ruleSets.size() == 1);
    CHECK (fresh.style->ruleSets[0].sections.size() == 2);
}

TEST_CASE ("The chord of a passage", "[style]")
{
    const auto guess = [] (std::initializer_list<int> notes, int bass) {
        double w[12] = {};
        for (const int n : notes)
            w[n % 12] += 1.0;
        return style_chord::guess (std::span<const double, 12> (w), bass);
    };
    CHECK (guess ({ 60, 64, 67 }, 0) == Chord { 0, ChordType::major, -1 });
    CHECK (guess ({ 57, 60, 64 }, 9) == Chord { 9, ChordType::minor, -1 });
    CHECK (guess ({ 57, 60, 64, 67 }, 9) == Chord { 9, ChordType::minor7, -1 });
    CHECK (guess ({ 57, 60, 64, 67 }, 0) == Chord { 0, ChordType::major6, -1 });
    CHECK (guess ({ 55, 59, 62, 65 }, 7) == Chord { 7, ChordType::seventh, -1 });
    CHECK (guess ({ 60, 64, 67, 72, 76, 79, 62 }, -1) == Chord { 0, ChordType::major, -1 }); // a passing D does not change it
    CHECK_FALSE (guess ({ 60 }, 0).has_value());
}

TEST_CASE ("A part of the Timeline becomes a section of a style", "[style]")
{
    Song song;
    const Tick bar = song.timeSignature.ticksPerBar();
    const auto add = [&] (int channel, std::vector<Note> notes) {
        Track t;
        t.id = song.nextId++;
        t.output.channel = channel;
        Clip c;
        c.id = song.nextId++;
        c.start = 0;
        c.length = bar * 4;
        c.notes = std::move (notes);
        t.clips.push_back (std::make_shared<const Clip> (c));
        song.tracks.push_back (t);
    };
    // Two bars of A minor, then two bars of F major.
    add (10, { { 0, 100, 36, 100 }, { bar * 2, 100, 36, 100 } });
    add (2, { { 0, bar, 45, 100 }, { bar, bar, 52, 100 }, { bar * 2, bar * 2 + 500, 41, 100 } });
    add (1, { { 0, bar * 2, 57, 90 }, { 0, bar * 2, 60, 90 }, { 0, bar * 2, 64, 90 }, { bar * 2, bar * 2, 60, 90 }, { bar * 2, bar * 2, 65, 90 }, { bar * 2, bar * 2, 69, 90 } });
    add (3, {}); // an empty track gets no part

    const int a = style_convert::sectionFromTimeline (song, 0, bar * 2, "Main A");
    const int b = style_convert::sectionFromTimeline (song, bar * 2, bar * 4, "Main B");
    REQUIRE (a == 0);
    REQUIRE (b == 1);
    REQUIRE (song.style.has_value());
    CHECK (song.scenes == std::vector<std::string> { "Main A", "Main B" });
    CHECK (song.tracks[0].styleChannel == 10);
    CHECK (song.tracks[1].styleChannel == 11);
    CHECK (song.tracks[2].styleChannel == 12);
    CHECK (song.tracks[3].styleChannel == 0);
    CHECK (song.launchQuantise == bar);

    REQUIRE (song.tracks[1].slots.size() >= 2);
    const auto& bassB = *song.tracks[1].slots[1];
    CHECK (bassB.length == bar * 2);
    REQUIRE (bassB.notes.size() == 1);
    CHECK (bassB.notes[0].start == 0);
    CHECK (bassB.notes[0].length == bar * 2); // cut at the end of the section

    // Each section knows the chord it was written in, so both play right in C.
    CHECK (style_convert::ruleFor (song, song.tracks[2], 0).sourceRoot == 9);
    CHECK (style_convert::ruleFor (song, song.tracks[2], 0).sourceChordType == static_cast<int> (ChordType::minor));
    CHECK (style_convert::ruleFor (song, song.tracks[2], 1).sourceRoot == 5);
    CHECK (style_convert::ruleFor (song, song.tracks[2], 1).sourceChordType == static_cast<int> (ChordType::major));

    const auto inC = style_convert::applyChord (song, { 0, ChordType::major, -1 });
    for (const std::size_t scene : { std::size_t { 0 }, std::size_t { 1 } })
    {
        std::set<int> classes;
        for (const auto& n : inC.tracks[2].slots[scene]->notes)
            classes.insert (n.pitch % 12);
        CHECK (classes == std::set<int> { 0, 4, 7 });
    }

    // The same name again replaces the section.
    CHECK (style_convert::sectionFromTimeline (song, bar * 2, bar * 4, "Main A") == 0);
    CHECK (song.tracks[1].slots[0]->notes.size() == 1);
    CHECK (style_convert::sectionFromTimeline (song, bar * 8, bar * 9, "Main C") == -1);
}

TEST_CASE ("What real PSR-S970 styles need", "[style]")
{
    // A rule as the keyboard's own styles write it (Ctb2): a piano part with a bass range below C3.
    const std::vector<std::uint8_t> ctb2 = { 0x0c, 'E', '.', 'P', 'i', 'a', 'n', 'o', ' ', 0x0c, 0x01, 0x0f, 0xff, 0x03, 0xff, 0xff, 0xff, 0xff, 0x00, 0x02, 0x30, 0x7f,
                                             0x00, 0x81, 0x08, 0x24, 0x2f, 0x02, 0x01, 0x02, 0x07, 0x00, 0x7f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x00,
                                             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    auto style = demoStyle (2);
    auto bytes = style_file::write (style);
    // Put the rule in place of the file's rules: build a CASM by hand.
    const auto be32 = [] (std::vector<std::uint8_t>& out, std::size_t n) { for (int s = 24; s >= 0; s -= 8) out.push_back (static_cast<std::uint8_t> (n >> s)); };
    const auto chunk = [&] (const char* id, const std::vector<std::uint8_t>& body) {
        std::vector<std::uint8_t> out (id, id + 4);
        be32 (out, body.size());
        out.insert (out.end(), body.begin(), body.end());
        return out;
    };
    const std::string names = "Main A";
    auto cseg = chunk ("Sdec", { names.begin(), names.end() });
    const auto rule = chunk ("Ctb2", ctb2);
    cseg.insert (cseg.end(), rule.begin(), rule.end());
    const auto casm = chunk ("CASM", chunk ("CSEG", cseg));
    const std::string all (bytes.begin(), bytes.end());
    const auto at = all.find ("CASM");
    REQUIRE (at != std::string::npos);
    bytes.resize (at);
    bytes.insert (bytes.end(), casm.begin(), casm.end());

    const auto read = style_file::read (bytes);
    REQUIRE (read.rule ("Main A", 13) != nullptr);
    const auto& r = *read.rule ("Main A", 13);
    CHECK (r.name == "E.Piano");
    CHECK (r.destChannel == 13);
    CHECK (r.sourceChordType == static_cast<int> (ChordType::major7));
    CHECK (r.middleLow == 0x30);
    CHECK (r.lowRange.bass);
    CHECK (r.lowRange.ntt == StyleTranspose::Ntt::melody);
    CHECK (r.lowRange.lowLimit == 0x24);
    CHECK (r.transpose.ntr == StyleTranspose::Ntr::rootFixed);
    CHECK (r.transpose.ntt == StyleTranspose::Ntt::chord);
    CHECK (r.transpose.highKey == 7);
    CHECK (r.special.size() == 7);

    // The rule goes back to the file byte for byte.
    const auto again = style_file::write (read);
    const std::string out (again.begin(), again.end());
    CHECK (out.substr (out.find ("CASM")) == std::string (casm.begin(), casm.end()));

    // With no chord, only the parts that the rule lets through keep playing (the rhythm parts).
    CHECK_FALSE (style_chord::plays (r, { 0, ChordType::cancel, -1 }));
    auto drums = r;
    drums.chordMute = 0x07ffffffffULL;
    CHECK (style_chord::plays (drums, { 0, ChordType::cancel, -1 }));

    // Guitar parts land on notes of the chord, near where they were written.
    auto guitar = r;
    guitar.middleLow = 0;
    guitar.transpose.ntr = StyleTranspose::Ntr::guitar;
    for (const int pitch : { 52, 55, 59, 60, 62, 64, 65, 71 })
    {
        const auto moved = style_chord::transpose (pitch, guitar, { 9, ChordType::minor, -1 });
        REQUIRE (moved.has_value());
        const int pc = *moved % 12;
        CHECK ((pc == 9 || pc == 0 || pc == 4));
        CHECK (std::abs (*moved - pitch) <= 6);
    }

    // SysEx, tempo changes and text inside a section stay in the file.
    Style withEvents = demoStyle (2);
    withEvents.sections[0].events = { { 0, { 0xff, 0x01, 0x03, 'f', 'n', ':' } }, { kBeat, { 0xf0, 0x43, 0x10, 0x4c, 0x08, 0x0f, 0x11, 0x74, 0xf7 } }, { kBeat * 2, { 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20 } } };
    const auto kept = style_file::read (style_file::write (withEvents));
    CHECK (kept.sections[0].events == withEvents.sections[0].events);
    CHECK (kept.tempo == withEvents.tempo);
    const auto song = style_convert::toSong (kept);
    CHECK (style_convert::fromSong (song).sections[0].events == withEvents.sections[0].events);
    CHECK (project_file::fromJson (project_file::toJson (song)).style == song.style);

    // Older converted styles add Cntt chunks to a CSEG; they stay.
    {
        auto plain = demoStyle (1);
        auto file = style_file::write (plain);
        const std::string text (file.begin(), file.end());
        const auto casmAt = text.find ("CASM");
        REQUIRE (casmAt != std::string::npos);
        const std::vector<std::uint8_t> cntt = { 'C', 'n', 't', 't', 0, 0, 0, 2, 0x0a, 0x81 };
        // Grow the first CSEG and the CASM by the new chunk.
        const auto csegAt = text.find ("CSEG", casmAt);
        const auto grow = [&] (std::size_t pos) {
            std::uint32_t n = (std::uint32_t (file[pos + 4]) << 24) | (std::uint32_t (file[pos + 5]) << 16) | (std::uint32_t (file[pos + 6]) << 8) | file[pos + 7];
            n += static_cast<std::uint32_t> (cntt.size());
            for (int i = 0; i < 4; ++i) file[pos + 4 + static_cast<std::size_t> (i)] = static_cast<std::uint8_t> (n >> (24 - 8 * i));
            return n;
        };
        const auto csegLen = grow (csegAt);
        grow (casmAt);
        file.insert (file.begin() + static_cast<std::ptrdiff_t> (csegAt + 8 + csegLen - cntt.size()), cntt.begin(), cntt.end());
        const auto withCntt = style_file::read (file);
        REQUIRE_FALSE (withCntt.ruleSets.empty());
        CHECK (withCntt.ruleSets[0].extra == cntt);
        CHECK (style_file::write (withCntt) == file);
    }

    CHECK (style_file::hasStyleExtension ("JazzFusion.T161.prs"));
    CHECK (style_file::hasStyleExtension ("Old.STY"));
    CHECK_FALSE (style_file::hasStyleExtension ("song.mid"));
}

TEST_CASE ("Chord names are read", "[style]")
{
    using style_chord::parse;
    CHECK (parse ("C") == Chord { 0, ChordType::major, -1 });
    CHECK (parse ("F#m7") == Chord { 6, ChordType::minor7, -1 });
    CHECK (parse ("Bb7sus4") == Chord { 10, ChordType::seventhSus4, -1 });
    CHECK (parse ("C/E") == Chord { 0, ChordType::major, 4 });
    CHECK (parse ("Cmaj7") == Chord { 0, ChordType::major7, -1 });
    CHECK (parse ("Amin") == Chord { 9, ChordType::minor, -1 });
    CHECK (parse ("G9") == Chord { 7, ChordType::seventh9, -1 });
    CHECK (parse ("-")->type == ChordType::cancel);
    CHECK_FALSE (parse ("H7"));
    CHECK_FALSE (parse ("Cxyz"));
    CHECK_FALSE (parse (""));

    // Every name the app writes reads back.
    for (int type = 0; type < kChordTypeCount - 1; ++type)
        for (const int root : { 0, 3, 6, 10 })
        {
            const Chord chord { root, static_cast<ChordType> (type), -1 };
            CHECK (parse (style_chord::name (chord)) == chord);
        }

    const auto list = style_chord::parseList ("C Am | F, G7  and");
    REQUIRE (list.size() == 4);
    CHECK (list[1] == Chord { 9, ChordType::minor, -1 });
    CHECK (list[3] == Chord { 7, ChordType::seventh, -1 });
}

TEST_CASE ("A style fills tracks on the Timeline", "[style]")
{
    const auto style = demoStyle (2);
    Song song;
    song.timeSignature = { 4, 4 };
    auto& own = edit::addTrack (song, "Piano");
    own.output.channel = 11; // the channel the style's bass wants
    const auto tempo = song.baseTempo();

    style_convert::Fill fill;
    fill.section = "Main A";
    fill.chords = { { 0, ChordType::major, -1 }, { 7, ChordType::major, -1 } }; // C, G, C, G
    fill.at = kBeat * 8;
    fill.bars = 4;
    const auto made = style_convert::fillTimeline (song, style, fill);
    REQUIRE (made.size() == 2);
    REQUIRE (song.tracks.size() == 3);
    CHECK (song.baseTempo() == tempo);
    CHECK_FALSE (song.style.has_value());

    const auto& drums = song.tracks[1];
    const auto& bass = song.tracks[2];
    CHECK (drums.styleChannel == 0);
    CHECK (drums.slots.empty());
    CHECK (drums.output.channel == 10);       // a drum kit goes to the GM drum channel
    CHECK (bass.name == "Bass");
    CHECK (bass.output.channel != 11);        // the song's own track has that channel
    CHECK (bass.output.channel != 10);
    CHECK (bass.mixer.volume == 100);

    REQUIRE (bass.clips.size() == 1);
    const auto& clip = *bass.clips[0];
    CHECK (clip.start == kBeat * 8);
    CHECK (clip.length == kBeat * 16);
    // The section is 2 bars long: it plays twice. Bar 1 and 3 are in C, bar 2 and 4 in G.
    REQUIRE (clip.notes.size() == 4);
    CHECK (clip.notes[0].start == 0);
    CHECK (clip.notes[0].pitch % 12 == 0);
    CHECK (clip.notes[1].start == kBeat * 2);
    CHECK (clip.notes[1].length == kBeat * 2); // cut at the bar line, where the chord changes
    CHECK (clip.notes[2].start == kBeat * 8);
    CHECK (clip.notes[2].pitch == clip.notes[0].pitch);
    CHECK (clip.controls.size() == 4);

    // The drums do not follow the chord.
    const auto& beat = *drums.clips[0];
    CHECK (beat.notes.size() == 8);
    for (const auto& n : beat.notes)
        CHECK ((n.pitch == 36 || n.pitch == 38));

    // All in G: the bass plays G where it played C.
    Song other;
    fill.chords = { { 7, ChordType::major, -1 } };
    fill.at = 0;
    style_convert::fillTimeline (other, style, fill);
    REQUIRE (other.tracks.size() == 2);
    CHECK (other.tracks[1].clips[0]->notes[0].pitch % 12 == 7);

    // No such section: nothing changes.
    fill.section = "Ending Z";
    CHECK (style_convert::fillTimeline (other, style, fill).empty());
    CHECK (other.tracks.size() == 2);
}

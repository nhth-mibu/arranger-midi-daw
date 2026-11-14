#include "arranger/core/Theory.h"

#include <catch2/catch_test_macros.hpp>

using namespace arranger;
using namespace arranger::theory;

namespace
{
    constexpr Tick kBeat = kTicksPerQuarter;
    constexpr Tick kBar = kBeat * 4;

    Key key (const char* text)
    {
        const auto k = parseKey (text);
        REQUIRE (k.has_value());
        return *k;
    }

    void chord (std::vector<Note>& notes, Tick at, std::initializer_list<int> pitches)
    {
        for (const int p : pitches)
            notes.push_back ({ at, kBar, static_cast<std::uint8_t> (p), 90 });
    }

    /** I IV V I in a major key, one chord a bar, from `bar`. */
    void cadence (std::vector<Note>& notes, int tonic, int bar)
    {
        const int t = 48 + tonic;
        chord (notes, bar * kBar, { t, t + 4, t + 7, t + 12 });
        chord (notes, (bar + 1) * kBar, { t + 5, t + 9, t + 12, t + 17 });
        chord (notes, (bar + 2) * kBar, { t + 7, t + 11, t + 14, t + 19 });
        chord (notes, (bar + 3) * kBar, { t, t + 4, t + 7, t + 12 });
    }

    std::string roman (const char* chordText, const char* keyText)
    {
        const auto c = style_chord::parse (chordText);
        REQUIRE (c.has_value());
        return theory::roman (*c, key (keyText));
    }

    std::string fromRoman (const char* text, const char* keyText)
    {
        const auto k = key (keyText);
        const auto c = theory::fromRoman (text, k);
        REQUIRE (c.has_value());
        return chordName (*c, k);
    }
}

TEST_CASE ("Keys are read, named and spelled", "[theory]")
{
    CHECK (key ("Eb major") == Key { 3, Mode::major, -3 });
    CHECK (key ("F# minor") == Key { 6, Mode::minor, 3 });
    CHECK (key ("Bbm") == Key { 10, Mode::minor, -5 });
    CHECK (key ("Am") == Key { 9, Mode::minor, 0 });
    CHECK (key ("D dorian") == Key { 2, Mode::dorian, 0 });
    CHECK (key ("C# major").fifths == 7);
    CHECK (key ("D# major") == Key { 3, Mode::major, -3 }); // no such key: Eb major
    CHECK_FALSE (parseKey ("H major").has_value());
    CHECK_FALSE (parseKey ("C blah").has_value());

    CHECK (name (makeKey (6, Mode::major)) == "F# major");
    CHECK (name (makeKey (3, Mode::minor)) == "Eb minor");
    CHECK (name (makeKey (1, Mode::major)) == "Db major");
    CHECK (name (key ("G mixolydian")) == "G mixolydian");

    CHECK (noteName (6, key ("Db major")) == "Gb");
    CHECK (noteName (11, key ("Gb major")) == "Cb");
    CHECK (noteName (8, key ("A minor")) == "G#");
    CHECK (noteName (1, key ("D minor")) == "C#"); // the leading note, not Db
    CHECK (noteName (10, key ("C major")) == "Bb");
    CHECK (noteName (8, key ("C major")) == "G#");
    CHECK (noteName (3, key ("C major")) == "Eb");

    CHECK (chordName ({ 6, ChordType::major, -1 }, key ("Db major")) == "Gb");
    CHECK (chordName ({ 1, ChordType::seventh, 5 }, key ("Ab major")) == "Db7/F");
    CHECK (chordName ({ 1, ChordType::seventh, 5 }, key ("E major")) == "C#7/E#"); // the third of C#7 is E#
    CHECK (chordName ({ 4, ChordType::seventh, 8 }, key ("C major")) == "E7/G#");
    CHECK (chordName ({ 8, ChordType::major, -1 }, key ("C major")) == "Ab");
    CHECK (chordName ({ 4, ChordType::major, -1 }, key ("Db major")) == "E");
}

TEST_CASE ("Scales are spelled with one letter for each note", "[theory]")
{
    using V = std::vector<std::string>;
    CHECK (spellScale (2, Scale::harmonicMinor, key ("D minor")) == V { "D", "E", "F", "G", "A", "Bb", "C#" });
    CHECK (spellScale (6, Scale::major, key ("F# major")) == V { "F#", "G#", "A#", "B", "C#", "D#", "E#" });
    CHECK (spellScale (9, Scale::blues, key ("A minor")) == V { "A", "C", "D", "Eb", "E", "G" });
    CHECK (spellScale (0, Scale::chromatic, key ("C major")).size() == 12);
    CHECK (parseScale ("Harmonic-Minor") == Scale::harmonicMinor);
    CHECK (parseScale ("aeolian") == Scale::naturalMinor);
    CHECK_FALSE (parseScale ("bebop").has_value());
}

TEST_CASE ("The key of some notes is found", "[theory]")
{
    std::vector<Note> notes;
    cadence (notes, 0, 0);
    const auto c = findKey (notes, 0, 4 * kBar);
    REQUIRE (c.has_value());
    CHECK (name (c->key) == "C major");

    std::vector<Note> minor;
    chord (minor, 0, { 57, 60, 64 });
    chord (minor, kBar, { 50, 57, 62, 65 });
    chord (minor, 2 * kBar, { 52, 56, 59, 64 });
    chord (minor, 3 * kBar, { 45, 57, 60, 64 });
    const auto a = findKey (minor, 0, 4 * kBar);
    REQUIRE (a.has_value());
    CHECK (name (a->key) == "A minor");

    const auto ranked = rankKeys (pitchWeights (notes, 0, 4 * kBar));
    CHECK (ranked.size() == 24);
    CHECK (certainty (ranked) > 0.3);

    CHECK_FALSE (findKey ({ { 0, kBar, 60, 90 } }, 0, kBar).has_value());
}

TEST_CASE ("A change of key is found", "[theory]")
{
    std::vector<Note> notes;
    cadence (notes, 0, 0);
    cadence (notes, 0, 4);
    cadence (notes, 3, 8); // Eb major
    cadence (notes, 3, 12);
    const auto regions = keyRegions (notes, 0, 16 * kBar, kBar);
    REQUIRE (regions.size() == 2);
    CHECK (name (regions[0].key) == "C major");
    CHECK (name (regions[1].key) == "Eb major");
    CHECK (regions[1].start >= 7 * kBar);
    CHECK (regions[1].start <= 9 * kBar);
    CHECK (regions[1].end == 16 * kBar);
}

TEST_CASE ("Chords get roman numerals", "[theory]")
{
    CHECK (roman ("C", "C major") == "I");
    CHECK (roman ("G7", "C major") == "V7");
    CHECK (roman ("G7/B", "C major") == "V65");
    CHECK (roman ("G7/F", "C major") == "V42");
    CHECK (roman ("C/E", "C major") == "I6");
    CHECK (roman ("C/G", "C major") == "I64");
    CHECK (roman ("Dm7", "C major") == "ii7");
    CHECK (roman ("Cmaj7", "C major") == "Imaj7");
    CHECK (roman ("Bm7b5", "C major") == "vii\xc3\xb8" "7");
    CHECK (roman ("Bdim", "C major") == "vii\xc2\xb0");
    CHECK (roman ("D7", "C major") == "V7/V");
    CHECK (roman ("A", "C major") == "V/ii");
    CHECK (roman ("E7", "C major") == "V7/vi");
    CHECK (roman ("Bb", "C major") == "bVII");
    CHECK (roman ("Ab", "C major") == "bVI");
    CHECK (roman ("Fm", "C major") == "iv");
    CHECK (roman ("Db/F", "C major") == "N6");
    CHECK (roman ("G9", "C major") == "V9");
    CHECK (roman ("Fadd9", "C major") == "IVadd9");

    CHECK (roman ("E", "A minor") == "V");
    CHECK (roman ("Am", "A minor") == "i");
    CHECK (roman ("G#dim7", "A minor") == "vii\xc2\xb0" "7");
    CHECK (roman ("G", "A minor") == "VII");
    CHECK (roman ("F", "A minor") == "VI");
    CHECK (roman ("Bm7b5", "A minor") == "ii\xc3\xb8" "7");
    CHECK (roman ("C", "A minor") == "III");
}

TEST_CASE ("Roman numerals give chords", "[theory]")
{
    CHECK (fromRoman ("ii7", "C major") == "Dm7");
    CHECK (fromRoman ("V7/V", "C major") == "D7");
    CHECK (fromRoman ("V7/V", "Eb major") == "F7");
    CHECK (fromRoman ("bVI", "C major") == "Ab");
    CHECK (fromRoman ("I64", "C major") == "C/G");
    CHECK (fromRoman ("V65", "C major") == "G7/B");
    CHECK (fromRoman ("N6", "C major") == "Db/F");
    CHECK (fromRoman ("ii\xc3\xb8" "7", "C major") == "Dm7b5");
    CHECK (fromRoman ("IVmaj7", "C major") == "FM7");
    CHECK (fromRoman ("vii\xc2\xb0" "7", "A minor") == "G#dim7");
    CHECK (fromRoman ("viio7", "A minor") == "G#dim7");
    CHECK (fromRoman ("VII", "A minor") == "G");
    CHECK (fromRoman ("V", "A minor") == "E");
    CHECK (fromRoman ("iv", "A minor") == "Dm");
    CHECK (fromRoman ("V9", "F major") == "C7(9)");
    CHECK_FALSE (theory::fromRoman ("X", key ("C major")).has_value());
    CHECK_FALSE (theory::fromRoman ("Vblah", key ("C major")).has_value());

    const auto k = key ("G major");
    std::vector<std::string> names;
    for (const auto& c : progression ("I vi | IV, V7", k))
        names.push_back (chordName (c, k));
    CHECK (names == std::vector<std::string> { "G", "Em", "C", "D7" });

    // Every diatonic chord reads back as itself.
    for (const char* text : { "C major", "A minor", "Eb major", "F# minor" })
        for (const bool sevenths : { false, true })
        {
            const auto kk = key (text);
            for (const auto& c : diatonicChords (kk, sevenths))
            {
                const auto r = theory::roman (c, kk);
                const auto back = theory::fromRoman (r, kk);
                INFO (text << " " << r);
                REQUIRE (back.has_value());
                CHECK (*back == c);
            }
        }
}

TEST_CASE ("Diatonic chords of a key", "[theory]")
{
    const auto k = key ("C major");
    std::vector<std::string> names;
    for (const auto& c : diatonicChords (k, true))
        names.push_back (chordName (c, k));
    CHECK (names == std::vector<std::string> { "CM7", "Dm7", "Em7", "FM7", "G7", "Am7", "Bm7b5" });

    const auto a = key ("A minor");
    names.clear();
    for (const auto& c : diatonicChords (a, false))
        names.push_back (chordName (c, a));
    CHECK (names == std::vector<std::string> { "Am", "Bdim", "C", "Dm", "E", "F", "G" });
}

TEST_CASE ("Voice-leading faults are found", "[theory]")
{
    const auto voice = [] (std::string n, std::initializer_list<int> pitches) {
        Voice v { std::move (n), {} };
        Tick at = 0;
        for (const int p : pitches)
        {
            v.notes.push_back ({ at, kBeat, static_cast<std::uint8_t> (p), 90 });
            at += kBeat;
        }
        return v;
    };
    using K = VoiceIssue::Kind;
    const auto kinds = [] (const std::vector<VoiceIssue>& issues) {
        std::vector<K> out;
        for (const auto& i : issues)
            out.push_back (i.kind);
        return out;
    };

    // C-D over F-G: parallel fifths.
    CHECK (kinds (checkVoiceLeading ({ voice ("S", { 60, 62 }), voice ("B", { 53, 55 }) }, 0, kBar)) == std::vector<K> { K::parallelFifths });
    // Octaves.
    CHECK (kinds (checkVoiceLeading ({ voice ("S", { 60, 62 }), voice ("B", { 48, 50 }) }, 0, kBar)) == std::vector<K> { K::parallelOctaves });
    // Contrary motion into a fifth is fine.
    CHECK (checkVoiceLeading ({ voice ("S", { 64, 62 }), voice ("B", { 53, 55 }) }, 0, kBar).empty());
    // Similar motion into an octave with a leap in the top voice: hidden octaves.
    CHECK (kinds (checkVoiceLeading ({ voice ("S", { 64, 72 }), voice ("B", { 55, 60 }) }, 0, kBar)) == std::vector<K> { K::hiddenOctaves });
    // Crossing.
    CHECK (kinds (checkVoiceLeading ({ voice ("S", { 67, 67, 72, 72 }), voice ("A", { 64, 69, 64, 64 }) }, 0, kBar))
           == std::vector<K> { K::voiceCrossing });
    // A leap of a tenth.
    CHECK (kinds (checkVoiceLeading ({ voice ("S", { 60, 76 }) }, 0, kBar)) == std::vector<K> { K::largeLeap });
}

TEST_CASE ("A chord part is split into voices", "[theory]")
{
    std::vector<Note> notes;
    chord (notes, 0, { 48, 55, 64, 72 });
    chord (notes, kBar, { 53, 57, 65, 72 });
    chord (notes, 2 * kBar, { 55, 59, 62, 67 });
    const auto voices = splitVoices ("Choir", notes);
    REQUIRE (voices.size() == 4);
    CHECK (voices[0].name == "Choir 1");
    CHECK (voices[0].notes.size() == 3);
    CHECK (voices[0].notes[2].pitch == 67);
    CHECK (voices[3].notes[0].pitch == 48);
    CHECK (splitVoices ("Lead", { { 0, kBeat, 60, 90 }, { kBeat, kBeat, 62, 90 } }).size() == 1);
}

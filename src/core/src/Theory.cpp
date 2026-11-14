#include "arranger/core/Theory.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <numeric>

namespace arranger::theory
{

namespace
{
    int mod (int v, int m) noexcept { return ((v % m) + m) % m; }
    int floorDiv (int a, int b) noexcept { return (a - mod (a, b)) / b; }

    constexpr int kNaturalPc[7] = { 0, 2, 4, 5, 7, 9, 11 };
    constexpr int kLetterPlace[7] = { 0, 2, 4, -1, 1, 3, 5 }; // C D E F G A B on the line of fifths

    /** Where the tonic sits on the line of fifths, from the key signature. */
    int modeOffset (Mode m) noexcept
    {
        switch (m)
        {
            case Mode::major: return 0;
            case Mode::minor: return 3;
            case Mode::dorian: return 2;
            case Mode::phrygian: return 4;
            case Mode::lydian: return -1;
            case Mode::mixolydian: return 1;
            case Mode::locrian: return 5;
        }
        return 0;
    }

    bool majorLike (Mode m) noexcept { return m == Mode::major || m == Mode::lydian || m == Mode::mixolydian; }

    std::string lower (std::string_view s)
    {
        std::string out;
        for (const char c : s)
            out += static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return out;
    }

    std::string trim (std::string s)
    {
        while (! s.empty() && std::isspace (static_cast<unsigned char> (s.back())))
            s.pop_back();
        std::size_t i = 0;
        while (i < s.size() && std::isspace (static_cast<unsigned char> (s[i])))
            ++i;
        return s.substr (i);
    }

    std::optional<Mode> parseMode (const std::string& s)
    {
        static const std::pair<const char*, Mode> kNames[] = {
            { "", Mode::major },          { "major", Mode::major },       { "maj", Mode::major },   { "ionian", Mode::major },
            { "m", Mode::minor },         { "min", Mode::minor },         { "minor", Mode::minor }, { "aeolian", Mode::minor },
            { "dorian", Mode::dorian },   { "phrygian", Mode::phrygian }, { "lydian", Mode::lydian },
            { "mixolydian", Mode::mixolydian }, { "mixo", Mode::mixolydian }, { "locrian", Mode::locrian },
        };
        for (const auto& [n, m] : kNames)
            if (s == n)
                return m;
        return {};
    }

    const char* modeName (Mode m)
    {
        switch (m)
        {
            case Mode::major: return "major";
            case Mode::minor: return "minor";
            case Mode::dorian: return "dorian";
            case Mode::phrygian: return "phrygian";
            case Mode::lydian: return "lydian";
            case Mode::mixolydian: return "mixolydian";
            case Mode::locrian: return "locrian";
        }
        return "major";
    }
}

// ---- keys and spelling ------------------------------------------------------------------------

int letterOf (int place) { return mod (4 * place, 7); }
int alterOf (int place) { return floorDiv (place + 1, 7); }

int fifthsPlace (int pitchClass, int c)
{
    const int base = mod (7 * pitchClass, 12); // 7 * 7 = 49 = 1 (mod 12): the place of a pitch class, mod 12
    const int low = c - 5;
    return low + mod (base - low, 12);
}

std::string placeName (int place)
{
    std::string s (1, "CDEFGAB"[letterOf (place)]);
    const int a = alterOf (place);
    s.append (static_cast<std::size_t> (std::abs (a)), a > 0 ? '#' : 'b');
    return s;
}

int centre (const Key& k)
{
    // Major keys lean to the flat side (bVII, bIII are common); minor keys to the sharp side (the raised 6th and 7th).
    return k.fifths + (majorLike (k.mode) ? 2 : 3);
}

std::string noteName (int pitchClass, const Key& k) { return placeName (fifthsPlace (pitchClass, centre (k))); }

Key makeKey (int tonic, Mode m)
{
    tonic = mod (tonic, 12);
    const int s0 = mod (7 * tonic - modeOffset (m), 12);
    int s = s0 <= 5 ? s0 : s0 - 12;
    if (s0 == 6)
        s = majorLike (m) ? 6 : -6; // F# major and Eb minor
    return { tonic, m, s };
}

std::optional<Key> parseKey (std::string_view text)
{
    auto t = trim (std::string (text));
    if (t.empty())
        return {};
    const char letter = static_cast<char> (std::tolower (static_cast<unsigned char> (t[0])));
    if (letter < 'a' || letter > 'g')
        return {};
    static constexpr int kLetterStep[7] = { 5, 6, 0, 1, 2, 3, 4 }; // a b c d e f g
    const int step = kLetterStep[letter - 'a'];
    int alter = 0;
    std::size_t i = 1;
    while (i < t.size())
    {
        if (t[i] == '#')
            ++alter, ++i;
        else if (t.compare (i, 3, "\xe2\x99\xaf") == 0)
            ++alter, i += 3;
        else if (t.compare (i, 3, "\xe2\x99\xad") == 0)
            --alter, i += 3;
        // Right after the letter a "b" is a flat ("Bbm", "Eb major"): no mode name starts with b.
        else if (t[i] == 'b' && i == 1)
            --alter, ++i;
        else
            break;
    }
    if (std::abs (alter) > 1)
        return {};
    auto rest = trim (t.substr (i));
    std::optional<Mode> mode;
    if (rest == "M")
        mode = Mode::major;
    else
        mode = parseMode (lower (rest));
    if (! mode)
        return {};
    const int place = kLetterPlace[step] + 7 * alter;
    const int tonic = mod (kNaturalPc[step] + alter, 12);
    const int fifths = place - modeOffset (*mode);
    if (fifths < -7 || fifths > 7)
        return makeKey (tonic, *mode); // D# major is written as Eb major
    return Key { tonic, *mode, fifths };
}

std::string name (const Key& k) { return placeName (k.fifths + modeOffset (k.mode)) + " " + modeName (k.mode); }

std::array<int, 7> modeSteps (Mode m)
{
    switch (m)
    {
        case Mode::major: return { 0, 2, 4, 5, 7, 9, 11 };
        case Mode::minor: return { 0, 2, 3, 5, 7, 8, 10 };
        case Mode::dorian: return { 0, 2, 3, 5, 7, 9, 10 };
        case Mode::phrygian: return { 0, 1, 3, 5, 7, 8, 10 };
        case Mode::lydian: return { 0, 2, 4, 6, 7, 9, 11 };
        case Mode::mixolydian: return { 0, 2, 4, 5, 7, 9, 10 };
        case Mode::locrian: return { 0, 1, 3, 5, 6, 8, 10 };
    }
    return { 0, 2, 4, 5, 7, 9, 11 };
}

namespace
{
    /** The place of a chord's root: major keys lean to flat roots (bIII, bVI, bVII), and no Fb, Cb, E# or B# that the key does not have. */
    int rootPlace (int pc, const Key& k)
    {
        int place = fifthsPlace (pc, majorLike (k.mode) ? k.fifths : centre (k));
        const bool inKey = place >= k.fifths - 1 && place <= k.fifths + 5;
        if (! inKey && (place == -8 || place == -7))
            place += 12; // Fb, Cb -> E, B
        else if (! inKey && (place == 11 || place == 12))
            place -= 12; // E#, B# -> F, C
        return place;
    }

    /** Places above the root for each interval of a chord tone: the third is 4 places up, the minor third 3 down... */
    int intervalPlace (int semitones, ChordType type)
    {
        static constexpr int kPlace[12] = { 0, -5, 2, -3, 4, -1, -6, 1, -4, 3, -2, 5 };
        if (semitones == 8 && (type == ChordType::augmented || type == ChordType::seventhAugmented || type == ChordType::major7Augmented))
            return 8; // #5
        if (semitones == 9 && type == ChordType::diminished7)
            return -9; // bb7
        return kPlace[mod (semitones, 12)];
    }
}

std::string chordName (const Chord& c, const Key& k)
{
    if (c.type == ChordType::cancel)
        return "-";
    const auto plainRoot = style_chord::name ({ c.root, ChordType::major, -1 });
    const auto suffix = style_chord::name ({ c.root, c.type, -1 }).substr (plainRoot.size());
    const int root = rootPlace (c.root, k);
    std::string s = placeName (root) + suffix;
    if (c.bass >= 0 && mod (c.bass, 12) != mod (c.root, 12))
    {
        // The bass is spelled from the root, as the chord tone it is (E7/G#, Db/F).
        auto bass = placeName (root + intervalPlace (c.bass - c.root, c.type));
        if (bass.size() > 2)
            bass = placeName (fifthsPlace (c.bass, centre (k)));
        s += "/" + bass;
    }
    return s;
}

// ---- scales -----------------------------------------------------------------------------------

namespace
{
    struct ScaleInfo
    {
        const char* name;
        std::vector<int> steps;
        std::vector<int> letters; ///< letter above the tonic for each note; empty = spell by the key
    };

    const ScaleInfo& scaleInfo (Scale s)
    {
        static const ScaleInfo kScales[kScaleCount] = {
            { "major", { 0, 2, 4, 5, 7, 9, 11 }, { 0, 1, 2, 3, 4, 5, 6 } },
            { "natural minor", { 0, 2, 3, 5, 7, 8, 10 }, { 0, 1, 2, 3, 4, 5, 6 } },
            { "harmonic minor", { 0, 2, 3, 5, 7, 8, 11 }, { 0, 1, 2, 3, 4, 5, 6 } },
            { "melodic minor", { 0, 2, 3, 5, 7, 9, 11 }, { 0, 1, 2, 3, 4, 5, 6 } },
            { "dorian", { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 2, 3, 4, 5, 6 } },
            { "phrygian", { 0, 1, 3, 5, 7, 8, 10 }, { 0, 1, 2, 3, 4, 5, 6 } },
            { "lydian", { 0, 2, 4, 6, 7, 9, 11 }, { 0, 1, 2, 3, 4, 5, 6 } },
            { "mixolydian", { 0, 2, 4, 5, 7, 9, 10 }, { 0, 1, 2, 3, 4, 5, 6 } },
            { "locrian", { 0, 1, 3, 5, 6, 8, 10 }, { 0, 1, 2, 3, 4, 5, 6 } },
            { "major pentatonic", { 0, 2, 4, 7, 9 }, { 0, 1, 2, 4, 5 } },
            { "minor pentatonic", { 0, 3, 5, 7, 10 }, { 0, 2, 3, 4, 6 } },
            { "blues", { 0, 3, 5, 6, 7, 10 }, { 0, 2, 3, 4, 4, 6 } },
            { "whole tone", { 0, 2, 4, 6, 8, 10 }, { 0, 1, 2, 3, 4, 5 } },
            { "chromatic", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, {} },
        };
        return kScales[std::clamp (static_cast<int> (s), 0, kScaleCount - 1)];
    }
}

std::string name (Scale s) { return scaleInfo (s).name; }

std::optional<Scale> parseScale (std::string_view text)
{
    auto t = lower (trim (std::string (text)));
    std::replace (t.begin(), t.end(), '-', ' ');
    std::replace (t.begin(), t.end(), '_', ' ');
    static const std::pair<const char*, Scale> kAlias[] = {
        { "ionian", Scale::major },        { "minor", Scale::naturalMinor },        { "aeolian", Scale::naturalMinor },
        { "pentatonic", Scale::majorPentatonic }, { "mixo", Scale::mixolydian }, { "blues minor", Scale::blues },
    };
    for (const auto& [n, s] : kAlias)
        if (t == n)
            return s;
    for (int i = 0; i < kScaleCount; ++i)
        if (t == scaleInfo (static_cast<Scale> (i)).name)
            return static_cast<Scale> (i);
    return {};
}

std::vector<int> intervals (Scale s) { return scaleInfo (s).steps; }

std::vector<std::string> spellScale (int tonic, Scale s, const Key& context)
{
    const auto& info = scaleInfo (s);
    std::vector<std::string> out;
    const int tonicPlace = fifthsPlace (tonic, centre (context));
    const int tonicLetter = letterOf (tonicPlace);
    const int tonicPc = mod (tonic, 12);
    for (std::size_t i = 0; i < info.steps.size(); ++i)
    {
        const int pc = mod (tonicPc + info.steps[i], 12);
        if (info.letters.empty())
        {
            out.push_back (noteName (pc, context));
            continue;
        }
        const int letter = (tonicLetter + info.letters[i]) % 7;
        int alter = mod (pc - kNaturalPc[letter] + 6, 12) - 6;
        std::string n (1, "CDEFGAB"[letter]);
        n.append (static_cast<std::size_t> (std::abs (alter)), alter > 0 ? '#' : 'b');
        out.push_back (n);
    }
    return out;
}

// ---- key finding ------------------------------------------------------------------------------

std::array<double, 12> pitchWeights (const std::vector<Note>& notes, Tick from, Tick to)
{
    std::array<double, 12> w {};
    for (const auto& n : notes)
    {
        const Tick a = std::max (from, n.start), b = std::min (to, n.end());
        if (b > a)
            w[static_cast<std::size_t> (n.pitch % 12)] += static_cast<double> (b - a) * (0.6 + 0.4 * n.velocity / 127.0);
    }
    return w;
}

std::vector<KeyGuess> rankKeys (std::span<const double, 12> weights)
{
    // Krumhansl and Kessler's probe-tone ratings (1982), tonic first.
    static constexpr double kMajor[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static constexpr double kMinor[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    const auto correlate = [&] (const double* profile, int tonic) {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i)
        {
            mx += weights[static_cast<std::size_t> ((tonic + i) % 12)];
            my += profile[i];
        }
        mx /= 12.0;
        my /= 12.0;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double x = weights[static_cast<std::size_t> ((tonic + i) % 12)] - mx, y = profile[i] - my;
            sxy += x * y;
            sxx += x * x;
            syy += y * y;
        }
        return sxx > 0 && syy > 0 ? sxy / std::sqrt (sxx * syy) : 0.0;
    };
    std::vector<KeyGuess> out;
    for (int t = 0; t < 12; ++t)
    {
        out.push_back ({ makeKey (t, Mode::major), correlate (kMajor, t) });
        out.push_back ({ makeKey (t, Mode::minor), correlate (kMinor, t) });
    }
    std::stable_sort (out.begin(), out.end(), [] (const KeyGuess& a, const KeyGuess& b) { return a.score > b.score; });
    return out;
}

namespace
{
    bool enoughNotes (const std::array<double, 12>& w)
    {
        return std::count_if (w.begin(), w.end(), [] (double v) { return v > 0; }) >= 3;
    }
}

std::optional<KeyGuess> findKey (const std::vector<Note>& notes, Tick from, Tick to)
{
    const auto w = pitchWeights (notes, from, to);
    if (! enoughNotes (w))
        return {};
    return rankKeys (w).front();
}

double certainty (const std::vector<KeyGuess>& ranked)
{
    if (ranked.size() < 2)
        return 0.0;
    const double lead = std::clamp ((ranked[0].score - ranked[1].score) / 0.2, 0.0, 1.0);
    return std::clamp (ranked[0].score, 0.0, 1.0) * (0.5 + 0.5 * lead);
}

std::vector<KeyRegion> keyRegions (const std::vector<Note>& notes, Tick from, Tick to, Tick bar, int windowBars)
{
    std::vector<KeyRegion> out;
    if (bar <= 0 || to <= from)
        return out;
    windowBars = std::max (1, windowBars);
    const int before = (windowBars - 1) / 2;
    std::vector<std::pair<Tick, std::optional<KeyGuess>>> perBar;
    std::optional<Key> last;
    for (Tick at = from; at < to; at += bar)
    {
        // The window stays inside the range, so the first and last bars get a whole window too.
        const Tick a = std::clamp (at - before * bar, from, std::max (from, to - windowBars * bar));
        const Tick b = std::min (to, a + windowBars * bar);
        const auto w = pitchWeights (notes, a, b);
        if (! enoughNotes (w))
        {
            perBar.push_back ({ at, std::nullopt });
            continue;
        }
        const auto ranked = rankKeys (w);
        KeyGuess pick = ranked.front();
        // Stay in the last key while it is nearly as good: a change must be clear.
        if (last)
            for (const auto& g : ranked)
                if (g.key == *last)
                {
                    if (g.score >= pick.score - 0.08)
                        pick = g;
                    break;
                }
        last = pick.key;
        perBar.push_back ({ at, pick });
    }
    for (std::size_t i = 0; i < perBar.size(); ++i)
    {
        const auto& [at, guess] = perBar[i];
        const Tick end = std::min (to, at + bar);
        if (! guess)
        {
            if (! out.empty())
                out.back().end = end; // a silent bar keeps the key
            continue;
        }
        if (! out.empty() && out.back().key == guess->key)
        {
            out.back().end = end;
            out.back().score = std::max (out.back().score, guess->score);
            continue;
        }
        out.push_back ({ at, end, guess->key, guess->score });
    }
    // A key that lasts half a window or less is no change of key: its bars go to its neighbours.
    const Tick shortest = std::max<Tick> (bar, bar * (windowBars / 2));
    for (std::size_t i = 0; i < out.size() && out.size() > 1;)
    {
        if (out[i].end - out[i].start > shortest)
        {
            ++i;
            continue;
        }
        if (i == 0)
            out[1].start = out[0].start;
        else if (i + 1 == out.size())
            out[i - 1].end = out[i].end;
        else
        {
            const Tick mid = out[i].start + ((out[i].end - out[i].start) / 2 / bar) * bar;
            out[i - 1].end = mid;
            out[i + 1].start = mid;
        }
        out.erase (out.begin() + static_cast<std::ptrdiff_t> (i));
        if (i > 0 && i < out.size() && out[i].key == out[i - 1].key)
        {
            out[i - 1].end = out[i].end;
            out.erase (out.begin() + static_cast<std::ptrdiff_t> (i));
        }
        i = 0;
    }
    return out;
}

// ---- roman numerals ---------------------------------------------------------------------------

namespace
{
    enum class Triad : std::uint8_t { major, minor, diminished, augmented, other };
    enum class Seventh : std::uint8_t { none, dominant, major, minor, halfDiminished, diminished, minorMajor };

    struct Quality
    {
        Triad triad = Triad::major;
        Seventh seventh = Seventh::none;
        const char* extra = ""; ///< a suffix for chords that are no plain triad or seventh chord
        bool upper = true;      ///< an upper-case numeral
    };

    Quality quality (ChordType t)
    {
        using C = ChordType;
        switch (t)
        {
            case C::major: return { Triad::major, Seventh::none, "", true };
            case C::major6: return { Triad::other, Seventh::none, "add6", true };
            case C::major7: return { Triad::major, Seventh::major, "", true };
            case C::major7Sharp11: return { Triad::other, Seventh::none, "maj7#11", true };
            case C::majorAdd9: return { Triad::other, Seventh::none, "add9", true };
            case C::major7_9: return { Triad::other, Seventh::none, "maj9", true };
            case C::major6_9: return { Triad::other, Seventh::none, "69", true };
            case C::augmented: return { Triad::augmented, Seventh::none, "", true };
            case C::minor: return { Triad::minor, Seventh::none, "", false };
            case C::minor6: return { Triad::other, Seventh::none, "add6", false };
            case C::minor7: return { Triad::minor, Seventh::minor, "", false };
            case C::minor7Flat5: return { Triad::diminished, Seventh::halfDiminished, "", false };
            case C::minorAdd9: return { Triad::other, Seventh::none, "add9", false };
            case C::minor7_9: return { Triad::other, Seventh::none, "9", false };
            case C::minor7_11: return { Triad::other, Seventh::none, "11", false };
            case C::minorMajor7: return { Triad::minor, Seventh::minorMajor, "", false };
            case C::minorMajor7_9: return { Triad::other, Seventh::none, "maj9", false };
            case C::diminished: return { Triad::diminished, Seventh::none, "", false };
            case C::diminished7: return { Triad::diminished, Seventh::diminished, "", false };
            case C::seventh: return { Triad::major, Seventh::dominant, "", true };
            case C::seventhSus4: return { Triad::other, Seventh::none, "7sus4", true };
            case C::seventhFlat5: return { Triad::other, Seventh::none, "7b5", true };
            case C::seventh9: return { Triad::other, Seventh::none, "9", true };
            case C::seventhSharp11: return { Triad::other, Seventh::none, "7#11", true };
            case C::seventh13: return { Triad::other, Seventh::none, "13", true };
            case C::seventhFlat9: return { Triad::other, Seventh::none, "7b9", true };
            case C::seventhFlat13: return { Triad::other, Seventh::none, "7b13", true };
            case C::seventhSharp9: return { Triad::other, Seventh::none, "7#9", true };
            case C::major7Augmented: return { Triad::augmented, Seventh::major, "", true };
            case C::seventhAugmented: return { Triad::augmented, Seventh::dominant, "", true };
            case C::octave: return { Triad::other, Seventh::none, "(8)", true };
            case C::fifth: return { Triad::other, Seventh::none, "5", true };
            case C::sus4: return { Triad::other, Seventh::none, "sus4", true };
            case C::sus2: return { Triad::other, Seventh::none, "sus2", true };
            case C::cancel: break;
        }
        return { Triad::other, Seventh::none, "", true };
    }

    /** True for chords that lead to a chord a fifth below: major triads and dominant sevenths and ninths. */
    bool dominantLike (ChordType t)
    {
        using C = ChordType;
        return t == C::major || t == C::seventh || t == C::seventh9 || t == C::seventhFlat9 || t == C::seventhSharp9 || t == C::seventh13
               || t == C::seventhFlat13 || t == C::seventhSus4;
    }

    bool leadingToneLike (ChordType t) { return t == ChordType::diminished || t == ChordType::diminished7 || t == ChordType::minor7Flat5; }

    constexpr const char* kNumerals[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };

    std::string numeral (int degree, bool upper)
    {
        std::string n = kNumerals[degree];
        if (! upper)
            for (auto& c : n)
                c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return n;
    }

    /** The degree (0 .. 6) of a note `r` semitones above the tonic, and its accidental (-1, 0, 1). */
    std::pair<int, int> degreeOf (int r, const Key& k, bool upper)
    {
        r = mod (r, 12);
        if (majorLike (k.mode))
        {
            static constexpr std::pair<int, int> kMajor[12] = { { 0, 0 }, { 1, -1 }, { 1, 0 }, { 2, -1 }, { 2, 0 }, { 3, 0 },
                                                                { 3, 1 }, { 4, 0 }, { 5, -1 }, { 5, 0 }, { 6, -1 }, { 6, 0 } };
            return kMajor[r];
        }
        static constexpr std::pair<int, int> kMinor[12] = { { 0, 0 }, { 1, -1 }, { 1, 0 }, { 2, 0 }, { 2, 1 }, { 3, 0 },
                                                            { 3, 1 }, { 4, 0 }, { 5, 0 }, { 5, 1 }, { 6, 0 }, { 6, 1 } };
        // In minor, "vii" is the leading-note chord and "VII" the chord a whole tone below the tonic.
        if (r == 11 && ! upper)
            return { 6, 0 };
        if (r == 10 && ! upper)
            return { 6, -1 };
        return kMinor[r];
    }

    /** Semitones above the tonic for a degree written with an accidental. The inverse of degreeOf. */
    int semitonesOf (int degree, int accidental, const Key& k, bool upper)
    {
        static constexpr int kMajor[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static constexpr int kMinor[7] = { 0, 2, 3, 5, 7, 8, 10 };
        if (majorLike (k.mode))
            return kMajor[degree] + accidental;
        if (degree == 6 && ! upper)
            return 11 + (accidental < 0 ? -1 : 0); // vii = leading note, bvii = a tone below
        if (degree == 6 && upper && accidental > 0)
            return 11;
        return kMinor[degree] + accidental;
    }

    std::string accidentalText (int a) { return a < 0 ? "b" : (a > 0 ? "#" : ""); }

    std::string figure (const Chord& c, const Quality& q)
    {
        const auto tones = style_chord::tones (c.type);
        const int rel = c.bass >= 0 ? mod (c.bass - c.root, 12) : 0;
        int inversion = 0;
        for (std::size_t i = 1; i < tones.size() && i < 4; ++i)
            if (tones[i] % 12 == rel)
                inversion = static_cast<int> (i);
        std::string symbol;
        if (q.triad == Triad::diminished && q.seventh != Seventh::halfDiminished)
            symbol = "\xc2\xb0"; // °
        else if (q.seventh == Seventh::halfDiminished)
            symbol = "\xc3\xb8"; // ø
        else if (q.triad == Triad::augmented)
            symbol = "+";
        if (q.triad == Triad::other)
            return q.extra;
        if (q.seventh == Seventh::none)
        {
            static const char* kTriad[3] = { "", "6", "64" };
            return symbol + kTriad[std::min (inversion, 2)];
        }
        static const char* kSeventh[4] = { "7", "65", "43", "42" };
        const std::string maj = q.seventh == Seventh::major || q.seventh == Seventh::minorMajor ? "maj" : "";
        return symbol + maj + kSeventh[std::min (inversion, 3)];
    }

    std::vector<int> scaleSet (const Key& k)
    {
        const auto steps = modeSteps (k.mode);
        std::vector<int> set (steps.begin(), steps.end());
        if (k.mode == Mode::minor)
        {
            set.push_back (9);  // melodic minor
            set.push_back (11); // harmonic minor
        }
        for (auto& s : set)
            s = mod (s + k.tonic, 12);
        return set;
    }

    bool diatonic (const Chord& c, const Key& k)
    {
        const auto set = scaleSet (k);
        for (const int t : style_chord::tones (c.type))
            if (std::find (set.begin(), set.end(), mod (c.root + t, 12)) == set.end())
                return false;
        return true;
    }

    /** Upper case when the triad on a degree of the key is major (in minor, V is major). Empty for a diminished triad. */
    std::optional<bool> degreeCase (int degree, const Key& k)
    {
        const auto steps = modeSteps (k.mode);
        if (k.mode == Mode::minor && degree == 4)
            return true;
        const int third = mod (steps[static_cast<std::size_t> ((degree + 2) % 7)] - steps[static_cast<std::size_t> (degree)], 12);
        const int fifth = mod (steps[static_cast<std::size_t> ((degree + 4) % 7)] - steps[static_cast<std::size_t> (degree)], 12);
        if (fifth != 7)
            return std::nullopt;
        return third == 4;
    }

    std::string plainRoman (const Chord& c, const Key& k)
    {
        const auto q = quality (c.type);
        const auto [degree, acc] = degreeOf (c.root - k.tonic, k, q.upper);
        return accidentalText (acc) + numeral (degree, q.upper) + figure (c, q);
    }

    std::optional<ChordType> typeFor (const std::string& symbol, const std::string& rest, bool upper, int& inversion)
    {
        using C = ChordType;
        inversion = 0;
        const bool dim = symbol == "dim", half = symbol == "half", aug = symbol == "aug";
        static const std::pair<const char*, int> kTriadFig[] = { { "", 0 }, { "6", 1 }, { "64", 2 } };
        for (const auto& [f, inv] : kTriadFig)
            if (rest == f)
            {
                inversion = inv;
                if (half)
                    return std::nullopt;
                if (dim) return C::diminished;
                if (aug) return C::augmented;
                return upper ? C::major : C::minor;
            }
        static const std::pair<const char*, int> kSeventhFig[] = { { "7", 0 }, { "65", 1 }, { "43", 2 }, { "42", 3 }, { "2", 3 } };
        for (const auto& [f, inv] : kSeventhFig)
        {
            inversion = inv;
            if (rest == f)
            {
                if (dim) return C::diminished7;
                if (half) return C::minor7Flat5;
                if (aug) return C::seventhAugmented;
                return upper ? C::seventh : C::minor7;
            }
            for (const char* maj : { "maj", "M" })
                if (rest == std::string (maj) + f)
                {
                    if (aug) return C::major7Augmented;
                    if (dim || half) return std::nullopt;
                    return upper ? C::major7 : C::minorMajor7;
                }
        }
        inversion = 0;
        if (! symbol.empty())
            return std::nullopt;
        static const std::tuple<const char*, C, C> kExtra[] = {
            { "add6", C::major6, C::minor6 },        { "add9", C::majorAdd9, C::minorAdd9 },      { "9", C::seventh9, C::minor7_9 },
            { "11", C::minor7_11, C::minor7_11 },    { "13", C::seventh13, C::seventh13 },         { "maj9", C::major7_9, C::minorMajor7_9 },
            { "69", C::major6_9, C::major6_9 },      { "sus4", C::sus4, C::sus4 },                 { "sus", C::sus4, C::sus4 },
            { "sus2", C::sus2, C::sus2 },            { "7sus4", C::seventhSus4, C::seventhSus4 }, { "7b9", C::seventhFlat9, C::seventhFlat9 },
            { "7#9", C::seventhSharp9, C::seventhSharp9 }, { "7b5", C::seventhFlat5, C::seventhFlat5 }, { "7#11", C::seventhSharp11, C::seventhSharp11 },
            { "7b13", C::seventhFlat13, C::seventhFlat13 }, { "maj7#11", C::major7Sharp11, C::major7Sharp11 }, { "5", C::fifth, C::fifth },
        };
        for (const auto& [f, up, low] : kExtra)
            if (rest == f)
                return upper ? up : low;
        return std::nullopt;
    }
}

std::string roman (const Chord& c, const Key& k)
{
    if (c.type == ChordType::cancel)
        return "-";
    const int r = mod (c.root - k.tonic, 12);
    if (! diatonic (c, k))
    {
        // A secondary dominant: a dominant (or leading-note) chord of a chord of the key other than the tonic.
        const bool dom = dominantLike (c.type), lead = leadingToneLike (c.type);
        if (dom || lead)
        {
            const int target = mod (dom ? c.root + 5 : c.root + 1, 12);
            const auto [degree, acc] = degreeOf (target - k.tonic, k, true);
            if (acc == 0 && degree != 0)
                if (const auto upper = degreeCase (degree, k))
                {
                    const Key local = makeKey (target, *upper ? Mode::major : Mode::minor);
                    return plainRoman (c, local) + "/" + numeral (degree, *upper);
                }
        }
        // The Neapolitan sixth: a major chord on the lowered second degree with its third in the bass.
        if (r == 1 && c.type == ChordType::major && c.bass >= 0 && mod (c.bass - c.root, 12) == 4)
            return "N6";
    }
    return plainRoman (c, k);
}

std::optional<Chord> fromRoman (std::string_view textIn, const Key& k)
{
    std::string text = trim (std::string (textIn));
    if (text.empty())
        return {};
    // A secondary chord: "V7/V" is the V7 of the key of V.
    if (const auto slash = text.find ('/'); slash != std::string::npos)
    {
        const auto target = fromRoman (text.substr (slash + 1), k);
        if (! target || target->type == ChordType::cancel)
            return {};
        const auto q = quality (target->type);
        const Key local = makeKey (target->root, q.upper ? Mode::major : Mode::minor);
        return fromRoman (text.substr (0, slash), local);
    }
    if (text == "N" || text == "N6")
    {
        Chord c { mod (k.tonic + 1, 12), ChordType::major, -1 };
        if (text == "N6")
            c.bass = mod (c.root + 4, 12);
        return c;
    }
    std::size_t i = 0;
    int acc = 0;
    while (i < text.size())
    {
        if (text[i] == 'b')
            --acc, ++i;
        else if (text[i] == '#')
            ++acc, ++i;
        else if (text.compare (i, 3, "\xe2\x99\xad") == 0)
            --acc, i += 3;
        else if (text.compare (i, 3, "\xe2\x99\xaf") == 0)
            ++acc, i += 3;
        else
            break;
    }
    if (std::abs (acc) > 1)
        return {};
    // The longest numeral that matches, all upper or all lower case.
    int degree = -1;
    bool upper = true;
    std::size_t used = 0;
    for (int d = 0; d < 7; ++d)
        for (const bool up : { true, false })
        {
            const auto n = numeral (d, up);
            if (text.compare (i, n.size(), n) == 0 && n.size() > used)
            {
                degree = d;
                upper = up;
                used = n.size();
            }
        }
    if (degree < 0)
        return {};
    i += used;
    std::string symbol;
    if (text.compare (i, 2, "\xc2\xb0") == 0)
        symbol = "dim", i += 2;
    else if (text.compare (i, 2, "\xc3\xb8") == 0)
        symbol = "half", i += 2;
    else if (i < text.size() && text[i] == 'o')
        symbol = "dim", ++i;
    else if (i < text.size() && text[i] == '+')
        symbol = "aug", ++i;
    int inversion = 0;
    const auto type = typeFor (symbol, text.substr (i), upper, inversion);
    if (! type)
        return {};
    Chord c;
    c.root = mod (k.tonic + semitonesOf (degree, acc, k, upper || symbol == "aug"), 12);
    c.type = *type;
    if (inversion > 0)
    {
        const auto tones = style_chord::tones (c.type);
        if (static_cast<std::size_t> (inversion) < tones.size())
            c.bass = mod (c.root + tones[static_cast<std::size_t> (inversion)], 12);
    }
    return c;
}

std::vector<Chord> progression (std::string_view text, const Key& k)
{
    std::vector<Chord> out;
    std::string word;
    const auto flush = [&] {
        if (auto c = fromRoman (word, k))
            out.push_back (*c);
        word.clear();
    };
    for (const char ch : text)
    {
        if (std::isspace (static_cast<unsigned char> (ch)) || ch == ',' || ch == '-' || ch == '|')
            flush();
        else
            word += ch;
    }
    flush();
    return out;
}

std::vector<Chord> diatonicChords (const Key& k, bool sevenths)
{
    auto steps = modeSteps (k.mode);
    std::vector<Chord> out;
    for (int d = 0; d < 7; ++d)
    {
        auto at = [&] (int n) {
            auto s = steps;
            if (k.mode == Mode::minor && d == 4)
                s[6] = 11; // V in minor takes the leading note
            return mod (s[static_cast<std::size_t> ((d + n) % 7)] - s[static_cast<std::size_t> (d)], 12);
        };
        const int third = at (2), fifth = at (4), seventh = at (6);
        using C = ChordType;
        C type = C::major;
        if (! sevenths)
            type = third == 4 ? (fifth == 8 ? C::augmented : C::major) : (fifth == 6 ? C::diminished : C::minor);
        else if (third == 4 && fifth == 7)
            type = seventh == 11 ? C::major7 : C::seventh;
        else if (third == 3 && fifth == 7)
            type = seventh == 11 ? C::minorMajor7 : C::minor7;
        else if (third == 3 && fifth == 6)
            type = seventh == 9 ? C::diminished7 : C::minor7Flat5;
        else
            type = C::major7Augmented;
        out.push_back ({ mod (k.tonic + steps[static_cast<std::size_t> (d)], 12), type, -1 });
    }
    return out;
}

// ---- voice leading ----------------------------------------------------------------------------

std::vector<Voice> splitVoices (const std::string& trackName, const std::vector<Note>& notesIn, int maxVoices)
{
    auto notes = notesIn;
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch > b.pitch; });
    std::vector<std::vector<Note>> groups;
    for (const auto& n : notes)
    {
        if (groups.empty() || groups.back().front().start != n.start)
            groups.emplace_back();
        groups.back().push_back (n);
    }
    std::map<int, int> sizes;
    for (const auto& g : groups)
        ++sizes[static_cast<int> (g.size())];
    int count = 1, best = 0;
    for (const auto& [size, times] : sizes)
        if (times > best || (times == best && size > count))
        {
            best = times;
            count = size;
        }
    count = std::clamp (count, 1, std::max (1, maxVoices));
    std::vector<Voice> voices (static_cast<std::size_t> (count));
    for (int v = 0; v < count; ++v)
        voices[static_cast<std::size_t> (v)].name = count == 1 ? trackName : trackName + " " + std::to_string (v + 1);
    for (const auto& g : groups)
    {
        const int k = static_cast<int> (g.size());
        const auto put = [&] (int voice, const Note& n) { voices[static_cast<std::size_t> (voice)].notes.push_back (n); };
        put (0, g.front());
        if (count == 1 || k == 1)
            continue;
        put (count - 1, g.back());
        for (int i = 1; i < k - 1 && i < count - 1; ++i)
            put (i, g[static_cast<std::size_t> (i)]);
    }
    for (auto& v : voices)
        for (std::size_t i = 0; i + 1 < v.notes.size(); ++i)
            v.notes[i].length = std::min (v.notes[i].length, v.notes[i + 1].start - v.notes[i].start);
    return voices;
}

std::string name (VoiceIssue::Kind k)
{
    switch (k)
    {
        case VoiceIssue::Kind::parallelFifths: return "parallel fifths";
        case VoiceIssue::Kind::parallelOctaves: return "parallel octaves";
        case VoiceIssue::Kind::hiddenFifths: return "hidden fifths";
        case VoiceIssue::Kind::hiddenOctaves: return "hidden octaves";
        case VoiceIssue::Kind::voiceCrossing: return "voice crossing";
        case VoiceIssue::Kind::largeLeap: return "large leap";
    }
    return {};
}

std::vector<VoiceIssue> checkVoiceLeading (const std::vector<Voice>& voices, Tick from, Tick to, std::size_t limit)
{
    std::vector<VoiceIssue> out;
    const auto n = voices.size();
    if (n == 0)
        return out;

    std::vector<Tick> times;
    std::vector<double> mean (n, 0.0);
    for (std::size_t v = 0; v < n; ++v)
    {
        double sum = 0;
        int count = 0;
        for (const auto& note : voices[v].notes)
        {
            if (note.end() <= from || note.start >= to)
                continue;
            if (note.start >= from)
                times.push_back (note.start);
            sum += note.pitch;
            ++count;
        }
        mean[v] = count > 0 ? sum / count : -1.0;
    }
    std::sort (times.begin(), times.end());
    times.erase (std::unique (times.begin(), times.end()), times.end());

    const auto pitchAt = [&] (std::size_t v, Tick t) {
        const auto& ns = voices[v].notes;
        auto it = std::upper_bound (ns.begin(), ns.end(), t, [] (Tick x, const Note& note) { return x < note.start; });
        if (it == ns.begin())
            return -1;
        --it;
        return t < it->end() ? static_cast<int> (it->pitch) : -1;
    };

    // Voices from high to low, by their average pitch.
    std::vector<std::size_t> order;
    for (std::size_t v = 0; v < n; ++v)
        if (mean[v] >= 0)
            order.push_back (v);
    std::stable_sort (order.begin(), order.end(), [&] (std::size_t a, std::size_t b) { return mean[a] > mean[b]; });
    if (order.size() >= 2)
    {
        const auto top = order.front(), bottom = order.back();
        const auto label = [&] (std::size_t a, std::size_t b) { return voices[a].name + " and " + voices[b].name; };
        for (std::size_t i = 1; i < times.size(); ++i)
        {
            const Tick t0 = times[i - 1], t1 = times[i];
            for (std::size_t x = 0; x < order.size(); ++x)
                for (std::size_t y = x + 1; y < order.size(); ++y)
                {
                    const auto a = order[x], b = order[y];
                    const int a0 = pitchAt (a, t0), a1 = pitchAt (a, t1), b0 = pitchAt (b, t0), b1 = pitchAt (b, t1);
                    if (a0 < 0 || a1 < 0 || b0 < 0 || b1 < 0)
                        continue;
                    const int da = a1 - a0, db = b1 - b0;
                    if (da == 0 && db == 0)
                        continue;
                    const int i0 = a0 - b0, i1 = a1 - b1;
                    const auto add = [&] (VoiceIssue::Kind kind) {
                        out.push_back ({ kind, t1, static_cast<int> (a), static_cast<int> (b), name (kind) + " between " + label (a, b) });
                    };
                    if (i0 >= 0 && i1 < 0)
                    {
                        add (VoiceIssue::Kind::voiceCrossing);
                        continue;
                    }
                    if (i0 < 0 || i1 < 0)
                        continue;
                    const bool similar = (da > 0 && db > 0) || (da < 0 && db < 0);
                    if (! similar)
                        continue;
                    const int c0 = i0 % 12, c1 = i1 % 12;
                    if (c0 == 7 && c1 == 7)
                        add (VoiceIssue::Kind::parallelFifths);
                    else if (c0 == 0 && c1 == 0)
                        add (VoiceIssue::Kind::parallelOctaves);
                    else if (a == top && b == bottom && std::abs (da) > 2 && (c1 == 7 || c1 == 0))
                        add (c1 == 7 ? VoiceIssue::Kind::hiddenFifths : VoiceIssue::Kind::hiddenOctaves);
                }
        }
    }
    for (std::size_t v = 0; v < n; ++v)
    {
        const Note* last = nullptr;
        for (const auto& note : voices[v].notes)
        {
            if (note.start >= to)
                break;
            if (note.start >= from && last != nullptr && std::abs (static_cast<int> (note.pitch) - static_cast<int> (last->pitch)) > 12)
                out.push_back ({ VoiceIssue::Kind::largeLeap, note.start, static_cast<int> (v), static_cast<int> (v),
                                 "a leap of more than an octave in " + voices[v].name });
            last = &note;
        }
    }
    std::stable_sort (out.begin(), out.end(), [] (const VoiceIssue& a, const VoiceIssue& b) { return a.at < b.at; });
    if (out.size() > limit)
        out.resize (limit);
    return out;
}

} // namespace arranger::theory

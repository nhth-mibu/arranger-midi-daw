#include "arranger/core/StyleChord.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>

namespace arranger::style_chord
{

namespace
{
    struct TypeInfo
    {
        const char* suffix;
        int third, fifth, seventh;   ///< semitones above the root; -1 = the chord has none
        std::array<int, 7> scale;    ///< the seven notes a melody uses over this chord
    };

    constexpr std::array<int, 7> kMajor { 0, 2, 4, 5, 7, 9, 11 }, kLydian { 0, 2, 4, 6, 7, 9, 11 }, kMixo { 0, 2, 4, 5, 7, 9, 10 },
        kDorian { 0, 2, 3, 5, 7, 9, 10 }, kMelodicMinor { 0, 2, 3, 5, 7, 9, 11 }, kLocrian { 0, 1, 3, 5, 6, 8, 10 },
        kDim { 0, 2, 3, 5, 6, 8, 9 }, kAug { 0, 2, 4, 5, 8, 9, 11 }, kAug7 { 0, 2, 4, 5, 8, 9, 10 };

    constexpr TypeInfo kTypes[kChordTypeCount] = {
        { "", 4, 7, -1, kMajor },        { "6", 4, 7, 9, kMajor },         { "M7", 4, 7, 11, kMajor },      { "M7#11", 4, 7, 11, kLydian },
        { "add9", 4, 7, -1, kMajor },    { "M7(9)", 4, 7, 11, kMajor },    { "6(9)", 4, 7, 9, kMajor },     { "aug", 4, 8, -1, kAug },
        { "m", 3, 7, -1, kDorian },      { "m6", 3, 7, 9, kDorian },       { "m7", 3, 7, 10, kDorian },     { "m7b5", 3, 6, 10, kLocrian },
        { "m(9)", 3, 7, -1, kDorian },   { "m7(9)", 3, 7, 10, kDorian },   { "m7(11)", 3, 7, 10, kDorian }, { "mM7", 3, 7, 11, kMelodicMinor },
        { "mM7(9)", 3, 7, 11, kMelodicMinor },
        { "dim", 3, 6, -1, kDim },       { "dim7", 3, 6, 9, kDim },
        { "7", 4, 7, 10, kMixo },        { "7sus4", 5, 7, 10, kMixo },     { "7b5", 4, 6, 10, { 0, 2, 4, 5, 6, 9, 10 } },
        { "7(9)", 4, 7, 10, kMixo },     { "7#11", 4, 7, 10, { 0, 2, 4, 6, 7, 9, 10 } },                   { "7(13)", 4, 7, 10, kMixo },
        { "7(b9)", 4, 7, 10, { 0, 1, 4, 5, 7, 8, 10 } },                  { "7(b13)", 4, 7, 10, { 0, 2, 4, 5, 7, 8, 10 } },
        { "7(#9)", 4, 7, 10, { 0, 3, 4, 5, 7, 9, 10 } },
        { "M7aug", 4, 8, 11, kAug },     { "7aug", 4, 8, 10, kAug7 },
        { "1+8", 0, 0, -1, kMajor },     { "1+5", 7, 7, -1, kMajor },      { "sus4", 5, 7, -1, kMixo },     { "sus2", 2, 7, -1, kMajor },
        { "", 4, 7, -1, kMajor },
    };

    constexpr const char* kNoteNames[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

    const TypeInfo& info (int type) { return kTypes[std::clamp (type, 0, kChordTypeCount - 1)]; }
    int mod12 (int v) { return ((v % 12) + 12) % 12; }

    /** Where a source interval goes in the target chord, treating both as root/third/fifth/seventh. */
    std::optional<int> chordToneFor (int rel, const TypeInfo& from, const TypeInfo& to)
    {
        if (rel == 0)
            return 0;
        if (rel == from.third)
            return to.third;
        if (rel == from.fifth)
            return to.fifth;
        if (from.seventh >= 0 && rel == from.seventh)
            return to.seventh >= 0 ? to.seventh : 12; // a triad has no seventh: use the root above
        return {};
    }

    /** The same step of the scale in the target, keeping a chromatic note chromatic. */
    int scaleToneFor (int rel, const TypeInfo& from, const TypeInfo& to)
    {
        int degree = 0;
        for (int i = 0; i < 7; ++i)
            if (from.scale[static_cast<std::size_t> (i)] <= rel)
                degree = i;
        const int offset = rel - from.scale[static_cast<std::size_t> (degree)];
        return to.scale[static_cast<std::size_t> (degree)] + offset;
    }
}

std::string name (const Chord& c)
{
    if (c.type == ChordType::cancel)
        return "-";
    std::string s = std::string (kNoteNames[mod12 (c.root)]) + info (static_cast<int> (c.type)).suffix;
    if (c.bass >= 0 && mod12 (c.bass) != mod12 (c.root))
        s += std::string ("/") + kNoteNames[mod12 (c.bass)];
    return s;
}

std::vector<int> tones (ChordType type)
{
    const auto& t = info (static_cast<int> (type));
    std::vector<int> out { 0 };
    for (const int v : { t.third, t.fifth, t.seventh })
        if (v > 0 && std::find (out.begin(), out.end(), v) == out.end())
            out.push_back (v);
    // The added notes that give some types their name.
    switch (type)
    {
        case ChordType::majorAdd9: case ChordType::major7_9: case ChordType::major6_9: case ChordType::minorAdd9:
        case ChordType::minor7_9: case ChordType::minorMajor7_9: case ChordType::seventh9: out.push_back (14); break;
        case ChordType::major7Sharp11: case ChordType::seventhSharp11: out.push_back (18); break;
        case ChordType::minor7_11: out.push_back (17); break;
        case ChordType::seventh13: out.push_back (21); break;
        case ChordType::seventhFlat9: out.push_back (13); break;
        case ChordType::seventhFlat13: out.push_back (20); break;
        case ChordType::seventhSharp9: out.push_back (15); break;
        case ChordType::octave: out.push_back (12); break;
        default: break;
    }
    std::sort (out.begin(), out.end());
    return out;
}

std::optional<Chord> detect (std::span<const int> pitches)
{
    if (pitches.empty())
        return {};
    unsigned held = 0;
    int lowest = 127;
    for (const int p : pitches)
    {
        held |= 1u << mod12 (p);
        lowest = std::min (lowest, p);
    }
    const int bass = mod12 (lowest);
    const auto rotate = [] (unsigned set, int root) { return ((set >> root) | (set << (12 - root))) & 0x0fffu; };
    const int count = static_cast<int> (std::popcount (held));

    if (count == 1)
        return Chord { bass, ChordType::octave, -1 };

    std::optional<Chord> best;
    int bestScore = -1;
    for (int root = 0; root < 12; ++root)
    {
        if ((held & (1u << root)) == 0)
            continue;
        const unsigned rel = rotate (held, root);
        for (int type = 0; type < kChordTypeCount - 1; ++type)
        {
            if (type == static_cast<int> (ChordType::octave))
                continue;
            unsigned full = 0;
            for (const int t : tones (static_cast<ChordType> (type)))
                full |= 1u << mod12 (t);
            const auto& ti = info (type);
            // A chord with four or more notes can leave out its fifth.
            const unsigned noFifth = std::popcount (full) >= 4 && ti.fifth == 7 ? full & ~(1u << 7) : full;
            int score = -1;
            if (rel == full)
                score = 20;
            else if (rel == noFifth)
                score = 10;
            if (score < 0)
                continue;
            if (root == bass)
                score += 5; // the lowest key is the root: the plain reading wins
            if (score > bestScore)
            {
                bestScore = score;
                best = Chord { root, static_cast<ChordType> (type), root == bass ? -1 : bass };
            }
        }
    }
    return best;
}

std::optional<Chord> guess (std::span<const double, 12> weight, int bass)
{
    double total = 0.0;
    int sounding = 0;
    for (const double w : weight)
    {
        total += w;
        sounding += w > 0.0 ? 1 : 0;
    }
    if (sounding < 2 || total <= 0.0)
        return {};

    // The types a style is usually written in, the plain ones first: they win a tie.
    static constexpr ChordType kTry[] = { ChordType::major, ChordType::minor, ChordType::major7, ChordType::minor7, ChordType::seventh,
                                          ChordType::major6, ChordType::minor6, ChordType::sus4, ChordType::diminished, ChordType::augmented,
                                          ChordType::minor7Flat5, ChordType::diminished7, ChordType::minorMajor7 };
    std::optional<Chord> best;
    double bestScore = -1.0e9;
    for (int root = 0; root < 12; ++root)
        for (const auto type : kTry)
        {
            const auto& ti = info (static_cast<int> (type));
            bool tone[12] = {}, scale[12] = {};
            for (const int t : tones (type))
                tone[mod12 (root + t)] = true;
            for (const int t : ti.scale)
                scale[mod12 (root + t)] = true;
            // A chord with fewer notes wins when two chords fit the same notes (Am before C6).
            double score = -0.02 * static_cast<double> (tones (type).size());
            bool complete = true;
            for (int n = 0; n < 12; ++n)
            {
                const double w = weight[static_cast<std::size_t> (n)] / total;
                if (tone[n])
                {
                    score += w;
                    // The fifth can be missing; the other notes must sound.
                    if (w <= 0.0)
                    {
                        if (mod12 (n - root) != ti.fifth)
                            complete = false;
                        score -= 0.05;
                    }
                }
                else
                    score -= scale[n] ? 0.25 * w : w; // passing notes cost little, wrong notes cost more
            }
            if (! complete)
                continue;
            if (root == bass)
                score += 0.2;
            if (score > bestScore + 1e-9)
            {
                bestScore = score;
                best = Chord { root, type, -1 };
            }
        }
    return best;
}

bool plays (const StyleChannelRule& rule, const Chord& chord)
{
    if (chord.type == ChordType::cancel)
        return true; // transpose() decides: rhythm goes on, the rest stops
    if (((rule.noteMute >> mod12 (chord.root)) & 1) == 0)
        return false;
    return ((rule.chordMute >> static_cast<int> (chord.type)) & 1) != 0;
}

std::optional<int> transpose (int pitch, const StyleChannelRule& rule, const Chord& chord)
{
    const auto& t = pitch < rule.middleLow ? rule.lowRange : (pitch > rule.middleHigh ? rule.highRange : rule.transpose);
    using Ntr = StyleTranspose::Ntr;
    using Ntt = StyleTranspose::Ntt;

    const bool fixedRoot = t.ntr != Ntr::rootTransposition;
    if (fixedRoot && t.ntt == Ntt::bypass)
        return pitch; // drums and effects: as recorded
    if (chord.type == ChordType::cancel)
        return {};

    const int targetRoot = mod12 (t.bass && chord.bass >= 0 ? chord.bass : chord.root);
    const auto& from = info (rule.sourceChordType);
    const auto& to = info (static_cast<int> (chord.type));
    const int rel = mod12 (pitch - rule.sourceRoot);
    const int base = pitch - rel; // the source root at or below the note

    // 1. The note's place in the new chord.
    int interval = rel;
    if (t.ntt == Ntt::chord)
        interval = chordToneFor (rel, from, to).value_or (scaleToneFor (rel, from, to));
    else if (t.ntt != Ntt::bypass)
    {
        // Melody: chord notes stay chord notes, the others follow the scale.
        const auto tone = chordToneFor (rel, from, to);
        interval = tone && *tone < 12 ? *tone : scaleToneFor (rel, from, to);
    }

    // 2. The root.
    int result = pitch;
    if (! fixedRoot)
    {
        int shift = mod12 (targetRoot - rule.sourceRoot);
        if (targetRoot > std::clamp (t.highKey, 0, 11))
            shift -= 12; // roots above the high key play an octave down
        result = base + interval + shift;
    }
    else
    {
        // Stay near the recorded pitch: take the nearest note with the right name.
        const int wanted = mod12 (targetRoot + interval);
        int up = pitch, down = pitch;
        while (mod12 (up) != wanted) ++up;
        while (mod12 (down) != wanted) --down;
        result = (up - pitch) < (pitch - down) ? up : down;
    }

    // 3. Keep inside the limits by octaves.
    if (t.lowLimit <= t.highLimit && t.highLimit - t.lowLimit >= 11)
    {
        while (result < t.lowLimit) result += 12;
        while (result > t.highLimit) result -= 12;
    }
    return std::clamp (result, 0, 127);
}

} // namespace arranger::style_chord

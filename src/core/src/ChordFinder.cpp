#include "arranger/core/ChordFinder.h"

#include "arranger/core/Notation.h"

#include <algorithm>
#include <limits>

namespace arranger::chords
{

namespace
{
    int mod12 (int n) noexcept { return ((n % 12) + 12) % 12; }

    /** True when two notes of the list sound at the same time somewhere between from and to. */
    bool playsChords (const std::vector<Note>& notes, Tick from, Tick to)
    {
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i];
            if (n.end() <= from || n.start >= to)
                continue;
            for (std::size_t j = i + 1; j < notes.size() && notes[j].start < n.end(); ++j)
                if (notes[j].start < to && notes[j].end() > from)
                    return true;
        }
        return false;
    }

    /** `parts`: the notes of each track, sorted by start. A part that plays one note at a time (a melody)
        counts less than a part that plays chords: its passing notes must not change the chord. */
    std::optional<Chord> chordOf (const std::vector<std::vector<Note>>& parts, Tick from, Tick to, double melodyWeight)
    {
        std::array<double, 12> weight {};
        int lowest = 128;
        bool anyChords = false;
        for (const auto& notes : parts)
            anyChords = anyChords || playsChords (notes, from, to);
        for (const auto& notes : parts)
        {
            const double partWeight = anyChords && ! playsChords (notes, from, to) ? melodyWeight : 1.0;
            for (const auto& n : notes)
            {
                const Tick a = std::max (from, n.start), b = std::min (to, n.end());
                if (b <= a)
                    continue;
                weight[static_cast<std::size_t> (n.pitch % 12)] += partWeight * static_cast<double> (b - a) * (0.5 + 0.5 * n.velocity / 127.0);
                if (partWeight == 1.0)
                    lowest = std::min (lowest, static_cast<int> (n.pitch));
            }
        }
        const int bass = lowest < 128 ? lowest % 12 : -1;
        auto chord = style_chord::guess (std::span<const double, 12> (weight), bass);
        if (chord && bass >= 0 && bass != chord->root)
        {
            const auto tones = style_chord::tones (chord->type);
            if (std::find (tones.begin(), tones.end(), mod12 (bass - chord->root)) != tones.end())
                chord->bass = bass;
        }
        return chord;
    }
}

std::vector<ScoreMark> find (const std::vector<const Track*>& tracks, const TimeSignature& time, Tick from, Tick to, Tick step,
                             const std::optional<theory::Key>& key)
{
    std::vector<std::vector<Note>> notes;
    for (const auto* t : tracks)
    {
        if (t == nullptr || notation::isDrumTrack (*t))
            continue;
        auto part = notation::timelineNotes (*t);
        std::sort (part.begin(), part.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        if (! part.empty())
            notes.push_back (std::move (part));
    }
    std::vector<ScoreMark> marks;
    if (notes.empty() || to <= from)
        return marks;
    step = std::clamp<Tick> (step, kTicksPerQuarter / 4, time.ticksPerBar());
    // Steps start on bar lines: a step never crosses a bar line.
    const Tick bar = time.ticksPerBar();
    std::string last;
    for (Tick barStart = (from / bar) * bar; barStart < to; barStart += bar)
        for (Tick at = barStart; at < barStart + bar && at < to; at += step)
        {
            const Tick end = std::min ({ at + step, barStart + bar, to });
            if (end <= from)
                continue;
            // The parts that play chords say the chord; a melody helps only when they are not enough.
            auto chord = chordOf (notes, std::max (at, from), end, 0.0);
            if (! chord)
                chord = chordOf (notes, std::max (at, from), end, 0.3);
            if (! chord)
                continue; // silence or one note: the last chord goes on
            const auto text = key ? theory::chordName (*chord, *key) : style_chord::name (*chord);
            if (text == last)
                continue;
            last = text;
            marks.push_back ({ ScoreMark::Kind::chord, std::max (at, from), text });
        }
    return marks;
}

void replaceChords (Track& track, Tick from, Tick to, const std::vector<ScoreMark>& chords)
{
    std::erase_if (track.marks, [&] (const ScoreMark& m) { return m.kind == ScoreMark::Kind::chord && m.at >= from && m.at < to; });
    track.marks.insert (track.marks.end(), chords.begin(), chords.end());
    std::stable_sort (track.marks.begin(), track.marks.end(), [] (const ScoreMark& a, const ScoreMark& b) { return a.at < b.at; });
}

} // namespace arranger::chords

namespace arranger::guitar
{

namespace
{
    constexpr int X = 99; // a muted string in the shape tables
    constexpr int kOpen[6] = { 4, 9, 2, 7, 11, 4 }; // E A D G B E

    struct Movable
    {
        ChordType type;
        std::array<int, 6> frets; ///< from the root fret on the low E (E shapes) or the A string (A shapes)
    };

    // Shapes with the root on the low E string (the E chord family).
    constexpr Movable kEShapes[] = {
        { ChordType::major, { 0, 2, 2, 1, 0, 0 } },        { ChordType::minor, { 0, 2, 2, 0, 0, 0 } },
        { ChordType::seventh, { 0, 2, 0, 1, 0, 0 } },      { ChordType::minor7, { 0, 2, 0, 0, 0, 0 } },
        { ChordType::major7, { 0, X, 1, 1, 0, X } },       { ChordType::major6, { 0, 2, 2, 1, 2, 0 } },
        { ChordType::minor6, { 0, 2, 2, 0, 2, 0 } },       { ChordType::sus4, { 0, 2, 2, 2, 0, 0 } },
        { ChordType::diminished, { 0, 1, 2, 0, X, X } },   { ChordType::diminished7, { X, X, 2, 3, 2, 3 } },
        { ChordType::augmented, { 0, 3, 2, 1, 1, 0 } },    { ChordType::minor7Flat5, { 0, 1, 0, 0, X, X } },
        { ChordType::seventh9, { 0, 2, 0, 1, 0, 2 } },     { ChordType::majorAdd9, { 0, 2, 2, 1, 0, 2 } },
        { ChordType::seventhSus4, { 0, 2, 0, 2, 0, 0 } },  { ChordType::minor7_9, { 0, 2, 0, 0, 0, 2 } },
        { ChordType::fifth, { 0, 2, 2, X, X, X } },        { ChordType::octave, { 0, X, 2, X, X, X } },
        { ChordType::seventh13, { 0, X, 0, 1, 2, X } },    { ChordType::seventhFlat9, { 0, X, 0, 1, 0, 1 } },
        { ChordType::minorMajor7, { 0, 2, 1, 0, 0, 0 } },  { ChordType::minorAdd9, { 0, 2, 2, 0, 0, 2 } },
        { ChordType::seventhAugmented, { 0, X, 0, 1, 1, 0 } },
    };

    // Shapes with the root on the A string (the A chord family). The low E is not played.
    constexpr Movable kAShapes[] = {
        { ChordType::major, { X, 0, 2, 2, 2, 0 } },        { ChordType::minor, { X, 0, 2, 2, 1, 0 } },
        { ChordType::seventh, { X, 0, 2, 0, 2, 0 } },      { ChordType::minor7, { X, 0, 2, 0, 1, 0 } },
        { ChordType::major7, { X, 0, 2, 1, 2, 0 } },       { ChordType::major6, { X, 0, 2, 2, 2, 2 } },
        { ChordType::minor6, { X, 0, 2, 2, 1, 2 } },       { ChordType::sus4, { X, 0, 2, 2, 3, 0 } },
        { ChordType::sus2, { X, 0, 2, 2, 0, 0 } },         { ChordType::diminished, { X, 0, 1, 2, 1, X } },
        { ChordType::diminished7, { X, 0, 1, 2, 1, 2 } },  { ChordType::augmented, { X, 0, 3, 2, 2, 1 } },
        { ChordType::minor7Flat5, { X, 0, 1, 0, 1, X } },  { ChordType::seventh9, { X, 0, -1, 0, 0, 0 } },
        { ChordType::majorAdd9, { X, 0, 2, 4, 2, 0 } },    { ChordType::seventhSus4, { X, 0, 2, 0, 3, 0 } },
        { ChordType::major6_9, { X, 0, -1, -1, 0, 0 } },   { ChordType::seventhSharp9, { X, 0, -1, 0, 1, X } },
        { ChordType::seventhFlat5, { X, 0, 1, 0, 2, X } }, { ChordType::fifth, { X, 0, 2, 2, X, X } },
        { ChordType::minorMajor7, { X, 0, 2, 1, 1, 0 } },  { ChordType::major7_9, { X, 0, -1, 1, 0, X } },
    };

    struct Open
    {
        int root;
        ChordType type;
        std::array<int, 6> frets;
    };

    // The usual open chords that are not an E or A shape at the nut.
    constexpr Open kOpenChords[] = {
        { 0, ChordType::major, { X, 3, 2, 0, 1, 0 } },    { 0, ChordType::seventh, { X, 3, 2, 3, 1, 0 } },
        { 0, ChordType::major7, { X, 3, 2, 0, 0, 0 } },   { 0, ChordType::majorAdd9, { X, 3, 2, 0, 3, 0 } },
        { 2, ChordType::major, { X, X, 0, 2, 3, 2 } },    { 2, ChordType::minor, { X, X, 0, 2, 3, 1 } },
        { 2, ChordType::seventh, { X, X, 0, 2, 1, 2 } },  { 2, ChordType::minor7, { X, X, 0, 2, 1, 1 } },
        { 2, ChordType::major7, { X, X, 0, 2, 2, 2 } },   { 2, ChordType::sus4, { X, X, 0, 2, 3, 3 } },
        { 2, ChordType::sus2, { X, X, 0, 2, 3, 0 } },     { 2, ChordType::major6, { X, X, 0, 2, 0, 2 } },
        { 7, ChordType::major, { 3, 2, 0, 0, 0, 3 } },    { 7, ChordType::seventh, { 3, 2, 0, 0, 0, 1 } },
        { 7, ChordType::major6, { 3, 2, 0, 0, 0, 0 } },   { 5, ChordType::major7, { X, X, 3, 2, 1, 0 } },
    };

    std::optional<std::array<int, 6>> place (const std::array<int, 6>& rel, int rootFret)
    {
        std::array<int, 6> out {};
        for (std::size_t s = 0; s < 6; ++s)
        {
            if (rel[s] == X)
            {
                out[s] = kMuted;
                continue;
            }
            out[s] = rel[s] + rootFret;
            if (out[s] < 0)
                return std::nullopt;
        }
        return out;
    }

    int highest (const std::array<int, 6>& f)
    {
        return *std::max_element (f.begin(), f.end());
    }

    /** A plainer chord for types that have no shape here (C7#11 is played as C7). */
    ChordType plainer (ChordType t)
    {
        switch (t)
        {
            case ChordType::major7Sharp11: return ChordType::major7;
            case ChordType::seventhSharp11: case ChordType::seventhFlat13: return ChordType::seventh;
            case ChordType::minor7_11: return ChordType::minor7;
            case ChordType::minorMajor7_9: return ChordType::minorMajor7;
            case ChordType::major7Augmented: return ChordType::augmented;
            case ChordType::cancel: return ChordType::cancel;
            default: return ChordType::major;
        }
    }

    std::optional<std::array<int, 6>> best (int root, ChordType type)
    {
        for (const auto& o : kOpenChords)
            if (o.root == root && o.type == type)
                return place (o.frets, 0);
        std::optional<std::array<int, 6>> found;
        const auto consider = [&] (const Movable* list, std::size_t count, int stringNote) {
            for (std::size_t i = 0; i < count; ++i)
            {
                if (list[i].type != type)
                    continue;
                const int fret = ((root - stringNote) % 12 + 12) % 12;
                for (const int f : { fret, fret + 12 })
                    if (auto p = place (list[i].frets, f); p && highest (*p) <= 15)
                    {
                        if (! found || highest (*p) < highest (*found))
                            found = p;
                        break;
                    }
            }
        };
        consider (kEShapes, std::size (kEShapes), kOpen[0]);
        consider (kAShapes, std::size (kAShapes), kOpen[1]);
        return found;
    }
}

std::optional<Shape> shape (const Chord& chord)
{
    if (chord.type == ChordType::cancel)
        return std::nullopt;
    const int root = ((chord.root % 12) + 12) % 12;
    auto frets = best (root, chord.type);
    if (! frets)
        frets = best (root, plainer (chord.type));
    if (! frets)
        return std::nullopt;
    Shape s;
    s.frets = *frets;
    if (highest (s.frets) > 4)
    {
        int lowest = std::numeric_limits<int>::max();
        for (const int f : s.frets)
            if (f > 0)
                lowest = std::min (lowest, f);
        s.firstFret = lowest;
    }
    return s;
}

std::vector<int> notes (const Shape& s)
{
    std::vector<int> out;
    for (std::size_t i = 0; i < 6; ++i)
        if (s.frets[i] != kMuted)
            out.push_back ((kOpen[i] + s.frets[i]) % 12);
    return out;
}

} // namespace arranger::guitar

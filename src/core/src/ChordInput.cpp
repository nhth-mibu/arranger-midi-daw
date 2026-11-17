#include "arranger/core/ChordInput.h"

#include "ArrangingDetail.h"
#include "arranger/core/ChordFinder.h"
#include "arranger/core/SongEdit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace arranger::chord_input
{

namespace
{
    int mod12 (int v) { return ((v % 12) + 12) % 12; }

    /** The broad sound of a chord type, for comparing chords and for rules. */
    enum class Quality { major, minor, diminished, augmented, dominant, other };

    Quality qualityOf (ChordType t)
    {
        switch (t)
        {
            case ChordType::major: case ChordType::major6: case ChordType::major7: case ChordType::major7Sharp11:
            case ChordType::majorAdd9: case ChordType::major7_9: case ChordType::major6_9: case ChordType::sus2: case ChordType::sus4:
                return Quality::major;
            case ChordType::minor: case ChordType::minor6: case ChordType::minor7: case ChordType::minorAdd9: case ChordType::minor7_9:
            case ChordType::minor7_11: case ChordType::minorMajor7: case ChordType::minorMajor7_9:
                return Quality::minor;
            case ChordType::diminished: case ChordType::diminished7: case ChordType::minor7Flat5:
                return Quality::diminished;
            case ChordType::augmented: case ChordType::major7Augmented: case ChordType::seventhAugmented:
                return Quality::augmented;
            case ChordType::seventh: case ChordType::seventhSus4: case ChordType::seventhFlat5: case ChordType::seventh9:
            case ChordType::seventhSharp11: case ChordType::seventh13: case ChordType::seventhFlat9: case ChordType::seventhFlat13:
            case ChordType::seventhSharp9:
                return Quality::dominant;
            default: break;
        }
        return Quality::other;
    }

    /** Major and dominant chords both have a major third. */
    bool majorThird (ChordType t) { const auto q = qualityOf (t); return q == Quality::major || q == Quality::dominant || q == Quality::augmented; }
    bool hasSeventh (ChordType t) { return style_chord::tones (t).size() >= 4; }

    /** The same chord for rules: the root and whether the third is major, minor or diminished. */
    bool sameFunction (const Chord& a, const Chord& b)
    {
        if (a.root != b.root)
            return false;
        const auto qa = qualityOf (a.type), qb = qualityOf (b.type);
        return (qa == Quality::dominant ? Quality::major : qa) == (qb == Quality::dominant ? Quality::major : qb);
    }

    std::string name (const Chord& c, const theory::Key& k) { return theory::chordName (c, k); }

    /** The degree (0..6) of a chord of the key, or -1. In minor, a major V counts as V. */
    int degreeOf (const Chord& c, const theory::Key& k)
    {
        const auto steps = theory::modeSteps (k.mode);
        const auto triads = theory::diatonicChords (k, false);
        const int r = mod12 (c.root - k.tonic);
        for (int d = 0; d < 7; ++d)
        {
            if (steps[static_cast<std::size_t> (d)] != r)
                continue;
            const auto want = qualityOf (triads[static_cast<std::size_t> (d)].type);
            auto q = qualityOf (c.type);
            if (q == Quality::dominant)
                q = Quality::major;
            if (q == want || (k.minor() && d == 4 && q == Quality::major))
                return d;
        }
        return -1;
    }

    // How often one chord of the key follows another (rough shares from pop and common-practice music).
    constexpr double kMajorMoves[7][7] = {
        //  I     ii    iii   IV    V     vi    vii
        { 0.00, 0.12, 0.05, 0.30, 0.28, 0.22, 0.03 }, // I
        { 0.10, 0.00, 0.05, 0.10, 0.55, 0.10, 0.10 }, // ii
        { 0.05, 0.10, 0.00, 0.30, 0.10, 0.45, 0.00 }, // iii
        { 0.35, 0.10, 0.05, 0.00, 0.35, 0.12, 0.03 }, // IV
        { 0.50, 0.03, 0.05, 0.15, 0.00, 0.25, 0.02 }, // V
        { 0.10, 0.22, 0.10, 0.38, 0.20, 0.00, 0.00 }, // vi
        { 0.70, 0.00, 0.20, 0.00, 0.00, 0.10, 0.00 }, // vii°
    };
    constexpr double kMinorMoves[7][7] = {
        //  i     ii°   III   iv    v/V   VI    VII
        { 0.00, 0.06, 0.10, 0.28, 0.18, 0.20, 0.18 }, // i
        { 0.20, 0.00, 0.10, 0.00, 0.60, 0.00, 0.10 }, // ii°
        { 0.10, 0.00, 0.00, 0.25, 0.10, 0.35, 0.20 }, // III
        { 0.30, 0.05, 0.05, 0.00, 0.30, 0.10, 0.20 }, // iv
        { 0.55, 0.00, 0.05, 0.10, 0.00, 0.25, 0.05 }, // v / V
        { 0.10, 0.05, 0.15, 0.20, 0.15, 0.00, 0.35 }, // VI
        { 0.35, 0.00, 0.40, 0.10, 0.00, 0.15, 0.00 }, // VII
    };

    double move (const theory::Key& k, int from, int to)
    {
        return k.minor() ? kMinorMoves[from][to] : kMajorMoves[from][to];
    }

    /** Known progressions, as roman numerals; they repeat. */
    const std::vector<std::pair<const char*, std::vector<const char*>>>& progressions (bool minor)
    {
        static const std::vector<std::pair<const char*, std::vector<const char*>>> major {
            { "I–V–vi–IV", { "I", "V", "vi", "IV" } },
            { "vi–IV–I–V", { "vi", "IV", "I", "V" } },
            { "I–vi–IV–V", { "I", "vi", "IV", "V" } },
            { "ii–V–I", { "ii", "V", "I" } },
            { "I–IV–V–I", { "I", "IV", "V", "I" } },
            { "I–IV–vi–V", { "I", "IV", "vi", "V" } },
            { "IV–V–iii–vi", { "IV", "V", "iii", "vi" } },
            { "I–bVII–IV–I", { "I", "bVII", "IV", "I" } },
            { "I–iii–IV–V", { "I", "iii", "IV", "V" } },
            { "vi–ii–V–I", { "vi", "ii", "V", "I" } },
            { "I–V–vi–iii–IV–I–IV–V", { "I", "V", "vi", "iii", "IV", "I", "IV", "V" } },
        };
        static const std::vector<std::pair<const char*, std::vector<const char*>>> minorList {
            { "i–bVI–bIII–bVII", { "i", "VI", "III", "VII" } },
            { "i–iv–v–i", { "i", "iv", "v", "i" } },
            { "i–VII–VI–V", { "i", "VII", "VI", "V" } },
            { "i–iv–VII–III", { "i", "iv", "VII", "III" } },
            { "ii°–V–i", { "ii°", "V", "i" } },
            { "i–VI–VII–i", { "i", "VI", "VII", "i" } },
            { "i–VI–iv–V", { "i", "VI", "iv", "V" } },
        };
        return minor ? minorList : major;
    }

    double groupWeight (const std::string& group)
    {
        return group == "diatonic" ? 1.0 : (group == "secondary" ? 0.55 : 0.45);
    }

    Chord seventhOf (const Chord& c, const theory::Key& k)
    {
        if (hasSeventh (c.type))
            return c;
        const int d = degreeOf (c, k);
        if (d >= 0)
        {
            auto s = theory::diatonicChords (k, true)[static_cast<std::size_t> (d)];
            if (k.minor() && d == 4 && qualityOf (c.type) == Quality::major)
                s.type = ChordType::seventh;
            s.bass = c.bass;
            return s;
        }
        Chord s = c;
        switch (qualityOf (c.type))
        {
            case Quality::major: s.type = ChordType::major7; break;
            case Quality::minor: s.type = ChordType::minor7; break;
            case Quality::diminished: s.type = ChordType::minor7Flat5; break;
            default: break;
        }
        return s;
    }
}

// ---- the chord lane ------------------------------------------------------------------

TrackId chordTrack (const Song& s, const DeviceLibrary* devices)
{
    for (const auto& t : s.tracks)
        for (const auto& m : t.marks)
            if (m.kind == ScoreMark::Kind::chord)
                return t.id;
    for (const auto& t : s.tracks)
        if (t.output.kind != TrackOutput::Kind::audio && ! arranging::isDrumTrack (t, devices))
            return t.id;
    return s.tracks.empty() ? 0 : s.tracks.front().id;
}

std::vector<LaneChord> laneChords (const Song& s, const DeviceLibrary* devices)
{
    std::vector<LaneChord> out;
    const auto* t = edit::findTrack (s, chordTrack (s, devices));
    if (t == nullptr)
        return out;
    for (const auto& m : t->marks)
        if (m.kind == ScoreMark::Kind::chord)
        {
            if (! out.empty() && out.back().at == m.at)
                out.pop_back(); // two at one time: the last wins
            out.push_back ({ m.at, 0, m.text, style_chord::parse (m.text) });
        }
    const Tick bar = std::max<Tick> (1, s.timeSignature.ticksPerBar());
    const Tick songEnd = (std::max<Tick> (s.contentEnd(), 1) + bar - 1) / bar * bar;
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const Tick next = i + 1 < out.size() ? out[i + 1].at : std::max (songEnd, (out[i].at / bar + 1) * bar);
        out[i].end = next;
    }
    return out;
}

const LaneChord* chordAt (const std::vector<LaneChord>& lane, Tick t)
{
    const LaneChord* found = nullptr;
    for (const auto& c : lane)
    {
        if (c.at > t)
            break;
        found = &c;
    }
    return found != nullptr && t < found->end ? found : nullptr;
}

theory::Key songKey (const Song& s)
{
    return theory::parseKey (s.key).value_or (theory::makeKey (0, theory::Mode::major));
}

std::optional<Chord> parseChord (std::string_view text, const theory::Key& key)
{
    std::string word (text);
    while (! word.empty() && (word.back() == ',' || word.back() == ' '))
        word.pop_back();
    if (word.empty())
        return {};
    // A name first: "Am" is a chord, never a numeral. Roman numerals start with I, V, b, # or N.
    if (auto c = style_chord::parse (word); c && c->type != ChordType::cancel)
        return c;
    if (auto c = theory::fromRoman (word, key); c && c->type != ChordType::cancel)
        return c;
    return {};
}

namespace
{
    std::vector<std::string> words (std::string_view text)
    {
        std::vector<std::string> out;
        std::string w;
        for (const char ch : text)
        {
            if (ch == ' ' || ch == ',' || ch == '\t' || ch == '\n' || ch == '|')
            {
                if (! w.empty())
                    out.push_back (std::move (w));
                w.clear();
                if (ch == '|')
                    out.emplace_back ("|");
            }
            else
                w += ch;
        }
        if (! w.empty())
            out.push_back (std::move (w));
        return out;
    }
}

Typed typeChords (std::string_view text, const theory::Key& key, const TimeSignature& ts, Tick start)
{
    Typed out;
    const auto all = words (text);
    const bool barred = std::find (all.begin(), all.end(), "|") != all.end();

    // Each bar is a list of slots: a chord, or -1 for "hold the one before".
    std::vector<std::vector<std::optional<Chord>>> bars;
    std::vector<std::optional<Chord>> bar;
    const auto endBar = [&] {
        if (! bar.empty())
            bars.push_back (std::move (bar));
        bar.clear();
    };
    for (const auto& w : all)
    {
        if (w == "|")
        {
            endBar();
            continue;
        }
        if (w == "%")
        {
            endBar();
            if (! bars.empty())
                bars.push_back (bars.back());
            continue;
        }
        if (w == "/" || w == "." || w == "-")
        {
            if (! barred)
                endBar(); // one more bar of the chord before
            bar.push_back (std::nullopt);
            continue;
        }
        const auto c = parseChord (w, key);
        if (! c)
        {
            if (w != "||")
                out.unknown.push_back (w);
            continue;
        }
        if (! barred)
            endBar();
        bar.push_back (c);
    }
    endBar();

    const Tick barLen = std::max<Tick> (1, ts.ticksPerBar());
    const Tick beat = std::max<Tick> (1, ts.ticksPerBeat());
    std::optional<Chord> last;
    for (std::size_t b = 0; b < bars.size(); ++b)
    {
        const auto& slots = bars[b];
        const auto n = static_cast<Tick> (slots.size());
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            if (! slots[i])
                continue;
            Tick offset = barLen * static_cast<Tick> (i) / n;
            if (offset % beat != 0 && (barLen / n) >= beat)
                offset = (offset + beat / 2) / beat * beat;
            if (last && *last == *slots[i])
                continue;
            last = slots[i];
            out.marks.push_back ({ ScoreMark::Kind::chord, start + static_cast<Tick> (b) * barLen + offset, name (*slots[i], key) });
        }
    }
    out.bars = static_cast<int> (bars.size());
    out.end = start + static_cast<Tick> (bars.size()) * barLen;
    return out;
}

void writeChords (Song& s, Tick from, Tick to, const std::vector<ScoreMark>& marks, const DeviceLibrary* devices)
{
    auto* t = edit::findTrack (s, chordTrack (s, devices));
    if (t == nullptr)
        return;
    // The lane is one track: chord symbols of other tracks in these bars go.
    for (auto& other : s.tracks)
        if (other.id != t->id)
            std::erase_if (other.marks, [&] (const ScoreMark& m) { return m.kind == ScoreMark::Kind::chord && m.at >= from && m.at < to; });
    chords::replaceChords (*t, from, to, marks);
}

std::vector<Chord> chordsIn (std::string_view text, const theory::Key& key)
{
    std::vector<Chord> out;
    for (const auto& w : words (text))
        if (auto c = parseChord (w, key))
            out.push_back (*c);
    return out;
}

// ---- the chord palette (pads) ---------------------------------------------------------------

std::vector<PaletteChord> palette (const theory::Key& key)
{
    static const std::array<const char*, 16> major { "I", "ii", "iii", "IV", "V", "vi", "vii°", "V7",
                                                     "iv", "bVI", "bVII", "bIII", "V7/ii", "V7/IV", "V7/V", "V7/vi" };
    static const std::array<const char*, 16> minor { "i", "ii°", "III", "iv", "v", "VI", "VII", "V7",
                                                     "IV", "N", "I", "vii°7", "V7/iv", "V7/III", "V7/V", "V7/VI" };
    const auto& list = key.minor() ? minor : major;
    std::vector<PaletteChord> out;
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const auto c = theory::fromRoman (list[i], key);
        if (! c)
            continue;
        const std::string group = i < 8 ? "diatonic" : (i < 12 ? "borrowed" : "secondary");
        std::string roman = list[i];
        if (roman == "N")
            roman = "bII";
        out.push_back ({ *c, name (*c, key), roman, group });
    }
    return out;
}

// ---- the next chord --------------------------------------------------------------------

std::vector<Suggestion> suggestNext (const std::vector<Chord>& before, const theory::Key& key, std::size_t count)
{
    std::vector<Suggestion> out;
    const auto pal = palette (key);
    int sevenths = 0;
    for (const auto& c : before)
        sevenths += hasSeventh (c.type) && qualityOf (c.type) != Quality::dominant ? 1 : 0; // a V7 alone is not a jazz sign
    const bool useSevenths = ! before.empty() && sevenths * 2 >= static_cast<int> (before.size());

    // `lift`: the score adds to the one the chord has already (a known progression).
    const auto add = [&] (Chord c, double score, std::string why, bool lift = false) {
        if (useSevenths)
            c = seventhOf (c, key);
        for (auto& s : out)
            if (sameFunction (s.chord, c))
            {
                if (lift || score > s.score)
                {
                    s.score = lift ? s.score + score : score;
                    s.why = std::move (why);
                }
                return;
            }
        out.push_back ({ c, name (c, key), theory::roman (c, key), std::move (why), score });
    };

    if (before.empty())
    {
        const char* starts[] = { "I", "vi", "IV", "ii", "V" };
        const char* minorStarts[] = { "i", "VI", "iv", "VII", "v" };
        double score = 1.0;
        for (const auto* r : key.minor() ? minorStarts : starts)
            if (const auto c = theory::fromRoman (r, key))
            {
                add (*c, score, score == 1.0 ? "starts at home" : "a usual first chord");
                score -= 0.15;
            }
    }
    else
    {
        const auto& prev = before.back();
        const int pd = degreeOf (prev, key);
        const std::string homeName = name (theory::diatonicChords (key, false)[0], key);
        for (const auto& p : pal)
        {
            if (sameFunction (p.chord, prev))
                continue;
            const int cd = degreeOf (p.chord, key);
            double score = 0.0;
            std::string why;
            const bool candDominant = majorThird (p.chord.type) && cd < 0 && p.group == "secondary";
            if (pd >= 0 && cd >= 0)
            {
                score = move (key, pd, cd);
                if (cd == 0 && pd == 4)
                    why = "goes home: V to I";
                else if (cd == 0 && pd == 3)
                    why = "plagal \"amen\" move: IV to I";
                else if (cd == 0 && pd == 6)
                    why = "the leading note resolves home";
                else if (pd == 4 && cd == 5)
                    why = "deceptive move: V to vi instead of home";
                else if (cd == 4)
                    why = "builds tension toward " + homeName;
                else if (cd == 1 || cd == 3)
                    why = "moves away from home (pre-dominant)";
                else if (mod12 (p.chord.root - prev.root) == 5)
                    why = "the root falls a fifth: a strong move";
                else
                    why = "a common move in the key";
            }
            else if (pd < 0 && majorThird (prev.type) && cd >= 0 && cd != 0 && mod12 (prev.root - p.chord.root) == 7)
            {
                score = 0.9; // a secondary dominant resolves
                why = "resolves the secondary dominant " + name (prev, key);
            }
            else if (pd < 0 && cd >= 0)
            {
                // From a borrowed or other chord, back into the key.
                const int r = mod12 (prev.root - key.tonic);
                if (cd == 0)
                    score = 0.45, why = "back home after the colour chord";
                else if (cd == 4)
                    score = 0.3, why = "builds tension toward " + homeName;
                else if (cd == 3 && (r == 10 || r == 8))
                    score = 0.3, why = "a rock move back into the key";
                else if (mod12 (p.chord.root - prev.root) == 5)
                    score = 0.3, why = "the root falls a fifth: a strong move";
                else
                    score = 0.08, why = "back into the key";
            }
            if (candDominant)
            {
                // A secondary dominant is good before a chord that would follow well.
                const int target = mod12 (p.chord.root + 5);
                for (const auto& d : pal)
                    if (d.group == "diatonic" && d.chord.root == target && degreeOf (d.chord, key) > 0)
                    {
                        const int td = degreeOf (d.chord, key);
                        const double follow = pd >= 0 ? move (key, pd, td) : 0.1;
                        score = std::max (score, follow * 0.8);
                        why = "secondary dominant: pulls into " + d.name;
                        break;
                    }
            }
            if (p.group == "borrowed" && score <= 0.0)
            {
                const int r = mod12 (p.chord.root - key.tonic);
                if (pd == 0 || pd == 3 || pd == 5)
                    score = r == 5 || r == 10 || r == 8 ? 0.22 : 0.1;
                why = key.minor() ? "borrowed from the major key: a brighter colour"
                                  : "borrowed from the minor key: a darker colour";
            }
            if (score <= 0.0)
                continue;
            add (p.chord, score * groupWeight (p.group), why);
        }

        // Known progressions: when the last chords match one, its next chord gets a lift.
        std::vector<Chord> lifted;
        for (const auto& [label, romans] : progressions (key.minor()))
        {
            std::vector<Chord> pattern;
            for (const auto* r : romans)
                if (const auto c = theory::fromRoman (r, key))
                    pattern.push_back (*c);
            if (pattern.size() != romans.size())
                continue;
            const auto n = pattern.size();
            for (std::size_t length = std::min<std::size_t> (3, before.size()); length >= 2; --length)
            {
                bool found = false;
                for (std::size_t j = 0; j < n && ! found; ++j)
                {
                    bool match = true;
                    for (std::size_t i = 0; i < length && match; ++i)
                        match = sameFunction (before[before.size() - length + i], pattern[(j + n * 3 - length + 1 + i) % n]);
                    if (! match)
                        continue;
                    found = true;
                    const auto& next = pattern[(j + 1) % n];
                    if (std::any_of (lifted.begin(), lifted.end(), [&] (const Chord& l) { return sameFunction (l, next); }))
                        break; // one lift for a chord (vi-IV-I-V is I-V-vi-IV turned)
                    lifted.push_back (next);
                    add (next, 0.35 + 0.2 * static_cast<double> (length), std::string ("as in the ") + label + " progression", true);
                }
                if (found)
                    break;
            }
        }
    }

    std::stable_sort (out.begin(), out.end(), [] (const Suggestion& a, const Suggestion& b) { return a.score > b.score; });
    if (out.size() > count)
        out.resize (count);
    return out;
}

// ---- voicings --------------------------------------------------------------------------

const char* voicingName (Voicing v)
{
    switch (v)
    {
        case Voicing::close: return "Close";
        case Voicing::open: return "Open";
        case Voicing::drop2: return "Drop 2";
        case Voicing::smooth: return "Smooth";
    }
    return "Close";
}

namespace
{
    std::vector<int> upperNotes (const Chord& c, int inversion, Voicing v, int centre)
    {
        auto rel = style_chord::tones (c.type);
        if (rel.empty())
            rel = { 0 };
        // Notes above an octave (9ths, 11ths, 13ths) stay above the root.
        std::vector<int> notes;
        for (const int r : rel)
            notes.push_back (c.root + r);
        const int n = static_cast<int> (notes.size());
        const int inv = n > 0 ? ((inversion % n) + n) % n : 0;
        for (int i = 0; i < inv; ++i)
        {
            std::sort (notes.begin(), notes.end());
            notes.front() += 12;
        }
        std::sort (notes.begin(), notes.end());
        if (v == Voicing::open && notes.size() >= 3)
            notes[1] += 12;
        if (v == Voicing::drop2 && notes.size() >= 3)
            notes[notes.size() - 2] -= 12;
        std::sort (notes.begin(), notes.end());
        // The octave that puts the lowest note near the centre (from 6 below to 5 above).
        const int lowest = notes.front();
        int shift = 0;
        while (lowest + shift < centre - 6)
            shift += 12;
        while (lowest + shift > centre + 5)
            shift -= 12;
        for (auto& p : notes)
            p += shift;
        return notes;
    }

    double distance (const std::vector<int>& a, const std::vector<int>& b)
    {
        // Each note to the nearest note of the other chord, both ways.
        double d = 0.0;
        for (const int x : a)
        {
            int best = 128;
            for (const int y : b)
                best = std::min (best, std::abs (x - y));
            d += best;
        }
        for (const int y : b)
        {
            int best = 128;
            for (const int x : a)
                best = std::min (best, std::abs (x - y));
            d += best;
        }
        return d;
    }
}

std::vector<int> voice (const Chord& c, const VoiceOptions& o, const std::vector<int>& previousIn)
{
    std::vector<int> notes;
    if (o.voicing == Voicing::smooth && ! previousIn.empty())
    {
        auto previous = previousIn;
        std::sort (previous.begin(), previous.end());
        if (o.bass && previous.size() > 2)
            previous.erase (previous.begin()); // compare the upper notes only
        const int n = static_cast<int> (style_chord::tones (c.type).size());
        double best = 1e9;
        for (int inv = 0; inv < std::max (1, n); ++inv)
            for (int shift : { -12, 0, 12 })
            {
                auto cand = upperNotes (c, inv, Voicing::close, o.centre);
                for (auto& p : cand)
                    p += shift;
                const double cost = distance (cand, previous) + 0.5 * std::abs (cand.front() - o.centre);
                if (cost < best)
                {
                    best = cost;
                    notes = cand;
                }
            }
    }
    else
        notes = upperNotes (c, o.inversion, o.voicing == Voicing::smooth ? Voicing::close : o.voicing, o.centre);

    if (o.bass && ! notes.empty())
    {
        const int bassPc = c.bass >= 0 ? c.bass : c.root;
        const int lowest = notes.front();
        int b = lowest - mod12 (lowest - bassPc);
        if (lowest - b < 5)
            b -= 12;
        notes.insert (notes.begin(), b);
    }
    for (auto& p : notes)
        p = std::clamp (p, 0, 127);
    std::sort (notes.begin(), notes.end());
    notes.erase (std::unique (notes.begin(), notes.end()), notes.end());
    return notes;
}

// ---- chords played on a keyboard -------------------------------------------------------------

std::optional<Chord> ChordListener::noteOn (int pitch)
{
    held.insert (pitch);
    if (held.size() < 3)
        return {};
    const std::vector<int> keys (held.begin(), held.end());
    const auto c = style_chord::detect (keys);
    if (! c || (last && *last == *c))
        return {};
    last = c;
    return c;
}

void ChordListener::noteOff (int pitch)
{
    held.erase (pitch);
}

std::vector<ScoreMark> quantiseHits (const std::vector<ChordHit>& hits, Tick grid, const theory::Key& key)
{
    grid = std::max<Tick> (1, grid);
    std::map<Tick, Chord> lines;
    for (const auto& h : hits)
    {
        const auto line = static_cast<Tick> (std::llround (std::max (0.0, h.tick) / static_cast<double> (grid))) * grid;
        lines[line] = h.chord; // the last one played wins
    }
    std::vector<ScoreMark> out;
    std::optional<Chord> last;
    for (const auto& [at, c] : lines)
    {
        if (last && *last == c)
            continue;
        last = c;
        out.push_back ({ ScoreMark::Kind::chord, at, name (c, key) });
    }
    return out;
}

// ---- reharmonizing ---------------------------------------------------------------------

std::vector<Alternative> alternatives (const Chord& c, const theory::Key& key, const std::optional<Chord>& next, std::size_t count)
{
    std::vector<Alternative> out;
    const auto q = qualityOf (c.type);
    const int r = mod12 (c.root - key.tonic);
    const bool seventh = hasSeventh (c.type);
    const std::string was = name (c, key);
    const auto add = [&] (std::string label, std::string why, std::vector<Chord> chords) {
        if (chords.size() == 1 && chords.front() == c)
            return;
        for (const auto& a : out)
            if (a.chords == chords)
                return;
        out.push_back ({ std::move (label), std::move (why), std::move (chords) });
    };
    const bool nextOther = next && next->root != c.root;

    // 1. Tritone substitute for a dominant chord.
    if (q == Quality::dominant || (q == Quality::major && r == 7))
    {
        const Chord sub { mod12 (c.root + 6), ChordType::seventh, -1 };
        add ("Tritone substitute: " + name (sub, key),
             "A dominant seventh a tritone away has the same two tension notes; the bass slides down a half step.", { sub });
    }
    // 2. A secondary dominant into the next chord, in the second half.
    if (nextOther && mod12 (next->root + 7) != c.root)
    {
        const Chord v { mod12 (next->root + 7), ChordType::seventh, -1 };
        add ("Lead into " + name (*next, key) + " with " + name (v, key),
             "The second half becomes the dominant of the next chord, so it pulls into it.", { c, v });
    }
    // 3. The relative chord: two notes in common.
    if (q == Quality::major || q == Quality::dominant)
    {
        const Chord rel { mod12 (c.root + 9), seventh ? ChordType::minor7 : ChordType::minor, -1 };
        add ("Relative minor: " + name (rel, key), "Shares two notes with " + was + ", so the melody still fits; a softer colour.", { rel });
    }
    else if (q == Quality::minor)
    {
        const Chord rel { mod12 (c.root + 3), seventh ? ChordType::major7 : ChordType::major, -1 };
        add ("Relative major: " + name (rel, key), "Shares two notes with " + was + ", so the melody still fits; a brighter colour.", { rel });
    }
    // 4. Borrowed from the parallel key.
    {
        std::optional<Chord> b;
        std::string why;
        if (! key.minor())
        {
            if (r == 5 && q == Quality::major) b = Chord { c.root, seventh ? ChordType::minor7 : ChordType::minor, -1 }, why = "The minor iv from the parallel minor: a wistful, darker turn.";
            else if (r == 9 && q == Quality::minor) b = Chord { mod12 (c.root - 1), seventh ? ChordType::major7 : ChordType::major, -1 }, why = "bVI from the parallel minor: a big, cinematic lift.";
            else if (r == 7 && (q == Quality::major || q == Quality::dominant)) b = Chord { mod12 (c.root + 3), ChordType::major, -1 }, why = "bVII in place of V: the rock and Mixolydian sound, a softer way home.";
            else if (r == 2 && q == Quality::minor) b = Chord { c.root, ChordType::minor7Flat5, -1 }, why = "The half-diminished ii from the parallel minor: darker before V.";
            else if (r == 0 && q == Quality::major) b = Chord { c.root, seventh ? ChordType::minor7 : ChordType::minor, -1 }, why = "The minor tonic: a sudden shadow over home.";
            else if (r == 4 && q == Quality::minor) b = Chord { mod12 (c.root - 1), ChordType::major, -1 }, why = "bIII from the parallel minor: a bold, bluesy colour.";
        }
        else
        {
            if (r == 5 && q == Quality::minor) b = Chord { c.root, seventh ? ChordType::seventh : ChordType::major, -1 }, why = "The major IV (Dorian): a brighter, hopeful colour in minor.";
            else if (r == 7 && q == Quality::minor) b = Chord { c.root, ChordType::seventh, -1 }, why = "The major V7 (harmonic minor): a much stronger pull home.";
            else if (r == 0 && q == Quality::minor) b = Chord { c.root, ChordType::major, -1 }, why = "The major tonic (Picardy third): a bright ending.";
        }
        if (b)
            add ("Borrowed: " + name (*b, key), why, { *b });
    }
    // 5. Richer colour.
    {
        Chord rich = c;
        rich.bass = -1;
        switch (q)
        {
            case Quality::major: rich.type = c.type == ChordType::major7 ? ChordType::major7_9 : (r == 0 || r == 5 ? ChordType::major7 : ChordType::majorAdd9); break;
            case Quality::minor: rich.type = c.type == ChordType::minor7 ? ChordType::minor7_9 : ChordType::minor7; break;
            case Quality::dominant: rich.type = c.type == ChordType::seventh9 ? ChordType::seventh13 : ChordType::seventh9; break;
            case Quality::diminished: rich.type = c.type == ChordType::diminished ? ChordType::diminished7 : ChordType::minor7Flat5; break;
            default: break;
        }
        rich.bass = c.bass;
        add ("Richer: " + name (rich, key), "The same chord with one more colour note; the harmony keeps its job.", { rich });
    }
    // 6. A suspension.
    if (q == Quality::major || q == Quality::dominant)
    {
        const Chord sus { c.root, q == Quality::dominant ? ChordType::seventhSus4 : ChordType::sus4, -1 };
        add ("Suspended: " + name (sus, key), "Holds the fourth in place of the third: open and unresolved.", { sus });
    }
    // 7. ii-V into the next chord.
    if (nextOther)
    {
        const bool minorTarget = qualityOf (next->type) == Quality::minor;
        const Chord ii { mod12 (next->root + 2), minorTarget ? ChordType::minor7Flat5 : ChordType::minor7, -1 };
        const Chord v { mod12 (next->root + 7), ChordType::seventh, -1 };
        if (! sameFunction (ii, c))
            add ("ii–V into " + name (*next, key) + ": " + name (ii, key) + " " + name (v, key),
                 "The jazz approach: two chords that walk into the next one.", { ii, v });
    }
    // 8. A passing diminished chord when the next root is a step up.
    if (nextOther && mod12 (next->root - c.root) == 2)
    {
        const Chord dim { mod12 (c.root + 1), ChordType::diminished7, -1 };
        add ("Passing " + name (dim, key), "A diminished chord in the second half: the bass climbs by half steps into " + name (*next, key) + ".", { c, dim });
    }
    // 9. A bass that walks toward the next chord.
    if (nextOther && c.bass < 0)
    {
        const auto rel = style_chord::tones (c.type);
        int bestPc = -1, bestDist = 3;
        for (const int t : rel)
        {
            const int pc = mod12 (c.root + t);
            if (pc == c.root || t >= 12)
                continue;
            const int d = std::min (mod12 (next->root - pc), mod12 (pc - next->root));
            if (d > 0 && d < bestDist)
                bestDist = d, bestPc = pc;
        }
        if (bestPc >= 0)
        {
            Chord slash = c;
            slash.bass = bestPc;
            add ("Walking bass: " + name (slash, key), "The bass plays another chord note, a step from " + name (*next, key) + ", so the bass line moves smoothly.", { slash });
        }
    }
    if (out.size() > count)
        out.resize (count);
    return out;
}

namespace
{
    int jobOf (int rel)
    {
        switch (mod12 (rel))
        {
            case 0: return 0;           // root
            case 3: case 4: return 1;   // third
            case 6: case 7: case 8: return 2; // fifth
            case 9: case 10: case 11: return 3; // sixth or seventh
            case 1: case 2: return 4;   // second or ninth
            default: return 5;          // fourth
        }
    }

    int nearestOctave (int pitch, int pc)
    {
        int best = pitch;
        for (int d = -6; d <= 5; ++d)
            if (mod12 (pitch + d) == pc)
                best = pitch + d;
        return std::clamp (best, 0, 127);
    }
}

int moveNote (int pitch, const Chord& from, const Chord& to, const theory::Key& key, bool bassPart)
{
    if (from == to)
        return pitch;
    const int pc = mod12 (pitch);
    const int fromBass = from.bass >= 0 ? from.bass : from.root;
    const int toBass = to.bass >= 0 ? to.bass : to.root;
    if (bassPart && (pc == fromBass || pc == from.root))
        return nearestOctave (pitch, toBass);

    std::vector<int> oldRel, newRel;
    for (const int t : style_chord::tones (from.type))
        oldRel.push_back (mod12 (t));
    for (const int t : style_chord::tones (to.type))
        newRel.push_back (mod12 (t));
    const int rel = mod12 (pc - from.root);
    const auto nearestNew = [&] (int pcFrom) {
        int best = pcFrom, bestD = 99;
        for (const int t : newRel)
        {
            const int cand = mod12 (to.root + t);
            const int d = std::min (mod12 (cand - pcFrom), mod12 (pcFrom - cand));
            if (d < bestD)
                bestD = d, best = cand;
        }
        return best;
    };
    if (std::find (oldRel.begin(), oldRel.end(), rel) != oldRel.end())
    {
        // A chord note: the note with the same job in the new chord.
        const int job = jobOf (rel);
        int bestRel = -1, bestD = 99;
        for (const int t : newRel)
            if (jobOf (t) == job)
            {
                const int d = std::abs (t - rel);
                if (d < bestD)
                    bestD = d, bestRel = t;
            }
        const int target = bestRel >= 0 ? mod12 (to.root + bestRel) : nearestNew (pc);
        return nearestOctave (pitch, target);
    }
    // Another note: it stays when it fits the key and does not rub against the new chord.
    const auto steps = theory::modeSteps (key.mode);
    const bool inKey = std::find (steps.begin(), steps.end(), mod12 (pc - key.tonic)) != steps.end();
    bool rubs = false;
    for (const int t : newRel)
    {
        const int cand = mod12 (to.root + t);
        const int d = std::min (mod12 (cand - pc), mod12 (pc - cand));
        rubs = rubs || d == 1;
    }
    if (inKey && ! rubs)
        return pitch;
    return nearestOctave (pitch, nearestNew (pc));
}

arranging::Proposal reharmonize (const Song& s, Tick at, const DeviceLibrary* devices, std::size_t count)
{
    using namespace arranging::detail;
    const auto key = songKey (s);
    const auto lane = laneChords (s, devices);
    const auto* lc = chordAt (lane, at);
    const Tick bar = barLength (s);
    if (lc == nullptr || ! lc->chord)
    {
        arranging::Proposal none;
        none.kind = "reharmonize";
        none.title = "No chord here";
        none.because = "Write a chord on the chord lane first, or find the chords from the music.";
        none.from = at / bar * bar;
        none.to = none.from + bar;
        return none;
    }
    const Tick from = lc->at;
    const Tick to = std::max (from + 1, std::min (lc->end, from + bar * 4));
    std::optional<Chord> next;
    for (const auto& c : lane)
        if (c.at >= lc->end)
        {
            if (c.at == lc->end)
                next = c.chord;
            break;
        }

    const auto chordId = chordTrack (s, devices);
    const auto roles = arranging::guessRoles (s, devices);
    std::vector<TrackId> tracks { chordId };
    std::map<TrackId, bool> bassOf;
    for (const auto& t : s.tracks)
    {
        const auto it = roles.find (t.id);
        if (it == roles.end() || it->second == arranging::Role::drums || it->second == arranging::Role::melody)
            continue;
        if (arranging::playedNotes (t, from, to).empty())
            continue;
        bassOf[t.id] = it->second == arranging::Role::bass;
        if (t.id != chordId)
            tracks.push_back (t.id);
    }

    const auto& c = *lc->chord;
    const std::string where = arranging::barsText (s, from, to);
    auto p = makeProposal (s, "reharmonize", "reharmonize-" + std::to_string (from) + "-" + lc->text,
                           "Reharmonize " + lc->text + " in " + where,
                           "Other chords for " + lc->text + " (" + theory::roman (c, key) + " in " + theory::name (key) + ")"
                               + (next ? ", before " + name (*next, key) : std::string())
                               + ". Each option changes the chord and moves the chord and bass parts to it. The melody and the drums stay.",
                           tracks, from, to);

    const Tick beat = beatLength (s);
    for (const auto& alt : alternatives (c, key, next, count))
    {
        std::vector<Track> changed;
        int moved = 0;
        std::vector<std::string> names;
        const auto n = static_cast<Tick> (alt.chords.size());
        const auto pieceStart = [&] (Tick i) {
            if (i >= n)
                return to;
            Tick t = from + (to - from) * i / n;
            return (t - from) % beat == 0 ? t : from + (t - from + beat / 2) / beat * beat;
        };
        for (const auto id : tracks)
        {
            const auto* t = edit::findTrack (s, id);
            if (t == nullptr)
                continue;
            Track copy = *t;
            int here = 0;
            if (const auto b = bassOf.find (id); b != bassOf.end())
                for (Tick i = 0; i < n; ++i)
                {
                    const auto& target = alt.chords[static_cast<std::size_t> (i)];
                    here += editNotes (copy, pieceStart (i), pieceStart (i + 1), [&] (Note& note) {
                        note.pitch = static_cast<std::uint8_t> (moveNote (note.pitch, c, target, key, b->second));
                        return true;
                    });
                }
            if (id == chordId)
            {
                std::vector<ScoreMark> marks;
                for (Tick i = 0; i < n; ++i)
                    marks.push_back ({ ScoreMark::Kind::chord, pieceStart (i), name (alt.chords[static_cast<std::size_t> (i)], key) });
                chords::replaceChords (copy, from, to, marks);
            }
            if (here > 0)
                names.push_back (t->name);
            moved += here;
            changed.push_back (std::move (copy));
        }
        std::string summary = alt.why;
        if (moved > 0)
        {
            std::string list;
            for (const auto& nm : names)
                list += (list.empty() ? "" : ", ") + nm;
            summary += " Moves " + arranging::detail::count (moved, "note", "notes") + " in " + list + ".";
        }
        else
            summary += " Only the chord symbol changes: no chord or bass part plays here.";
        addOption (p, alt.label, summary, std::move (changed), std::max (1, moved));
    }
    return p;
}

} // namespace arranger::chord_input

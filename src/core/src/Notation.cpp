#include "arranger/core/Notation.h"

#include "arranger/core/Theory.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

namespace arranger::notation
{

namespace
{
    int mod (int v, int m) { return ((v % m) + m) % m; }

    constexpr int kNaturalPc[7] = { 0, 2, 4, 5, 7, 9, 11 };

    /** The alteration that the key signature gives a letter. */
    int keyAlter (int step, int fifths)
    {
        // Sharps come in the order F C G D A E B, flats in the reverse order.
        static constexpr int kSharpOrder[7] = { 3, 0, 4, 1, 5, 2, 6 };
        if (fifths > 0)
            for (int i = 0; i < std::min (fifths, 7); ++i)
                if (kSharpOrder[i] == step)
                    return 1;
        if (fifths < 0)
            for (int i = 0; i < std::min (-fifths, 7); ++i)
                if (kSharpOrder[6 - i] == step)
                    return -1;
        return 0;
    }

    struct Placed
    {
        Tick start = 0, end = 0;
        int pitch = 60;
        int source = -1;
    };

    /** How a bar divides: the beat and whether the beats are dotted (6/8, 9/8, 12/8). */
    struct Meter
    {
        Tick bar = kTicksPerQuarter * 4;
        Tick beat = kTicksPerQuarter;
        bool compound = false;
        int beats = 4;
    };

    Meter meterOf (TimeSignature t)
    {
        Meter m;
        m.bar = t.ticksPerBar();
        m.compound = t.denominator == 8 && t.numerator % 3 == 0 && t.numerator > 3;
        m.beat = m.compound ? t.ticksPerBeat() * 3 : t.ticksPerBeat();
        m.beats = static_cast<int> (m.bar / std::max<Tick> (1, m.beat));
        return m;
    }

    struct Piece
    {
        Tick start = 0, length = 0; // within the bar
        Value value = Value::quarter;
        int dots = 0;
        bool triplet = false;
    };

    struct Kind
    {
        Tick length;
        Value value;
        int dots;
    };

    constexpr Tick q = kTicksPerQuarter;
    constexpr Kind kKinds[] = {
        { q * 4, Value::whole, 0 },      { q * 3, Value::half, 1 },        { q * 2, Value::half, 0 },
        { q * 3 / 2, Value::quarter, 1 }, { q, Value::quarter, 0 },         { q * 3 / 4, Value::eighth, 1 },
        { q / 2, Value::eighth, 0 },     { q * 3 / 8, Value::sixteenth, 1 }, { q / 4, Value::sixteenth, 0 },
        { q / 8, Value::thirtySecond, 0 },
    };

    /** Splits [p, e) of a bar into note values that show the beats. */
    void splitPlain (Tick p, Tick e, bool rest, const Meter& m, std::vector<Piece>& out)
    {
        while (p < e)
        {
            const Kind* chosen = nullptr;
            for (const auto& k : kKinds)
            {
                const Tick end = p + k.length;
                if (end > e)
                    continue;
                if (k.length < m.beat)
                {
                    if (p / m.beat != (end - 1) / m.beat)
                        continue; // shorter than a beat: stays inside its beat
                    if (rest && k.dots > 0 && ! m.compound)
                        continue;
                }
                else
                {
                    if (p % m.beat != 0)
                        continue;
                    if (m.compound && k.length % m.beat != 0)
                        continue;
                    if (rest && k.dots > 0 && ! m.compound && k.length != m.bar)
                        continue;
                    // In 4/4, show the middle of the bar unless the note starts the bar.
                    if (! m.compound && m.beats == 4 && p != 0 && p < m.bar / 2 && end > m.bar / 2)
                        continue;
                }
                chosen = &k;
                break;
            }
            if (chosen == nullptr)
            {
                // Off the grid (should not happen after quantising): take what is left.
                out.push_back ({ p, e - p, Value::thirtySecond, 0, false });
                return;
            }
            out.push_back ({ p, chosen->length, chosen->value, chosen->dots, false });
            p += chosen->length;
        }
    }

    Value valueForTicks (Tick t)
    {
        for (const auto& k : kKinds)
            if (k.dots == 0 && k.length == t)
                return k.value;
        return Value::eighth;
    }
}

int beamCount (Value v) noexcept
{
    switch (v)
    {
        case Value::eighth: return 1;
        case Value::sixteenth: return 2;
        case Value::thirtySecond: return 3;
        default: return 0;
    }
}

int keyFifths (const std::string& keyName)
{
    const auto key = theory::parseKey (keyName);
    return key ? key->fifths : 0;
}

bool minorKey (const std::string& keyName)
{
    const auto key = theory::parseKey (keyName);
    return key && key->mode != theory::Mode::major && key->mode != theory::Mode::lydian && key->mode != theory::Mode::mixolydian;
}

Spelling spellPlace (int pitch, int place)
{
    Spelling s;
    s.step = theory::letterOf (place);
    s.alter = std::clamp (theory::alterOf (place), -2, 2);
    s.octave = (pitch - s.alter) / 12 - 1;
    return s;
}

int spellingPlace (int pitch, int fifths, bool minor)
{
    // The line of fifths: notes of the key keep their spelling; other notes take the spelling nearest to the
    // key (C major: C# Eb F# G# Bb; A minor: the raised 6th and 7th are F# and G#).
    return theory::fifthsPlace (mod (pitch, 12), fifths + (minor ? 3 : 2));
}

Spelling spell (int pitch, int fifths) { return spell (pitch, fifths, false); }

Spelling spell (int pitch, int fifths, bool minor) { return spellPlace (pitch, spellingPlace (pitch, fifths, minor)); }

namespace
{
    struct DrumPlace
    {
        int position;
        bool cross;
    };

    DrumPlace drumPlace (int pitch)
    {
        switch (pitch)
        {
            case 35: case 36: return { 1, false };   // kick: bottom space
            case 37: return { 5, true };             // side stick
            case 38: case 40: return { 5, false };   // snare: third space
            case 39: return { 5, true };             // clap
            case 41: return { 2, false };            // low floor tom
            case 43: return { 3, false };            // floor tom
            case 45: case 47: return { 6, false };   // mid toms
            case 48: case 50: return { 7, false };   // high toms
            case 42: return { 10, true };            // closed hi-hat
            case 46: return { 10, true };            // open hi-hat
            case 44: return { -1, true };            // pedal hi-hat
            case 49: case 57: case 52: case 55: return { 11, true }; // crash, china, splash
            case 51: case 59: case 53: return { 9, true };           // ride
            default: return { 4, false };
        }
    }

    int positionFor (const Spelling& s, Clef clef)
    {
        const int diatonic = s.octave * 7 + s.step;
        return clef == Clef::bass ? diatonic - (2 * 7 + 4) : diatonic - (4 * 7 + 2);
    }

    /** Builds one staff from quantised notes. */
    Staff buildStaff (const std::vector<Placed>& notes, Clef clef, Tick end, const Meter& meter, const std::set<Tick>& tripletBeats,
                      int fifths, bool minor, bool drums)
    {
        Staff staff;
        staff.clef = clef;

        // 1. One voice: a chord at each start, held to its longest note or the next start.
        std::map<Tick, std::vector<const Placed*>> byStart;
        for (const auto& n : notes)
            byStart[n.start].push_back (&n);
        struct Span
        {
            Tick start, end;
            std::vector<int> pitches;
            std::map<int, int> sources; // pitch -> index of the note it comes from
        };
        std::vector<Span> spans;
        for (auto it = byStart.begin(); it != byStart.end(); ++it)
        {
            Tick e = 0;
            std::vector<int> pitches;
            std::map<int, int> sources;
            for (const auto* n : it->second)
            {
                e = std::max (e, n->end);
                pitches.push_back (n->pitch);
                sources.emplace (n->pitch, n->source);
            }
            const auto next = std::next (it);
            if (drums)
            {
                // A drum hit has no real length: hold it to the next hit, but
                // not past its beat, so the hits beam and do not need rests.
                const Tick beatEnd = ((it->first % meter.bar) / meter.beat + 1) * meter.beat + (it->first / meter.bar) * meter.bar;
                e = beatEnd;
            }
            if (next != byStart.end())
                e = std::min (e, next->first);
            std::sort (pitches.begin(), pitches.end());
            pitches.erase (std::unique (pitches.begin(), pitches.end()), pitches.end());
            if (e > it->first)
                spans.push_back ({ it->first, e, std::move (pitches), std::move (sources) });
        }

        // 2. Bars, with rests in the gaps and ties across bar lines.
        const auto splitSpan = [&] (Tick p, Tick e, bool rest, Tick barStart, std::vector<Piece>& out) {
            // Cut at the edges of triplet beats; inside them use triplet values.
            std::vector<Tick> cuts { p };
            for (Tick b = (p / meter.beat) * meter.beat; b < e; b += meter.beat)
                if (tripletBeats.count (barStart + b) != 0)
                {
                    if (b > p) cuts.push_back (b);
                    if (b + meter.beat < e) cuts.push_back (b + meter.beat);
                }
            cuts.push_back (e);
            std::sort (cuts.begin(), cuts.end());
            cuts.erase (std::unique (cuts.begin(), cuts.end()), cuts.end());
            for (std::size_t i = 0; i + 1 < cuts.size(); ++i)
            {
                const Tick a = cuts[i], b = cuts[i + 1];
                const Tick beatStart = (a / meter.beat) * meter.beat;
                const bool inTriplet = tripletBeats.count (barStart + beatStart) != 0 && ! (a == beatStart && b - a == meter.beat);
                if (! inTriplet)
                {
                    splitPlain (a, b, rest, meter, out);
                    continue;
                }
                const Tick unit = meter.beat / 3;
                for (Tick x = a; x < b;)
                {
                    const Tick len = (b - x >= unit * 2 && (x - beatStart) % unit == 0 && x - beatStart <= unit) ? unit * 2 : unit;
                    const Tick written = len * 3 / 2;
                    out.push_back ({ x, std::min (len, b - x), valueForTicks (written), 0, true });
                    x += len;
                }
            }
        };

        std::size_t next = 0;
        for (Tick barStart = 0; barStart < end; barStart += meter.bar)
        {
            Bar bar;
            bar.start = barStart;
            bar.length = meter.bar;
            const Tick barEnd = barStart + meter.bar;

            struct Item
            {
                Piece piece;
                std::vector<int> pitches;
                bool continues = false; // tied from the piece before
                bool tieOn = false;     // tied to the piece after
                const std::map<int, int>* sources = nullptr;
            };
            std::vector<Item> items;
            Tick cursor = barStart;
            while (next > 0 && next <= spans.size() && spans[next - 1].end > barStart)
                --next; // a span from the previous bar that reaches in
            for (std::size_t i = next; i < spans.size() && spans[i].start < barEnd; ++i)
            {
                const auto& s = spans[i];
                const Tick a = std::max (s.start, barStart), b = std::min (s.end, barEnd);
                if (b <= a)
                    continue;
                if (a > cursor)
                {
                    std::vector<Piece> rests;
                    splitSpan (cursor - barStart, a - barStart, true, barStart, rests);
                    for (const auto& r : rests)
                        items.push_back ({ r, {}, false, false, nullptr });
                }
                std::vector<Piece> pieces;
                splitSpan (a - barStart, b - barStart, false, barStart, pieces);
                for (std::size_t k = 0; k < pieces.size(); ++k)
                {
                    Item it { pieces[k], s.pitches, k > 0 || s.start < barStart, k + 1 < pieces.size() || s.end > barEnd, &s.sources };
                    items.push_back (std::move (it));
                }
                cursor = b;
            }
            while (next < spans.size() && spans[next].end <= barEnd)
                ++next;
            if (items.empty())
            {
                Event rest;
                rest.rest = true;
                rest.wholeBarRest = true;
                rest.start = 0;
                rest.length = meter.bar;
                rest.value = Value::whole;
                bar.events.push_back (rest);
                staff.bars.push_back (std::move (bar));
                continue;
            }
            if (cursor < barEnd)
            {
                std::vector<Piece> rests;
                splitSpan (cursor - barStart, meter.bar, true, barStart, rests);
                for (const auto& r : rests)
                    items.push_back ({ r, {}, false, false, nullptr });
            }

            // 3. Heads, spelling and accidentals.
            std::map<int, int> shown; // diatonic number -> alteration in force
            int lastPitch = -1, lastPlace = 0; // a repeated note keeps its spelling
            for (std::size_t itemIndex = 0; itemIndex < items.size(); ++itemIndex)
            {
                const auto& item = items[itemIndex];
                // A melody note out of the key that moves a semitone on is spelled in the direction it goes:
                // C C# D going up, D Db C going down (not C Db D, D C# C).
                int nextPitch = -1;
                if (item.pitches.size() == 1 && itemIndex + 1 < items.size() && items[itemIndex + 1].pitches.size() == 1)
                    nextPitch = items[itemIndex + 1].pitches.front();
                Event ev;
                ev.start = item.piece.start;
                ev.length = item.piece.length;
                ev.value = item.piece.value;
                ev.dots = item.piece.dots;
                ev.triplet = item.piece.triplet;
                ev.rest = item.pitches.empty();
                for (const int pitch : item.pitches)
                {
                    Head h;
                    h.pitch = pitch;
                    h.tieToNext = item.tieOn;
                    if (item.sources != nullptr)
                        if (const auto found = item.sources->find (pitch); found != item.sources->end())
                            h.source = found->second;
                    if (drums)
                    {
                        const auto d = drumPlace (pitch);
                        h.position = d.position;
                        h.cross = d.cross;
                    }
                    else
                    {
                        int place = spellingPlace (pitch, fifths, minor);
                        const bool inKey = place >= fifths - 1 && place <= fifths + 5;
                        if (pitch == lastPitch && item.pitches.size() == 1)
                            place = lastPlace;
                        else if (! inKey && nextPitch >= 0 && std::abs (nextPitch - pitch) == 1)
                        {
                            const int nextLetter = theory::letterOf (spellingPlace (nextPitch, fifths, minor));
                            if (theory::letterOf (place) == nextLetter)
                                place += nextPitch > pitch ? 12 : -12;
                        }
                        h.spelling = spellPlace (pitch, place);
                        if (item.pitches.size() == 1)
                            lastPitch = pitch, lastPlace = place;
                        h.position = positionFor (h.spelling, clef);
                        const int diatonic = h.spelling.octave * 7 + h.spelling.step;
                        const auto it = shown.find (diatonic);
                        const int inForce = it != shown.end() ? it->second : keyAlter (h.spelling.step, fifths);
                        if (h.spelling.alter != inForce && ! item.continues)
                        {
                            switch (h.spelling.alter)
                            {
                                case -2: h.accidental = Accidental::doubleFlat; break;
                                case -1: h.accidental = Accidental::flat; break;
                                case 1: h.accidental = Accidental::sharp; break;
                                case 2: h.accidental = Accidental::doubleSharp; break;
                                default: h.accidental = Accidental::natural; break;
                            }
                        }
                        shown[diatonic] = h.spelling.alter;
                    }
                    ev.heads.push_back (h);
                }
                std::sort (ev.heads.begin(), ev.heads.end(), [] (const Head& x, const Head& y) { return x.position != y.position ? x.position < y.position : x.pitch < y.pitch; });
                // Two drums on the same line: keep one head.
                ev.heads.erase (std::unique (ev.heads.begin(), ev.heads.end(), [] (const Head& x, const Head& y) { return x.position == y.position; }), ev.heads.end());
                bar.events.push_back (std::move (ev));
            }

            // 4. Triplet brackets: one per beat.
            int tuplet = 0;
            for (std::size_t i = 0; i < bar.events.size();)
            {
                if (! bar.events[i].triplet)
                {
                    ++i;
                    continue;
                }
                const Tick beat = bar.events[i].start / meter.beat;
                while (i < bar.events.size() && bar.events[i].triplet && bar.events[i].start / meter.beat == beat)
                    bar.events[i++].tupletGroup = tuplet;
                ++tuplet;
            }

            // 5. Beams: notes of eighth or shorter inside one beat.
            int group = 0;
            for (std::size_t i = 0; i < bar.events.size();)
            {
                const auto beamable = [&] (std::size_t k) { return ! bar.events[k].rest && beamCount (bar.events[k].value) > 0; };
                if (! beamable (i))
                {
                    ++i;
                    continue;
                }
                const Tick beat = bar.events[i].start / meter.beat;
                std::size_t j = i;
                while (j < bar.events.size() && beamable (j) && bar.events[j].start / meter.beat == beat)
                    ++j;
                if (j - i >= 2)
                {
                    for (std::size_t k = i; k < j; ++k)
                        bar.events[k].beamGroup = group;
                    ++group;
                }
                i = j;
            }

            // 6. Stems: away from the note farthest from the middle line.
            const auto stemUpFor = [&] (int low, int high) { return drums || (high - 4) <= (4 - low); };
            for (auto& ev : bar.events)
                if (! ev.rest && ev.beamGroup < 0)
                    ev.stemUp = stemUpFor (ev.heads.front().position, ev.heads.back().position);
            for (int g = 0; g < group; ++g)
            {
                int low = 1000, high = -1000;
                for (const auto& ev : bar.events)
                    if (ev.beamGroup == g)
                    {
                        low = std::min (low, ev.heads.front().position);
                        high = std::max (high, ev.heads.back().position);
                    }
                for (auto& ev : bar.events)
                    if (ev.beamGroup == g)
                        ev.stemUp = stemUpFor (low, high);
            }
            staff.bars.push_back (std::move (bar));
        }
        return staff;
    }

    bool containsWord (std::string s, const char* word)
    {
        for (auto& c : s)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return s.find (word) != std::string::npos;
    }
}

Part makePart (const std::string& name, const std::vector<Note>& notes, Tick end, TimeSignature time, int fifths, const Options& options)
{
    Part part;
    part.name = name;
    part.keyFifths = std::clamp (fifths, -7, 7);
    part.time = time.isValid() ? time : TimeSignature {};
    part.drums = options.drums;
    const auto meter = meterOf (part.time);

    Tick last = 0;
    for (const auto& n : notes)
        last = std::max (last, n.end());
    end = std::max ({ end, last, meter.bar });
    end = ((end + meter.bar - 1) / meter.bar) * meter.bar;

    // 1. Which beats read better as eighth triplets.
    const Tick grid = std::max<Tick> (kTicksPerQuarter / 8, options.grid);
    std::set<Tick> tripletBeats;
    if (options.triplets && ! meter.compound && meter.beat == kTicksPerQuarter)
    {
        std::map<Tick, std::pair<double, double>> error; // beat -> straight, triplet
        std::map<Tick, bool> offGrid;
        const Tick unit = meter.beat / 3;
        for (const auto& n : notes)
        {
            const Tick beat = ((n.start + grid / 2) / meter.beat) * meter.beat;
            const auto straight = std::abs (static_cast<double> (n.start - ((n.start + grid / 2) / grid) * grid));
            const Tick tripletPos = ((n.start + unit / 2) / unit) * unit;
            const auto triplet = std::abs (static_cast<double> (n.start - tripletPos));
            auto& e = error[beat];
            e.first += straight;
            e.second += triplet;
            if ((tripletPos - beat) % meter.beat != 0 && (tripletPos - beat) % grid != 0)
                offGrid[beat] = true;
        }
        for (const auto& [beat, e] : error)
            if (offGrid[beat] && e.second + static_cast<double> (grid) / 8.0 < e.first)
                tripletBeats.insert (beat);
    }

    const auto snap = [&] (Tick t) {
        const Tick beat = (t / meter.beat) * meter.beat;
        const Tick unit = tripletBeats.count (beat) != 0 ? meter.beat / 3 : grid;
        return ((t + unit / 2) / unit) * unit;
    };

    std::vector<Placed> placed;
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const auto& n = notes[index];
        Placed p;
        p.pitch = n.pitch;
        p.source = static_cast<int> (index);
        p.start = snap (n.start);
        // The end snaps by the grid of the beat it falls in.
        p.end = snap (n.end());
        const Tick startBeat = (p.start / meter.beat) * meter.beat;
        const Tick minimum = tripletBeats.count (startBeat) != 0 ? meter.beat / 3 : grid;
        if (p.end - p.start < minimum)
            p.end = p.start + minimum;
        if (p.start >= end)
            continue;
        p.end = std::min (p.end, end);
        placed.push_back (p);
    }
    std::sort (placed.begin(), placed.end(), [] (const Placed& a, const Placed& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });

    // 2. One staff or two.
    if (options.drums)
    {
        part.staves.push_back (buildStaff (placed, Clef::percussion, end, meter, tripletBeats, 0, false, true));
        return part;
    }
    bool grand = options.staves == Options::Staves::grand;
    if (options.staves == Options::Staves::automatic && ! placed.empty())
    {
        std::size_t low = 0, high = 0;
        int minPitch = 127, maxPitch = 0;
        for (const auto& p : placed)
        {
            (p.pitch < options.split ? low : high)++;
            minPitch = std::min (minPitch, p.pitch);
            maxPitch = std::max (maxPitch, p.pitch);
        }
        const bool keys = containsWord (name, "piano") || containsWord (name, "keys") || containsWord (name, "organ") || containsWord (name, "harp")
                          || containsWord (name, "rhodes") || containsWord (name, "clav");
        const bool wide = maxPitch - minPitch > 24 && low * 7 >= placed.size() && high * 7 >= placed.size();
        grand = keys || wide;
    }
    if (grand)
    {
        part.grandStaff = true;
        std::vector<Placed> upper, lower;
        for (const auto& p : placed)
            (p.pitch >= options.split ? upper : lower).push_back (p);
        part.staves.push_back (buildStaff (upper, Clef::treble, end, meter, tripletBeats, part.keyFifths, options.minorKey, false));
        part.staves.push_back (buildStaff (lower, Clef::bass, end, meter, tripletBeats, part.keyFifths, options.minorKey, false));
        return part;
    }
    // One staff: bass clef when most notes are low.
    std::vector<int> pitches;
    for (const auto& p : placed)
        pitches.push_back (p.pitch);
    Clef clef = Clef::treble;
    if (! pitches.empty())
    {
        std::nth_element (pitches.begin(), pitches.begin() + static_cast<std::ptrdiff_t> (pitches.size() / 2), pitches.end());
        if (pitches[pitches.size() / 2] < 57)
            clef = Clef::bass;
    }
    part.staves.push_back (buildStaff (placed, clef, end, meter, tripletBeats, part.keyFifths, options.minorKey, false));
    return part;
}

namespace
{
    std::vector<std::pair<Note, NoteRef>> timelineNotesWithRefs (const Track& track)
    {
        std::vector<std::pair<Note, NoteRef>> out;
        for (const auto& c : track.clips)
        {
            const Tick round = c->loopLength > 0 && c->loopLength < c->length ? c->loopLength : c->length;
            for (Tick base = 0; base < c->length; base += round)
                for (std::size_t i = 0; i < c->notes.size(); ++i)
                {
                    auto n = c->notes[i];
                    if (n.start >= round || base + n.start >= c->length)
                        continue;
                    const Tick limit = std::min (round, c->length - base);
                    n.length = std::max<Tick> (1, std::min (n.length, limit - n.start));
                    n.start = c->start + base + n.start;
                    out.push_back ({ n, { c->id, i, c->start + base } });
                }
        }
        std::stable_sort (out.begin(), out.end(), [] (const auto& a, const auto& b) {
            return a.first.start != b.first.start ? a.first.start < b.first.start : a.first.pitch < b.first.pitch;
        });
        return out;
    }
}

Part makeScore (const std::vector<Part>& parts, const std::vector<int>& sourceOffsets)
{
    Part out;
    if (parts.empty())
        return out;
    out.keyFifths = parts.front().keyFifths;
    out.time = parts.front().time;
    std::size_t bars = static_cast<std::size_t> (-1);
    for (const auto& p : parts)
        for (const auto& staff : p.staves)
            bars = std::min (bars, staff.bars.size());
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        const auto& p = parts[i];
        if (p.staves.empty())
            continue;
        const int offset = i < sourceOffsets.size() ? sourceOffsets[i] : 0;
        out.groups.push_back ({ p.name, out.staves.size(), p.staves.size(), p.marks });
        for (auto staff : p.staves)
        {
            staff.bars.resize (bars);
            for (auto& bar : staff.bars)
                for (auto& e : bar.events)
                    for (auto& h : e.heads)
                        if (h.source >= 0)
                            h.source += offset;
            out.staves.push_back (std::move (staff));
        }
    }
    return out;
}

std::vector<Note> timelineNotes (const Track& track)
{
    std::vector<Note> out;
    for (const auto& [note, ref] : timelineNotesWithRefs (track))
        out.push_back (note);
    return out;
}

std::vector<NoteRef> timelineNoteRefs (const Track& track)
{
    std::vector<NoteRef> out;
    for (const auto& [note, ref] : timelineNotesWithRefs (track))
        out.push_back (ref);
    return out;
}

int pitchAt (Clef clef, int position, int fifths)
{
    const int diatonic = position + (clef == Clef::bass ? 2 * 7 + 4 : 4 * 7 + 2);
    const int step = mod (diatonic, 7);
    const int octave = (diatonic - step) / 7;
    return std::clamp (12 * (octave + 1) + kNaturalPc[step] + keyAlter (step, fifths), 0, 127);
}

int drumPitchAt (int position)
{
    static constexpr std::pair<int, int> kPlaces[] = { { -1, 44 }, { 1, 36 }, { 2, 41 }, { 3, 43 }, { 5, 38 }, { 6, 45 }, { 7, 48 }, { 9, 51 }, { 10, 42 }, { 11, 49 } };
    int best = 38, distance = 1000;
    for (const auto& [place, pitch] : kPlaces)
        if (std::abs (place - position) < distance)
        {
            distance = std::abs (place - position);
            best = pitch;
        }
    return best;
}

bool isDrumTrack (const Track& track)
{
    return track.output.kind == TrackOutput::Kind::sampler || track.output.channel == 10;
}

} // namespace arranger::notation

#include "arranger/core/StyleFromAudio.h"

#include "arranger/core/Arranging.h"
#include "arranger/core/SongEdit.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <map>
#include <numeric>
#include <set>
#include <span>
#include <stdexcept>

namespace arranger::style_from_audio
{

namespace
{
    constexpr double kBeat = static_cast<double> (kTicksPerQuarter);
    constexpr double kEarly = kBeat / 8.0; ///< a note this much before a chord change belongs to the new chord

    int mod12 (int n) noexcept { return ((n % 12) + 12) % 12; }

    bool isKick (int p) noexcept { return p == 35 || p == 36; }
    bool isSnare (int p) noexcept { return p == 37 || p == 38 || p == 39 || p == 40; }
    int drumClass (int p) noexcept { return isKick (p) ? 0 : (isSnare (p) ? 1 : 2); }

    std::string letter (int group) { return std::string (1, static_cast<char> ('A' + std::clamp (group, 0, 3))); }
    std::string mainName (int group) { return "Main " + letter (group); }
    std::string fillName (int group) { return "Fill In " + letter (group) + letter (group); }

    int sectionOrder (const std::string& name)
    {
        for (int i = 0; i < static_cast<int> (std::size (kStyleSectionNames)); ++i)
            if (name == kStyleSectionNames[i])
                return i;
        return 100;
    }

    // ---- chords ---------------------------------------------------------------------

    bool has (const Chord& c, int interval)
    {
        const auto t = style_chord::tones (c.type);
        return std::find (t.begin(), t.end(), interval) != t.end();
    }

    // ---- the parts in song time -----------------------------------------------------

    struct Part
    {
        PartSource source;
        std::vector<Hit> hits; ///< sorted by tick
    };

    /** The default chord rule of a part: rhythm as recorded, bass follows the root (and the bass note), the rest follow the chord from CMaj7. */
    StyleChannelRule ruleFor (const PartSource& p)
    {
        StyleChannelRule r;
        r.sourceChannel = p.channel;
        r.destChannel = p.channel;
        r.name = kStylePartNames[std::clamp (p.channel, 9, 16) - 9];
        r.sourceRoot = 0;
        r.sourceChordType = static_cast<int> (ChordType::major7);
        if (p.drums)
        {
            r.transpose.ntr = StyleTranspose::Ntr::rootFixed;
            r.transpose.ntt = StyleTranspose::Ntt::bypass;
            r.transpose.rtr = StyleTranspose::Rtr::stop;
        }
        else if (p.channel == 11 || p.channel == 15 || p.channel == 16)
        {
            r.transpose.ntt = StyleTranspose::Ntt::melody;
            r.transpose.bass = p.channel == 11;
            r.transpose.rtr = StyleTranspose::Rtr::pitchShiftToRoot;
        }
        else
        {
            r.transpose.ntr = StyleTranspose::Ntr::rootFixed;
            r.transpose.ntt = StyleTranspose::Ntt::chord;
        }
        r.lowRange = r.highRange = r.transpose;
        return r;
    }

    // ---- sections -------------------------------------------------------------------

    struct BarInfo
    {
        std::array<double, 48> drums {}; ///< 3 classes x 16 steps
        std::vector<double> notes;       ///< by part
        double energy = 0.0;
    };

    double cosine (const std::array<double, 48>& a, const std::array<double, 48>& b)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            ab += a[i] * b[i];
            aa += a[i] * a[i];
            bb += b[i] * b[i];
        }
        if (aa <= 0.0 && bb <= 0.0)
            return 1.0;
        if (aa <= 0.0 || bb <= 0.0)
            return 0.0;
        return ab / std::sqrt (aa * bb);
    }

    /** How different two bars (or averages of bars) are, 0..1: the drum pattern, how busy each part is, and how loud it all is. */
    double distance (const BarInfo& a, const BarInfo& b)
    {
        double parts = 0.0;
        const auto n = std::min (a.notes.size(), b.notes.size());
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto level = [] (double x) { return std::min (1.0, std::log1p (x) / std::log1p (16.0)); };
            parts += std::abs (level (a.notes[i]) - level (b.notes[i]));
        }
        const double louder = std::max (a.energy, b.energy);
        const double energy = louder > 0.0 ? std::abs (a.energy - b.energy) / louder : 0.0;
        return 0.4 * (1.0 - cosine (a.drums, b.drums)) + 0.3 * (n > 0 ? parts / static_cast<double> (n) : 0.0) + 0.3 * energy;
    }

    BarInfo average (const std::vector<BarInfo>& bars, const std::vector<int>& which)
    {
        BarInfo out;
        if (bars.empty())
            return out;
        out.notes.assign (bars.front().notes.size(), 0.0);
        if (which.empty())
            return out;
        for (const int b : which)
        {
            const auto& x = bars[static_cast<std::size_t> (b)];
            for (std::size_t i = 0; i < out.drums.size(); ++i)
                out.drums[i] += x.drums[i];
            for (std::size_t i = 0; i < out.notes.size(); ++i)
                out.notes[i] += x.notes[i];
            out.energy += x.energy;
        }
        const auto n = static_cast<double> (which.size());
        for (auto& v : out.drums)
            v /= n;
        for (auto& v : out.notes)
            v /= n;
        out.energy /= n;
        return out;
    }

    // ---- the working state of the loop ----------------------------------------------

    struct SectionWork
    {
        std::string name;
        int group = 0;               ///< 0..3 for mains and fills; -1 intro, -2 ending
        bool main = false;
        int length = 1;              ///< bars
        std::vector<int> lengths;    ///< the lengths to try
        std::vector<int> bars;       ///< the bars of the song it plays in (compare)
        std::vector<int> sourceBars; ///< bars it may come from (for windows)
        std::vector<std::vector<int>> runs; ///< the source runs (phrases) the windows sit in
        std::map<int, PartSettings> settings; ///< by part channel
    };

    struct Work
    {
        const analysis::Result& result;
        Grid grid;
        Plan plan;
        std::vector<Part> parts;
        std::vector<BarInfo> barInfo;
        std::vector<SectionWork> sections;
        std::map<int, StyleChannelRule> rules; ///< by channel
        Tick barTicks = 0;
        int tried = 0;

        explicit Work (const analysis::Result& r) : result (r) {}

        const Chord& chordAt (double tick) const
        {
            static const Chord c;
            if (plan.beatChords.empty())
                return c;
            const auto beat = static_cast<long> (std::floor (tick / kBeat));
            return plan.beatChords[static_cast<std::size_t> (std::clamp<long> (beat, 0, static_cast<long> (plan.beatChords.size()) - 1))];
        }

        /** The windows (first bars) a section's pattern can come from, best first. */
        std::vector<int> windows (const SectionWork& s, int length) const
        {
            std::vector<int> out;
            const std::set<int> sourceSet (s.sourceBars.begin(), s.sourceBars.end());
            for (const auto& run : s.runs)
            {
                if (run.empty())
                    continue;
                for (std::size_t k = 0; k + static_cast<std::size_t> (length) <= run.size(); k += static_cast<std::size_t> (length))
                {
                    bool clean = true;
                    for (int i = 0; i < length; ++i)
                        clean = clean && sourceSet.count (run[k + static_cast<std::size_t> (i)]) != 0;
                    if (clean)
                        out.push_back (run[k]);
                }
            }
            if (out.empty())
                for (const auto& run : s.runs)
                    if (! run.empty())
                        out.push_back (run.front());
            if (out.empty() && ! s.sourceBars.empty())
                out.push_back (s.sourceBars.front());
            // Most like the section as a whole first.
            const auto whole = average (barInfo, s.sourceBars);
            std::stable_sort (out.begin(), out.end(), [&] (int a, int b) {
                const auto da = distanceOfWindow (a, length, whole);
                const auto db = distanceOfWindow (b, length, whole);
                return da < db;
            });
            if (out.size() > 12)
                out.resize (12);
            return out;
        }

        double distanceOfWindow (int start, int length, const BarInfo& whole) const
        {
            std::vector<int> w;
            for (int i = 0; i < length; ++i)
                if (start + i < static_cast<int> (barInfo.size()))
                    w.push_back (start + i);
            return distance (average (barInfo, w), whole);
        }

        /** The pattern of one part of one section with these settings: notes from tick 0, over `length` bars. */
        Clip pattern (const SectionWork& s, const Part& part, int length, const PartSettings& set) const
        {
            Clip clip;
            clip.name = s.name;
            clip.length = barTicks * length;
            const auto all = windows (s, length);
            if (all.empty())
                return clip;
            std::vector<int> use;
            if (set.consensus > 0)
                use = all;
            else
                use.push_back (all[static_cast<std::size_t> (std::clamp (set.window, 0, static_cast<int> (all.size()) - 1))]);

            const bool drums = part.source.drums;
            const bool bassPart = part.source.channel == 11;
            const double span = static_cast<double> (clip.length);
            const double step = kBeat / std::max (1, set.grid);

            struct Placed
            {
                double tick, length;
                int pitch, velocity, window;
            };
            std::vector<Placed> placed;
            for (std::size_t wi = 0; wi < use.size(); ++wi)
            {
                const double from = static_cast<double> (use[wi]) * static_cast<double> (barTicks);
                const auto first = std::lower_bound (part.hits.begin(), part.hits.end(), from, [] (const Hit& h, double t) { return h.tick < t; });
                for (auto it = first; it != part.hits.end() && it->tick < from + span; ++it)
                {
                    const auto& h = *it;
                    // Clean-up: weak or very short notes are often bleed from other instruments.
                    const int minVelocity = set.cleanup == 0 ? 0 : (set.cleanup == 1 ? (drums ? 15 : 20) : (drums ? 35 : 40));
                    const double minLength = drums || set.cleanup == 0 ? 0.0 : (set.cleanup == 1 ? kBeat / 16.0 : kBeat / 8.0);
                    if (h.velocity < minVelocity || h.length < minLength)
                        continue;
                    const int pitch = drums ? h.pitch : toSourceChord (h.pitch, chordAt (h.tick + kEarly), bassPart);
                    placed.push_back ({ h.tick - from, h.length, std::clamp (pitch, 0, 127), h.velocity, static_cast<int> (wi) });
                }
            }

            std::vector<Placed> kept;
            if (set.consensus > 0 && use.size() > 1)
            {
                // Notes that at least `consensus` % of the places play, at their usual time.
                std::map<std::pair<long, int>, std::vector<const Placed*>> cells;
                for (const auto& p : placed)
                    cells[{ std::lround (p.tick / step), p.pitch }].push_back (&p);
                const auto need = std::max<std::size_t> (2, static_cast<std::size_t> (std::ceil (static_cast<double> (use.size()) * set.consensus / 100.0)));
                for (auto& [key, group] : cells)
                {
                    std::set<int> places;
                    for (const auto* p : group)
                        places.insert (p->window);
                    if (places.size() < need)
                        continue;
                    std::vector<double> ticks, lengths;
                    double velocity = 0.0;
                    for (const auto* p : group)
                    {
                        ticks.push_back (p->tick);
                        lengths.push_back (p->length);
                        velocity += p->velocity;
                    }
                    std::sort (ticks.begin(), ticks.end());
                    std::sort (lengths.begin(), lengths.end());
                    kept.push_back ({ ticks[ticks.size() / 2], lengths[lengths.size() / 2], key.second,
                                      static_cast<int> (std::lround (velocity / static_cast<double> (group.size()))), 0 });
                }
            }
            else
                kept = std::move (placed);

            for (const auto& p : kept)
            {
                const double onGrid = std::round (p.tick / step) * step;
                double t = p.tick + (onGrid - p.tick) * set.quantise / 100.0;
                if (t < 0.0)
                    t = 0.0;
                if (t >= span)
                    continue;
                Note n;
                n.start = static_cast<Tick> (std::lround (t));
                n.length = std::max<Tick> (1, std::min<Tick> (static_cast<Tick> (std::lround (std::max (p.length, 30.0))), clip.length - n.start));
                n.pitch = static_cast<std::uint8_t> (p.pitch);
                n.velocity = static_cast<std::uint8_t> (std::clamp (p.velocity, 1, 127));
                clip.notes.push_back (n);
            }
            std::sort (clip.notes.begin(), clip.notes.end(), [] (const Note& a, const Note& b) {
                return a.start != b.start ? a.start < b.start : (a.pitch != b.pitch ? a.pitch < b.pitch : a.velocity > b.velocity);
            });
            // The same note twice at almost the same time: keep the louder (first after the sort).
            std::vector<Note> unique;
            for (const auto& n : clip.notes)
            {
                bool twice = false;
                for (auto it = unique.rbegin(); it != unique.rend() && n.start - it->start < 40; ++it)
                    twice = twice || it->pitch == n.pitch;
                if (! twice)
                    unique.push_back (n);
            }
            clip.notes = std::move (unique);
            return clip;
        }

        /** The section's pattern played over its bars with the song's chords: hits in song ticks. */
        std::vector<Hit> render (const SectionWork& s, const Part& part, const Clip& clip) const
        {
            std::vector<Hit> out;
            const auto rule = rules.at (part.source.channel);
            const int length = std::max<int> (1, static_cast<int> (clip.length / barTicks));
            for (std::size_t i = 0; i < s.bars.size(); ++i)
            {
                const int bar = s.bars[i];
                int runStart = bar;
                while (runStart > 0 && plan.barSection[static_cast<std::size_t> (runStart - 1)] == s.name)
                    --runStart;
                const int index = (bar - runStart) % length;
                const Tick from = barTicks * index, to = from + barTicks;
                for (const auto& n : clip.notes)
                {
                    if (n.start < from || n.start >= to)
                        continue;
                    const double tick = static_cast<double> (bar) * static_cast<double> (barTicks) + static_cast<double> (n.start - from);
                    int pitch = n.pitch;
                    if (! part.source.drums)
                    {
                        const auto& chord = chordAt (tick + kEarly);
                        if (! style_chord::plays (rule, chord))
                            continue;
                        const auto moved = style_chord::transpose (n.pitch, rule, chord);
                        if (! moved)
                            continue;
                        pitch = *moved;
                    }
                    out.push_back ({ tick, static_cast<double> (n.length), pitch, n.velocity });
                }
            }
            std::sort (out.begin(), out.end(), [] (const Hit& a, const Hit& b) { return a.tick < b.tick; });
            return out;
        }

        /** The hits of a part inside the bars of a section. */
        std::vector<Hit> heard (const SectionWork& s, const Part& part) const
        {
            std::vector<Hit> out;
            for (const int bar : s.bars)
            {
                const double from = static_cast<double> (bar) * static_cast<double> (barTicks);
                const double to = from + static_cast<double> (barTicks);
                const auto first = std::lower_bound (part.hits.begin(), part.hits.end(), from, [] (const Hit& h, double t) { return h.tick < t; });
                for (auto it = first; it != part.hits.end() && it->tick < to; ++it)
                    if (it->velocity >= (part.source.drums ? 15 : 20) && (part.source.drums || it->length >= kBeat / 16.0))
                        out.push_back (*it); // very weak notes are bleed: they don't count
            }
            return out;
        }

        PartScore score (const SectionWork& s, const Part& part, const Clip& clip) const;
        PartScore score (const SectionWork& s, const Part& part) const
        {
            return score (s, part, pattern (s, part, s.length, settingsOf (s, part)));
        }

        PartSettings settingsOf (const SectionWork& s, const Part& part) const
        {
            const auto it = s.settings.find (part.source.channel);
            return it != s.settings.end() ? it->second : PartSettings {};
        }
    };

    /** Matches two lists of hits by time (greedy, nearest first). Returns the matches and their mean distance. */
    std::pair<int, double> match (const std::vector<Hit>& a, const std::vector<Hit>& b, double tolerance, bool drumClasses)
    {
        std::vector<std::tuple<double, std::size_t, std::size_t>> pairs;
        std::size_t start = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            while (start < b.size() && b[start].tick < a[i].tick - tolerance)
                ++start;
            for (std::size_t j = start; j < b.size() && b[j].tick <= a[i].tick + tolerance; ++j)
                if (! drumClasses || drumClass (a[i].pitch) == drumClass (b[j].pitch))
                    pairs.emplace_back (std::abs (a[i].tick - b[j].tick), i, j);
        }
        std::sort (pairs.begin(), pairs.end());
        std::vector<bool> usedA (a.size()), usedB (b.size());
        int matches = 0;
        double sum = 0.0;
        for (const auto& [d, i, j] : pairs)
        {
            if (usedA[i] || usedB[j])
                continue;
            usedA[i] = usedB[j] = true;
            ++matches;
            sum += d;
        }
        return { matches, matches > 0 ? sum / matches : 0.0 };
    }

    PartScore Work::score (const SectionWork& s, const Part& part, const Clip& clip) const
    {
        PartScore out;
        out.channel = part.source.channel;
        out.name = kStylePartNames[std::clamp (part.source.channel, 9, 16) - 9];
        out.bars = static_cast<int> (s.bars.size());
        const auto played = render (s, part, clip);
        const auto original = heard (s, part);
        if (played.empty() && original.empty())
        {
            out.onsets = out.timing = out.notes = out.density = out.total = 1.0;
            return out;
        }

        const bool drums = part.source.drums;
        const double tolerance = kBeat / 8.0;
        const auto [matches, meanDistance] = match (played, original, tolerance, drums);
        const auto all = played.size() + original.size();
        out.onsets = all > 0 ? 2.0 * matches / static_cast<double> (all) : 1.0;
        out.timing = matches > 0 ? std::clamp (1.0 - meanDistance / tolerance, 0.0, 1.0) : 0.0;

        // Density: notes per bar, the smaller over the larger.
        {
            std::map<int, std::pair<int, int>> perBar;
            for (const auto& h : played)
                ++perBar[static_cast<int> (h.tick / static_cast<double> (barTicks))].first;
            for (const auto& h : original)
                ++perBar[static_cast<int> (h.tick / static_cast<double> (barTicks))].second;
            double sum = 0.0;
            for (const int bar : s.bars)
            {
                const auto [p, o] = perBar[bar];
                sum += std::max (p, o) == 0 ? 1.0 : static_cast<double> (std::min (p, o)) / std::max (p, o);
            }
            out.density = s.bars.empty() ? 1.0 : sum / static_cast<double> (s.bars.size());
        }

        // Notes: what sounds on each step. Drums: which drum classes hit in each 16th.
        // Bass: the lowest note's pitch class in each 8th. Others: the pitch classes in each 8th.
        {
            const double step = drums ? kBeat / 4.0 : kBeat / 2.0;
            double sum = 0.0;
            int steps = 0;
            for (const int bar : s.bars)
            {
                const double from = static_cast<double> (bar) * static_cast<double> (barTicks);
                const int count = static_cast<int> (std::lround (static_cast<double> (barTicks) / step));
                for (int k = 0; k < count; ++k)
                {
                    const double a = from + k * step, b = a + step, mid = a + step / 2.0;
                    const auto set = [&] (const std::vector<Hit>& hits) {
                        std::uint32_t bits = 0;
                        int lowest = 128;
                        const auto first = std::lower_bound (hits.begin(), hits.end(), a - 4.0 * kBeat, [] (const Hit& h, double t) { return h.tick < t; });
                        for (auto it = first; it != hits.end() && it->tick < b; ++it)
                        {
                            if (drums)
                            {
                                if (it->tick >= a)
                                    bits |= 1u << drumClass (it->pitch);
                            }
                            else if (it->tick <= mid && it->tick + it->length > mid - step / 4.0)
                            {
                                bits |= 1u << mod12 (it->pitch);
                                lowest = std::min (lowest, it->pitch);
                            }
                        }
                        if (! drums && part.source.channel == 11)
                            bits = lowest < 128 ? 1u << mod12 (lowest) : 0u;
                        return bits;
                    };
                    const auto p = set (played), o = set (original);
                    if ((p | o) == 0)
                        continue;
                    ++steps;
                    sum += static_cast<double> (std::popcount (p & o)) / std::popcount (p | o);
                }
            }
            out.notes = steps > 0 ? sum / steps : 1.0;
        }

        if (drums)
            out.total = 0.4 * out.onsets + 0.2 * out.notes + 0.2 * out.timing + 0.2 * out.density;
        else if (part.source.channel == 11)
            out.total = 0.3 * out.onsets + 0.4 * out.notes + 0.1 * out.timing + 0.2 * out.density;
        else
            out.total = 0.2 * out.onsets + 0.5 * out.notes + 0.05 * out.timing + 0.25 * out.density;
        return out;
    }

    SectionScore sectionScore (const Work& w, const SectionWork& s)
    {
        SectionScore out;
        out.section = s.name;
        out.bars = static_cast<int> (s.bars.size());
        if (s.bars.empty())
            return out;
        double sum = 0.0;
        for (const auto& part : w.parts)
        {
            out.parts.push_back (w.score (s, part));
            sum += out.parts.back().total;
        }
        out.total = out.parts.empty() ? 0.0 : sum / static_cast<double> (out.parts.size());
        return out;
    }

    Score scoreAll (const Work& w)
    {
        Score out;
        double sum = 0.0;
        int bars = 0;
        for (const auto& s : w.sections)
        {
            auto ss = sectionScore (w, s);
            if (ss.bars > 0)
            {
                sum += ss.total * ss.bars;
                bars += ss.bars;
            }
            out.sections.push_back (std::move (ss));
        }
        out.total = bars > 0 ? sum / bars : 0.0;
        return out;
    }

    std::string barsText (const std::vector<int>& bars)
    {
        std::string out;
        for (std::size_t i = 0; i < bars.size();)
        {
            std::size_t j = i;
            while (j + 1 < bars.size() && bars[j + 1] == bars[j] + 1)
                ++j;
            if (! out.empty())
                out += ", ";
            out += std::to_string (bars[i] + 1);
            if (j > i)
                out += "-" + std::to_string (bars[j] + 1);
            i = j + 1;
        }
        return out;
    }

    std::string describe (const std::string& knob, const PartSettings& set, int window, int length)
    {
        if (knob == "window")
            return "the pattern from bar" + std::string (length > 1 ? "s " : " ") + std::to_string (window + 1) + (length > 1 ? "-" + std::to_string (window + length) : "");
        if (knob == "consensus")
            return set.consensus == 0 ? "notes from one place only" : "only notes that " + std::to_string (set.consensus) + "% of the phrases play";
        if (knob == "quantise")
            return "quantise " + std::to_string (set.quantise) + "%";
        if (knob == "grid")
            return set.grid == 3 ? "triplet grid" : "16th grid";
        if (knob == "cleanup")
            return set.cleanup == 0 ? "keep weak notes" : (set.cleanup == 1 ? "drop very weak notes" : "drop weak and short notes");
        return knob;
    }
} // namespace

// ---- the bar lines ---------------------------------------------------------------

double Grid::tickAt (double seconds) const noexcept { return (seconds - firstDownbeat) / beatSeconds() * kBeat; }
double Grid::secondsAt (double tick) const noexcept { return firstDownbeat + tick / kBeat * beatSeconds(); }

std::vector<Onset> onsets (const analysis::Result& r)
{
    std::vector<Onset> out;
    if (const auto* drums = r.stem ("drums"); drums != nullptr && ! drums->notes.empty())
    {
        for (const auto& n : drums->notes)
        {
            const double v = std::clamp (n.velocity / 127.0, 0.1, 1.0);
            out.push_back ({ n.start, v * (isKick (n.pitch) ? 1.5 : (isSnare (n.pitch) ? 1.0 : 0.5)), isKick (n.pitch), false });
        }
    }
    else if (const auto* bass = r.stem ("bass"); bass != nullptr && ! bass->notes.empty())
    {
        for (const auto& n : bass->notes)
            out.push_back ({ n.start, std::clamp (n.velocity / 127.0, 0.1, 1.0), true, false });
    }
    else
    {
        for (const auto& s : r.stems)
            if (s.name != "vocals")
                for (const auto& n : s.notes)
                    out.push_back ({ n.start, std::clamp (n.velocity / 127.0, 0.1, 1.0), n.pitch < 52, false });
    }
    // The bass marks the downbeat too when there are drums.
    if (const auto* bass = r.stem ("bass"); bass != nullptr && r.stem ("drums") != nullptr && ! r.stem ("drums")->notes.empty())
        for (const auto& n : bass->notes)
            out.push_back ({ n.start, 0.0, true, false });
    for (std::size_t i = 1; i < r.chords.size(); ++i)
        if (! (r.chords[i].chord == r.chords[i - 1].chord))
            out.push_back ({ r.chords[i].start, 0.0, false, true });
    std::sort (out.begin(), out.end(), [] (const Onset& a, const Onset& b) { return a.seconds < b.seconds; });
    return out;
}

Grid findGrid (const std::vector<Onset>& hits, int beatsPerBar, double nearBpm, double bpm)
{
    Grid g;
    g.beatsPerBar = std::clamp (beatsPerBar, 1, 16);
    if (bpm > 0.0)
        g.bpm = clampTempo (bpm);
    double total = 0.0, firstHit = -1.0;
    for (const auto& h : hits)
        if (h.weight > 0.0)
        {
            total += h.weight;
            if (firstHit < 0.0)
                firstHit = h.seconds;
        }
    if (total <= 0.0)
        return g;

    // An envelope of the hits, 10 ms a frame, a little smoothed.
    constexpr double hop = 0.01;
    const double end = hits.back().seconds + 1.0;
    const auto frames = static_cast<std::size_t> (end / hop) + 2;
    std::vector<double> env (frames, 0.0), low (frames, 0.0);
    for (const auto& h : hits)
    {
        if (h.seconds < 0.0)
            continue;
        const auto i = static_cast<std::size_t> (std::lround (h.seconds / hop));
        for (int d = -2; d <= 2; ++d)
        {
            const auto k = static_cast<long> (i) + d;
            if (k < 0 || k >= static_cast<long> (frames))
                continue;
            const double shape = std::exp (-0.5 * d * d);
            env[static_cast<std::size_t> (k)] += h.weight * shape;
            low[static_cast<std::size_t> (k)] += ((h.low ? std::max (h.weight, 0.7) : 0.0) + (h.change ? 2.0 : 0.0)) * shape;
        }
    }
    const auto at = [&] (const std::vector<double>& v, double seconds) {
        const auto i = static_cast<long> (std::lround (seconds / hop));
        return i >= 0 && i < static_cast<long> (v.size()) ? v[static_cast<std::size_t> (i)] : 0.0;
    };

    // How well beats of a period and phase fall on the hits: the mean envelope on the beats.
    const auto comb = [&] (double period, double& bestPhase) {
        double best = -1.0;
        const int phases = std::max (1, static_cast<int> (period / hop));
        for (int p = 0; p < phases; ++p)
        {
            const double phase = p * hop;
            double sum = 0.0;
            int count = 0;
            for (double t = phase; t < end; t += period, ++count)
                sum += at (env, t);
            const double mean = count > 0 ? sum / count : 0.0;
            if (mean > best)
            {
                best = mean;
                bestPhase = phase;
            }
        }
        return best;
    };

    double phase = 0.0;
    if (bpm <= 0.0)
    {
        const double centre = nearBpm > 0.0 ? nearBpm : 110.0;
        double bestScore = -1.0, bestBpm = 120.0;
        for (double b = 60.0; b <= 200.0; b += 0.5)
        {
            double ph = 0.0;
            const double prior = std::exp (-0.5 * std::pow (std::log2 (b / centre) / 0.5, 2.0));
            const double s = comb (60.0 / b, ph) * (0.35 + 0.65 * prior);
            if (s > bestScore)
            {
                bestScore = s;
                bestBpm = b;
            }
        }
        double fine = bestBpm, fineScore = -1.0;
        for (double b = bestBpm - 0.6; b <= bestBpm + 0.6; b += 0.05)
        {
            double ph = 0.0;
            const double s = comb (60.0 / b, ph);
            if (s > fineScore)
            {
                fineScore = s;
                fine = b;
            }
        }
        g.bpm = std::round (fine * 10.0) / 10.0;
    }
    const double period = 60.0 / g.bpm;
    comb (period, phase);

    // The downbeat: the beat of the bar where kicks, bass notes and chord changes land most.
    int bestBeat = 0;
    double bestLow = -1.0;
    for (int m = 0; m < g.beatsPerBar; ++m)
    {
        double sum = 0.0;
        for (double t = phase + m * period; t < end; t += period * g.beatsPerBar)
            sum += at (low, t) + 0.25 * at (env, t);
        if (sum > bestLow + 1e-9)
        {
            bestLow = sum;
            bestBeat = m;
        }
    }
    const double bar = period * g.beatsPerBar;
    double first = phase + bestBeat * period;
    while (first - bar >= firstHit - period * 0.25)
        first -= bar;
    while (first + bar <= firstHit + period * 0.25)
        first += bar;
    if (first < 0.0)
        first += bar;
    g.firstDownbeat = first;

    // Confidence: the share of the hits within a 32nd note of an 8th note.
    double on = 0.0;
    for (const auto& h : hits)
    {
        if (h.weight <= 0.0)
            continue;
        const double pos = (h.seconds - first) / (period / 2.0);
        if (std::abs (pos - std::round (pos)) < 0.25)
            on += h.weight;
    }
    g.confidence = on / total;
    return g;
}

// ---- the parts ---------------------------------------------------------------------

std::vector<PartSource> partsFor (const analysis::Result& r, bool vocals)
{
    std::vector<PartSource> out;
    const auto withNotes = [&] (const char* name) -> const analysis::Stem* {
        const auto* s = r.stem (name);
        return s != nullptr && ! s->notes.empty() ? s : nullptr;
    };
    const auto add = [&] (const analysis::Stem& s, int channel) {
        PartSource p;
        p.stem = s.name;
        p.channel = channel;
        p.instrument = analysis::instrumentFor (s);
        p.drums = s.name == "drums" || p.instrument.drums;
        out.push_back (p);
    };
    if (const auto* s = withNotes ("drums"))
        add (*s, stylePartChannel (StylePart::rhythm2));
    if (const auto* s = withNotes ("bass"))
        add (*s, stylePartChannel (StylePart::bass));
    const auto* piano = withNotes ("piano");
    const auto* guitar = withNotes ("guitar");
    const auto* other = withNotes ("other");
    if (piano != nullptr)
        add (*piano, stylePartChannel (StylePart::chord1));
    if (guitar != nullptr)
        add (*guitar, stylePartChannel (StylePart::chord2));
    if (other != nullptr)
        add (*other, stylePartChannel (piano == nullptr ? StylePart::chord1 : (guitar == nullptr ? StylePart::chord2 : StylePart::pad)));
    if (vocals)
        if (const auto* s = withNotes ("vocals"))
            add (*s, stylePartChannel (StylePart::phrase1));
    if (out.empty())
        if (const auto* s = withNotes ("mix"))
            add (*s, stylePartChannel (StylePart::chord1));
    std::sort (out.begin(), out.end(), [] (const PartSource& a, const PartSource& b) { return a.channel < b.channel; });
    return out;
}

int toSourceChord (int pitch, const Chord& chord, bool bassPart)
{
    const int root = bassPart && chord.bass >= 0 ? chord.bass : chord.root;
    const int rel = mod12 (pitch - root);
    int mapped = rel;
    if (! (bassPart && chord.bass >= 0))
    {
        if (rel == 3 && has (chord, 3) && ! has (chord, 4))
            mapped = 4; // minor 3rd -> major 3rd
        else if (rel == 10 && has (chord, 10))
            mapped = 11; // minor 7th -> major 7th
        else if (rel == 6 && has (chord, 6) && ! has (chord, 7))
            mapped = 7; // flat 5th -> 5th
        else if (rel == 8 && has (chord, 8) && ! has (chord, 7))
            mapped = 7; // sharp 5th -> 5th
        else if (rel == 5 && (chord.type == ChordType::sus4 || chord.type == ChordType::seventhSus4))
            mapped = 4; // sus4 -> 3rd
        else if (rel == 2 && chord.type == ChordType::sus2)
            mapped = 4;
        else if (rel == 9 && chord.type == ChordType::diminished7)
            mapped = 11;
    }
    int shift = mod12 (-root);
    if (shift > 6)
        shift -= 12;
    return std::clamp (pitch + shift + (mapped - rel), 0, 127);
}

// ---- making it ---------------------------------------------------------------------

std::string barsOf (const Plan& plan, const std::string& section)
{
    std::vector<int> bars;
    for (int b = 0; b < static_cast<int> (plan.barSection.size()); ++b)
        if (plan.barSection[static_cast<std::size_t> (b)] == section)
            bars.push_back (b);
    return barsText (bars);
}

namespace
{
    void planSections (Work& w)
    {
        auto& plan = w.plan;
        const int bars = plan.bars;
        plan.phraseBars = bars >= 24 ? 4 : (bars >= 4 ? 2 : 1);
        const int pb = plan.phraseBars;
        const int phraseCount = (bars + pb - 1) / pb;
        std::vector<std::vector<int>> phrases;
        for (int p = 0; p < phraseCount; ++p)
        {
            std::vector<int> b;
            for (int i = p * pb; i < std::min (bars, (p + 1) * pb); ++i)
                b.push_back (i);
            phrases.push_back (b);
        }

        const bool ends = phraseCount >= 5;
        const int firstBody = ends ? 1 : 0;
        const int lastBody = ends ? phraseCount - 2 : phraseCount - 1;

        // Group the body phrases: join the two nearest groups while they are near, or while there are more than four.
        std::vector<std::vector<int>> groups; // of phrase numbers
        for (int p = firstBody; p <= lastBody; ++p)
            groups.push_back ({ p });
        const auto phraseInfo = [&] (const std::vector<int>& ps) {
            std::vector<int> b;
            for (const int p : ps)
                b.insert (b.end(), phrases[static_cast<std::size_t> (p)].begin(), phrases[static_cast<std::size_t> (p)].end());
            return average (w.barInfo, b);
        };
        while (groups.size() > 1)
        {
            double best = 1e9;
            std::size_t bi = 0, bj = 1;
            for (std::size_t i = 0; i < groups.size(); ++i)
                for (std::size_t j = i + 1; j < groups.size(); ++j)
                {
                    const double d = distance (phraseInfo (groups[i]), phraseInfo (groups[j]));
                    if (d < best)
                    {
                        best = d;
                        bi = i;
                        bj = j;
                    }
                }
            if (best > 0.15 && groups.size() <= 4)
                break;
            groups[bi].insert (groups[bi].end(), groups[bj].begin(), groups[bj].end());
            groups.erase (groups.begin() + static_cast<long> (bj));
        }
        // Quietest first: Main A .. Main D.
        std::stable_sort (groups.begin(), groups.end(), [&] (const auto& a, const auto& b) { return phraseInfo (a).energy < phraseInfo (b).energy; });
        std::vector<int> groupOfPhrase (static_cast<std::size_t> (phraseCount), -3);
        for (std::size_t g = 0; g < groups.size(); ++g)
            for (const int p : groups[g])
                groupOfPhrase[static_cast<std::size_t> (p)] = static_cast<int> (g);
        if (ends)
        {
            groupOfPhrase.front() = -1;
            groupOfPhrase.back() = -2;
        }

        plan.barSection.assign (static_cast<std::size_t> (bars), {});
        plan.barGroup.assign (static_cast<std::size_t> (bars), 0);
        for (int p = 0; p < phraseCount; ++p)
        {
            const int g = groupOfPhrase[static_cast<std::size_t> (p)];
            for (const int b : phrases[static_cast<std::size_t> (p)])
            {
                plan.barGroup[static_cast<std::size_t> (b)] = g;
                plan.barSection[static_cast<std::size_t> (b)] = g == -1 ? "Intro A" : (g == -2 ? "Ending A" : mainName (g));
            }
        }

        // Sections and their source bars.
        std::map<std::string, SectionWork> made;
        const auto section = [&] (const std::string& name, int group) -> SectionWork& {
            auto& s = made[name];
            s.name = name;
            s.group = group;
            return s;
        };

        // The typical bar of each group (without the phrase ends), to find fills.
        std::map<int, BarInfo> typical;
        for (std::size_t g = 0; g < groups.size(); ++g)
        {
            std::vector<int> b;
            for (const int p : groups[g])
            {
                const auto& ph = phrases[static_cast<std::size_t> (p)];
                for (std::size_t i = 0; i + 1 < ph.size(); ++i)
                    b.push_back (ph[i]);
                if (ph.size() == 1)
                    b.push_back (ph[0]);
            }
            typical[static_cast<int> (g)] = average (w.barInfo, b);
        }

        for (int p = firstBody; p <= lastBody; ++p)
        {
            const int g = groupOfPhrase[static_cast<std::size_t> (p)];
            const auto& ph = phrases[static_cast<std::size_t> (p)];
            auto& main = section (mainName (g), g);
            main.main = true;
            main.runs.push_back (ph);
            // A fill: the last bar of a phrase, when the next phrase is another section, or the drums break the pattern there.
            if (pb >= 2 && ph.size() == static_cast<std::size_t> (pb))
            {
                const int last = ph.back();
                const int next = p + 1 < phraseCount ? groupOfPhrase[static_cast<std::size_t> (p + 1)] : -2;
                const auto& info = w.barInfo[static_cast<std::size_t> (last)];
                const bool breaks = 1.0 - cosine (info.drums, typical[g].drums) > 0.35;
                if (next != g || breaks)
                {
                    const int target = next >= 0 ? next : g;
                    const auto name = fillName (target);
                    auto& fill = section (name, target);
                    fill.length = 1;
                    fill.lengths = { 1 };
                    fill.bars.push_back (last);
                    fill.sourceBars.push_back (last);
                    fill.runs.push_back ({ last });
                    plan.barSection[static_cast<std::size_t> (last)] = name;
                    for (std::size_t i = 0; i + 1 < ph.size(); ++i)
                        main.sourceBars.push_back (ph[i]);
                    for (std::size_t i = 0; i + 1 < ph.size(); ++i)
                        main.bars.push_back (ph[i]);
                    continue;
                }
            }
            main.sourceBars.insert (main.sourceBars.end(), ph.begin(), ph.end());
            main.bars.insert (main.bars.end(), ph.begin(), ph.end());
        }
        // Every main gets a fill: from its own phrase ends when the song has none for it.
        for (std::size_t g = 0; g < groups.size(); ++g)
        {
            const auto name = fillName (static_cast<int> (g));
            if (made.count (name) != 0)
                continue;
            auto& fill = section (name, static_cast<int> (g));
            fill.length = 1;
            fill.lengths = { 1 };
            for (const int p : groups[g])
            {
                const int last = phrases[static_cast<std::size_t> (p)].back();
                fill.sourceBars.push_back (last);
                fill.runs.push_back ({ last });
            }
        }
        if (ends)
            for (const int p : { 0, phraseCount - 1 })
            {
                const auto& ph = phrases[static_cast<std::size_t> (p)];
                auto& s = section (p == 0 ? "Intro A" : "Ending A", p == 0 ? -1 : -2);
                s.length = static_cast<int> (ph.size());
                s.lengths = { s.length };
                s.bars = ph;
                s.sourceBars = ph;
                s.runs = { ph };
            }
        for (auto& [name, s] : made)
        {
            if (s.main)
            {
                for (const int l : { 1, 2, 4 })
                    if (l <= pb)
                        s.lengths.push_back (l);
                s.length = std::min (2, pb);
            }
            std::sort (s.bars.begin(), s.bars.end());
            std::sort (s.sourceBars.begin(), s.sourceBars.end());
            w.sections.push_back (s);
        }
        std::sort (w.sections.begin(), w.sections.end(), [] (const SectionWork& a, const SectionWork& b) { return sectionOrder (a.name) < sectionOrder (b.name); });
        for (const auto& s : w.sections)
            plan.sections.push_back (s.name);
    }

    Style buildStyle (const Work& w, const std::string& name)
    {
        Style style;
        style.name = name;
        style.format = 2;
        style.tempo = std::round (w.grid.bpm);
        style.timeSignature.numerator = w.grid.beatsPerBar;
        style.timeSignature.denominator = 4;
        for (const auto& part : w.parts)
        {
            const auto status = [&] (int kind) { return static_cast<std::uint8_t> (kind | (part.source.channel - 1)); };
            const int msb = part.source.drums ? 127 : 0;
            const int program = part.source.drums ? 0 : std::clamp (part.source.instrument.program, 0, 127);
            style.setup.push_back ({ 0, { status (0xb0), 0, static_cast<std::uint8_t> (msb) } });
            style.setup.push_back ({ 0, { status (0xb0), 32, 0 } });
            style.setup.push_back ({ 0, { status (0xc0), static_cast<std::uint8_t> (program) } });
            style.setup.push_back ({ 0, { status (0xb0), 7, 100 } });
            style.setup.push_back ({ 0, { status (0xb0), 10, 64 } });
            style.setup.push_back ({ 0, { status (0xb0), 91, static_cast<std::uint8_t> (part.source.drums ? 20 : 40) } });
        }
        StyleRuleSet rules;
        for (const auto& s : w.sections)
        {
            StyleSection out;
            out.name = s.name;
            out.length = w.barTicks * s.length;
            for (const auto& part : w.parts)
            {
                auto clip = w.pattern (s, part, s.length, w.settingsOf (s, part));
                if (clip.notes.empty())
                    continue;
                clip.name = s.name;
                out.parts[part.source.channel] = std::move (clip);
            }
            if (out.parts.empty())
                continue;
            rules.sections.push_back (s.name);
            style.sections.push_back (std::move (out));
        }
        for (const auto& part : w.parts)
            rules.rules.push_back (w.rules.at (part.source.channel));
        if (! rules.sections.empty())
            style.ruleSets.push_back (std::move (rules));
        return style;
    }
} // namespace

Made make (const analysis::Result& result, const Options& options)
{
    Made made;
    made.source = result.source;
    Work w (result);
    const auto report = [&] (double progress, const std::string& step) { return ! options.progress || options.progress (progress, step); };
    report (0.0, "Finding the beat");

    // 1. The bar lines.
    w.grid = findGrid (onsets (result), options.beatsPerBar, 0.0, options.bpm);
    w.barTicks = kTicksPerQuarter * w.grid.beatsPerBar;
    if (w.grid.confidence < 0.6)
        made.warnings.push_back ("The beat is not clear (" + std::to_string (static_cast<int> (std::lround (w.grid.confidence * 100))) + "% of the hits fall on it). If the tempo changes in the song, the bars drift: set the tempo by hand.");

    // 2. The parts as hits in song time.
    for (const auto& source : partsFor (result, options.vocals))
    {
        Part part;
        part.source = source;
        if (const auto* stem = result.stem (source.stem))
            for (const auto& n : stem->notes)
            {
                const double tick = w.grid.tickAt (n.start);
                if (tick < -kBeat / 8.0)
                    continue;
                part.hits.push_back ({ std::max (0.0, tick), std::max (1.0, n.length / w.grid.beatSeconds() * kBeat), n.pitch, n.velocity });
            }
        std::sort (part.hits.begin(), part.hits.end(), [] (const Hit& a, const Hit& b) { return a.tick < b.tick; });
        if (! part.hits.empty())
            w.parts.push_back (std::move (part));
    }
    if (w.parts.empty())
        throw std::runtime_error ("The analysis found no notes to make a style from. Analyse the audio with notes on first.");
    for (const auto& part : w.parts)
    {
        w.rules[part.source.channel] = ruleFor (part.source);
        made.parts.push_back (part.source);
    }

    double lastTick = 0.0;
    for (const auto& part : w.parts)
        for (const auto& h : part.hits)
            lastTick = std::max (lastTick, h.tick + std::min (h.length, kBeat));
    w.plan.bars = std::max (1, static_cast<int> (std::ceil ((lastTick - kBeat / 2.0) / static_cast<double> (w.barTicks))));

    // The chord of every beat: from the analysis, else guessed from the notes of each bar.
    {
        const int beats = w.plan.bars * w.grid.beatsPerBar;
        w.plan.beatChords.assign (static_cast<std::size_t> (beats), Chord {});
        std::vector<bool> known (static_cast<std::size_t> (beats), false);
        for (int b = 0; b < beats; ++b)
        {
            const double t = w.grid.secondsAt ((b + 0.5) * kBeat);
            for (const auto& c : result.chords)
                if (c.start <= t && t < c.end && c.chord.type != ChordType::cancel)
                {
                    w.plan.beatChords[static_cast<std::size_t> (b)] = c.chord;
                    known[static_cast<std::size_t> (b)] = true;
                    break;
                }
        }
        const bool any = std::find (known.begin(), known.end(), true) != known.end();
        if (! any)
        {
            for (int bar = 0; bar < w.plan.bars; ++bar)
            {
                std::array<double, 12> weight {};
                std::array<double, 12> bassWeight {};
                const double from = bar * static_cast<double> (w.barTicks), to = from + static_cast<double> (w.barTicks);
                for (const auto& part : w.parts)
                {
                    if (part.source.drums)
                        continue;
                    for (const auto& h : part.hits)
                        if (h.tick >= from && h.tick < to)
                        {
                            const double d = std::min (h.length, to - h.tick) * h.velocity / 127.0;
                            weight[static_cast<std::size_t> (mod12 (h.pitch))] += d;
                            if (part.source.channel == 11)
                                bassWeight[static_cast<std::size_t> (mod12 (h.pitch))] += d;
                        }
                }
                const auto bassNote = static_cast<int> (std::max_element (bassWeight.begin(), bassWeight.end()) - bassWeight.begin());
                const auto chord = style_chord::guess (std::span<const double, 12> (weight), bassWeight[static_cast<std::size_t> (bassNote)] > 0.0 ? bassNote : -1);
                if (chord)
                    for (int i = 0; i < w.grid.beatsPerBar; ++i)
                    {
                        w.plan.beatChords[static_cast<std::size_t> (bar * w.grid.beatsPerBar + i)] = *chord;
                        known[static_cast<std::size_t> (bar * w.grid.beatsPerBar + i)] = true;
                    }
            }
            made.warnings.push_back ("The analysis has no chords: the chords were guessed from the notes, bar by bar.");
        }
        // Beats with no chord keep the chord before them (or after them, at the start).
        int last = -1;
        for (int b = 0; b < beats; ++b)
            if (known[static_cast<std::size_t> (b)])
                last = b;
            else if (last >= 0)
                w.plan.beatChords[static_cast<std::size_t> (b)] = w.plan.beatChords[static_cast<std::size_t> (last)];
        const auto firstKnown = std::find (known.begin(), known.end(), true);
        if (firstKnown != known.end())
            for (auto i = known.begin(); i != firstKnown; ++i)
                w.plan.beatChords[static_cast<std::size_t> (i - known.begin())] = w.plan.beatChords[static_cast<std::size_t> (firstKnown - known.begin())];
    }

    // 3. Bars, phrases and sections.
    w.barInfo.assign (static_cast<std::size_t> (w.plan.bars), {});
    for (auto& b : w.barInfo)
        b.notes.assign (w.parts.size(), 0.0);
    for (std::size_t p = 0; p < w.parts.size(); ++p)
        for (const auto& h : w.parts[p].hits)
        {
            const auto bar = static_cast<int> (h.tick / static_cast<double> (w.barTicks));
            if (bar < 0 || bar >= w.plan.bars)
                continue;
            auto& info = w.barInfo[static_cast<std::size_t> (bar)];
            const double v = h.velocity / 127.0;
            info.energy += v;
            if (w.parts[p].source.drums)
            {
                const double pos = h.tick - bar * static_cast<double> (w.barTicks);
                const int step = static_cast<int> (std::lround (pos / (static_cast<double> (w.barTicks) / 16.0))) % 16;
                info.drums[static_cast<std::size_t> (drumClass (h.pitch) * 16 + step)] += v;
            }
            else
                info.notes[p] += 1.0;
        }
    planSections (w);
    if (! report (0.05, "Comparing"))
        throw std::runtime_error ("Stopped.");

    // 4. Compare and refine.
    for (auto& s : w.sections)
        for (const auto& part : w.parts)
            s.settings[part.source.channel] = PartSettings {};
    if (options.mainBars > 0)
        for (auto& s : w.sections)
            if (s.main)
                s.length = std::clamp (options.mainBars, 1, w.plan.phraseBars);

    made.first = scoreAll (w);
    double current = made.first.total;
    bool stopped = false;
    for (int round = 1; round <= options.maxRounds && ! stopped; ++round)
    {
        bool better = false;
        for (std::size_t si = 0; si < w.sections.size() && ! stopped; ++si)
        {
            auto& s = w.sections[si];
            if (s.bars.empty())
                continue;
            const double progress = 0.05 + 0.9 * ((round - 1) + static_cast<double> (si) / static_cast<double> (w.sections.size())) / options.maxRounds;
            if (! report (progress, "Round " + std::to_string (round) + ": " + s.name))
            {
                stopped = true;
                break;
            }

            // The pattern length of a main section.
            if (s.main && options.mainBars <= 0 && s.lengths.size() > 1)
            {
                double before = sectionScore (w, s).total;
                for (const int l : s.lengths)
                {
                    if (l == s.length)
                        continue;
                    const int was = s.length;
                    s.length = l;
                    ++w.tried;
                    const double after = sectionScore (w, s).total;
                    if (after > before + 1e-4)
                    {
                        const double total = scoreAll (w).total;
                        made.steps.push_back ({ round, s.name + ": " + std::to_string (l) + "-bar pattern", current, total });
                        current = total;
                        before = after;
                        better = true;
                    }
                    else
                        s.length = was;
                }
            }

            for (const auto& part : w.parts)
            {
                auto& set = s.settings[part.source.channel];
                double before = w.score (s, part).total;
                const auto windows = static_cast<int> (w.windows (s, s.length).size());
                const auto tryKnob = [&] (const std::string& knob, auto values, auto field) {
                    for (const int v : values)
                    {
                        if (set.*field == v)
                            continue;
                        const auto was = set;
                        set.*field = v;
                        ++w.tried;
                        const double after = w.score (s, part).total;
                        if (after > before + 1e-4)
                        {
                            const double total = scoreAll (w).total;
                            int windowBar = 0;
                            if (knob == "window")
                            {
                                const auto all = w.windows (s, s.length);
                                windowBar = all.empty() ? 0 : all[static_cast<std::size_t> (std::clamp (v, 0, static_cast<int> (all.size()) - 1))];
                            }
                            made.steps.push_back ({ round, s.name + ", " + std::string (kStylePartNames[part.source.channel - 9]) + ": " + describe (knob, set, windowBar, s.length), current, total });
                            current = total;
                            before = after;
                            better = true;
                        }
                        else
                            set = was;
                    }
                };
                std::vector<int> windowValues;
                for (int i = 0; i < windows; ++i)
                    windowValues.push_back (i);
                if (set.consensus == 0)
                    tryKnob ("window", windowValues, &PartSettings::window);
                if (windows > 1)
                    tryKnob ("consensus", std::vector<int> { 0, 50, 34 }, &PartSettings::consensus);
                tryKnob ("quantise", std::vector<int> { 0, 50, 100 }, &PartSettings::quantise);
                tryKnob ("grid", std::vector<int> { 4, 3 }, &PartSettings::grid);
                tryKnob ("cleanup", std::vector<int> { 0, 1, 2 }, &PartSettings::cleanup);
            }
        }
        if (! better)
            break;
    }
    if (! stopped)
        report (0.97, "Writing the style");

    made.best = scoreAll (w);
    made.tried = w.tried;
    made.grid = w.grid;
    made.plan = w.plan;
    auto name = options.name;
    if (name.empty())
        name = std::filesystem::path (result.source).stem().string();
    if (name.empty())
        name = "Style from Audio";
    made.style = buildStyle (w, name);
    return made;
}

analysis::Result resultFromSong (const Song& song, const std::string& source)
{
    analysis::Result r;
    r.source = source;
    std::map<std::string, analysis::Stem> stems;
    const auto roles = arranging::guessRoles (song);
    for (const auto& t : song.tracks)
    {
        const auto role = roles.find (t.id);
        if (role == roles.end() || t.mixer.mute)
            continue;
        std::string name = "other";
        switch (role->second)
        {
            case arranging::Role::drums: name = "drums"; break;
            case arranging::Role::bass: name = "bass"; break;
            case arranging::Role::melody: name = "vocals"; break;
            default:
                if (t.sound.program >= 0 && t.sound.program < 8)
                    name = "piano";
                else if (t.sound.program >= 24 && t.sound.program < 32)
                    name = "guitar";
                break;
        }
        auto& stem = stems[name];
        stem.name = name;
        stem.hasNotes = name != "drums";
        for (const auto& n : arranging::playedNotes (t))
        {
            const double start = song.tempo.secondsAt (static_cast<double> (n.on));
            stem.notes.push_back ({ start, song.tempo.secondsAt (static_cast<double> (n.off)) - start, n.pitch, n.velocity });
        }
        for (const auto& m : t.marks)
            if (m.kind == ScoreMark::Kind::chord)
                if (const auto c = style_chord::parse (m.text))
                    r.chords.push_back ({ song.tempo.secondsAt (static_cast<double> (m.at)), 0.0, *c });
    }
    for (auto& [name, stem] : stems)
    {
        std::sort (stem.notes.begin(), stem.notes.end(), [] (const auto& a, const auto& b) { return a.start < b.start; });
        if (! stem.notes.empty())
            r.stems.push_back (std::move (stem));
    }
    std::sort (r.chords.begin(), r.chords.end(), [] (const auto& a, const auto& b) { return a.start < b.start; });
    r.chords.erase (std::unique (r.chords.begin(), r.chords.end(), [] (const auto& a, const auto& b) { return std::abs (a.start - b.start) < 1e-6; }), r.chords.end());
    double end = 0.0;
    for (const auto& stem : r.stems)
        for (const auto& n : stem.notes)
            end = std::max (end, n.start + n.length);
    for (std::size_t i = 0; i < r.chords.size(); ++i)
        r.chords[i].end = i + 1 < r.chords.size() ? r.chords[i + 1].start : std::max (end, r.chords[i].start + 1.0);
    return r;
}

Score compare (const Style& style, const analysis::Result& result, const Grid& grid, const Plan& plan, const std::vector<PartSource>& sources)
{
    Work w (result);
    w.grid = grid;
    w.plan = plan;
    w.barTicks = kTicksPerQuarter * grid.beatsPerBar;
    for (const auto& source : sources)
    {
        Part part;
        part.source = source;
        if (const auto* stem = result.stem (source.stem))
            for (const auto& n : stem->notes)
            {
                const double tick = grid.tickAt (n.start);
                if (tick >= -kBeat / 8.0)
                    part.hits.push_back ({ std::max (0.0, tick), std::max (1.0, n.length / grid.beatSeconds() * kBeat), n.pitch, n.velocity });
            }
        std::sort (part.hits.begin(), part.hits.end(), [] (const Hit& a, const Hit& b) { return a.tick < b.tick; });
        w.parts.push_back (std::move (part));
        w.rules[source.channel] = ruleFor (source);
    }
    Score out;
    double sum = 0.0;
    int bars = 0;
    for (const auto& name : plan.sections)
    {
        SectionWork s;
        s.name = name;
        for (int b = 0; b < static_cast<int> (plan.barSection.size()); ++b)
            if (plan.barSection[static_cast<std::size_t> (b)] == name)
                s.bars.push_back (b);
        SectionScore ss;
        ss.section = name;
        ss.bars = static_cast<int> (s.bars.size());
        const auto* section = style.section (name);
        if (s.bars.empty() || section == nullptr)
        {
            out.sections.push_back (ss);
            continue;
        }
        for (const auto& part : w.parts)
            if (const auto* rule = style.rule (name, part.source.channel))
                w.rules[part.source.channel] = *rule;
        double partSum = 0.0;
        for (const auto& part : w.parts)
        {
            const auto it = section->parts.find (part.source.channel);
            Clip clip;
            clip.length = section->length;
            if (it != section->parts.end())
                clip.notes = it->second.notes;
            ss.parts.push_back (w.score (s, part, clip));
            partSum += ss.parts.back().total;
        }
        ss.total = ss.parts.empty() ? 0.0 : partSum / static_cast<double> (ss.parts.size());
        sum += ss.total * ss.bars;
        bars += ss.bars;
        out.sections.push_back (std::move (ss));
    }
    out.total = bars > 0 ? sum / bars : 0.0;
    return out;
}

Song preview (const Made& made, Listen listen)
{
    Song song;
    song.title = made.style.name + " (preview)";
    song.tempo = TempoMap (made.grid.bpm);
    song.timeSignature.numerator = made.grid.beatsPerBar;
    song.timeSignature.denominator = 4;
    song.metronome.enabled = false;
    const Tick barTicks = song.timeSignature.ticksPerBar();
    const Tick total = barTicks * std::max (1, made.plan.bars);

    if (listen != Listen::style && ! made.source.empty())
    {
        Track t;
        t.id = song.nextId++;
        t.name = "Recording";
        t.colour = "amber";
        t.output.kind = TrackOutput::Kind::audio;
        t.mixer.volume = listen == Listen::both ? 80 : 100;
        AudioClip clip;
        clip.id = song.nextId++;
        clip.name = std::filesystem::path (made.source).filename().string();
        clip.file = made.source;
        clip.start = 0;
        clip.offset = made.grid.firstDownbeat;
        clip.length = total;
        t.audio.push_back (std::make_shared<const AudioClip> (std::move (clip)));
        song.tracks.push_back (std::move (t));
    }
    if (listen == Listen::original)
        return song;

    // The style played bar by bar: each bar plays the bar of its section's pattern, moved to the chord of each beat.
    int colour = 0;
    int nextChannel = 1;
    for (const auto& source : made.parts)
    {
        Clip clip;
        clip.id = song.nextId++;
        clip.name = made.style.name;
        clip.start = 0;
        clip.length = total;
        for (int bar = 0; bar < made.plan.bars; ++bar)
        {
            const auto& name = made.plan.barSection[static_cast<std::size_t> (bar)];
            const auto* section = made.style.section (name);
            if (section == nullptr)
                continue;
            const auto it = section->parts.find (source.channel);
            if (it == section->parts.end())
                continue;
            const auto* rule = made.style.rule (name, source.channel);
            int runStart = bar;
            while (runStart > 0 && made.plan.barSection[static_cast<std::size_t> (runStart - 1)] == name)
                --runStart;
            const int length = std::max<int> (1, static_cast<int> (section->length / barTicks));
            const Tick from = barTicks * ((bar - runStart) % length), to = from + barTicks;
            for (const auto& n : it->second.notes)
            {
                if (n.start < from || n.start >= to)
                    continue;
                Note copy = n;
                copy.start = barTicks * bar + (n.start - from);
                copy.length = std::min (n.length, barTicks * (bar + 1) - copy.start + barTicks);
                if (! source.drums && rule != nullptr)
                {
                    const auto beat = static_cast<std::size_t> (std::clamp<long> ((copy.start + static_cast<Tick> (kEarly)) / kTicksPerQuarter, 0, static_cast<long> (made.plan.beatChords.size()) - 1));
                    const auto& chord = made.plan.beatChords.empty() ? Chord {} : made.plan.beatChords[beat];
                    if (! style_chord::plays (*rule, chord))
                        continue;
                    const auto moved = style_chord::transpose (n.pitch, *rule, chord);
                    if (! moved)
                        continue;
                    copy.pitch = static_cast<std::uint8_t> (*moved);
                }
                clip.notes.push_back (copy);
            }
        }
        std::sort (clip.notes.begin(), clip.notes.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });

        Track t;
        t.id = song.nextId++;
        t.name = kStylePartNames[std::clamp (source.channel, 9, 16) - 9];
        t.colour = kTrackColours[static_cast<std::size_t> (colour++) % std::size (kTrackColours)];
        t.output.kind = TrackOutput::Kind::soundFont;
        if (source.drums)
        {
            t.output.channel = 10;
            t.sound.program = 0;
        }
        else
        {
            if (nextChannel == 10)
                ++nextChannel;
            t.output.channel = std::min (16, nextChannel++);
            t.sound.bankMsb = 0;
            t.sound.program = std::clamp (source.instrument.program, 0, 127);
        }
        t.clips.push_back (std::make_shared<const Clip> (std::move (clip)));
        song.tracks.push_back (std::move (t));
    }

    // Chord symbols on the first style track, for the Score and the chord lane.
    for (auto& t : song.tracks)
    {
        if (t.output.kind == TrackOutput::Kind::audio)
            continue;
        for (std::size_t b = 0; b < made.plan.beatChords.size(); ++b)
            if (b == 0 || ! (made.plan.beatChords[b] == made.plan.beatChords[b - 1]))
                t.marks.push_back ({ ScoreMark::Kind::chord, static_cast<Tick> (b) * kTicksPerQuarter, style_chord::name (made.plan.beatChords[b]) });
        break;
    }
    return song;
}

} // namespace arranger::style_from_audio

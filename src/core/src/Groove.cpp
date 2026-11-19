#include "arranger/core/Groove.h"

#include "arranger/core/Arranging.h"
#include "arranger/core/SongEdit.h"
#include "arranger/core/StyleFromAudio.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <numeric>
#include <regex>
#include <stdexcept>

namespace arranger::groove
{

using json = nlohmann::json;

namespace
{
    constexpr double kBeat = static_cast<double> (kTicksPerQuarter);

    double msPerTick (double bpm) { return secondsPerTick (bpm > 0.0 ? bpm : 120.0) * 1000.0; }

    double median (std::vector<double> v)
    {
        if (v.empty())
            return 0.0;
        std::sort (v.begin(), v.end());
        const auto n = v.size();
        return n % 2 == 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
    }

    double positiveMod (double x, double m)
    {
        const double r = std::fmod (x, m);
        return r < 0.0 ? r + m : r;
    }

    /** The lane to read for a note: its own lane when the groove has it, else `all`. */
    const std::vector<Step>& laneFor (const Groove& g, Lane l)
    {
        return g.hasLane (l) ? g.lane (l) : g.lane (Lane::all);
    }

    /** Offsets of every step: steps not played take the offset of the played steps around them (in a circle). */
    std::vector<double> filledOffsets (const std::vector<Step>& steps)
    {
        const int n = static_cast<int> (steps.size());
        std::vector<double> out (static_cast<std::size_t> (n), 0.0);
        std::vector<int> played;
        for (int i = 0; i < n; ++i)
            if (steps[static_cast<std::size_t> (i)].played())
                played.push_back (i);
        if (played.empty())
            return out;
        for (int i = 0; i < n; ++i)
        {
            if (steps[static_cast<std::size_t> (i)].played())
            {
                out[static_cast<std::size_t> (i)] = steps[static_cast<std::size_t> (i)].offset;
                continue;
            }
            // The played steps before and after, around the pattern.
            int before = -1, after = -1;
            for (int d = 1; d <= n && before < 0; ++d)
                if (steps[static_cast<std::size_t> (((i - d) % n + n) % n)].played())
                    before = d;
            for (int d = 1; d <= n && after < 0; ++d)
                if (steps[static_cast<std::size_t> ((i + d) % n)].played())
                    after = d;
            const double a = steps[static_cast<std::size_t> (((i - before) % n + n) % n)].offset;
            const double b = steps[static_cast<std::size_t> ((i + after) % n)].offset;
            out[static_cast<std::size_t> (i)] = a + (b - a) * static_cast<double> (before) / static_cast<double> (before + after);
        }
        return out;
    }

    /** Relative loudness of every step (1 = the mean of the played steps); steps not played take their neighbours'. */
    std::vector<double> filledAccents (const std::vector<Step>& steps)
    {
        const int n = static_cast<int> (steps.size());
        std::vector<double> out (static_cast<std::size_t> (n), 1.0);
        double sum = 0.0;
        int count = 0;
        for (const auto& s : steps)
            if (s.played())
            {
                sum += s.velocity;
                ++count;
            }
        if (count == 0 || sum <= 0.0)
            return out;
        const double mean = sum / count;
        std::vector<Step> rel = steps;
        for (auto& s : rel)
            s.offset = s.played() ? s.velocity / mean : 0.0;
        return filledOffsets (rel); // the same circular fill, on the relative loudness
    }

    struct Prepared
    {
        double grid = 240.0, length = 3840.0;
        std::vector<double> offsets, accents;
    };

    Prepared prepare (const Groove& g, Lane l)
    {
        Prepared p;
        p.grid = static_cast<double> (g.grid);
        p.length = static_cast<double> (g.length);
        const auto& steps = laneFor (g, l);
        p.offsets = filledOffsets (steps);
        p.accents = filledAccents (steps);
        // A warp must keep the order of the notes: neighbouring offsets differ by less than a step.
        const int n = static_cast<int> (p.offsets.size());
        for (int i = 0; i + 1 < n; ++i)
        {
            auto& next = p.offsets[static_cast<std::size_t> (i + 1)];
            next = std::clamp (next, p.offsets[static_cast<std::size_t> (i)] - p.grid * 0.9, p.offsets[static_cast<std::size_t> (i)] + p.grid * 0.9);
        }
        return p;
    }

    double offsetIn (const Prepared& p, double songTick)
    {
        const int n = static_cast<int> (p.offsets.size());
        if (n == 0 || p.grid <= 0.0)
            return 0.0;
        const double pos = positiveMod (songTick, p.length);
        const int k = std::min (n - 1, static_cast<int> (pos / p.grid));
        const double frac = (pos - k * p.grid) / p.grid;
        const double a = p.offsets[static_cast<std::size_t> (k)];
        const double b = p.offsets[static_cast<std::size_t> ((k + 1) % n)];
        return a + (b - a) * frac;
    }

    double accentIn (const Prepared& p, double songTick)
    {
        const int n = static_cast<int> (p.accents.size());
        if (n == 0 || p.grid <= 0.0)
            return 1.0;
        const double pos = positiveMod (songTick, p.length);
        const int k = std::min (n - 1, static_cast<int> (pos / p.grid));
        const double frac = (pos - k * p.grid) / p.grid;
        const double a = p.accents[static_cast<std::size_t> (k)];
        const double b = p.accents[static_cast<std::size_t> ((k + 1) % n)];
        if (frac <= 0.25)
            return a;
        if (frac >= 0.75)
            return b;
        return a + (b - a) * (frac - 0.25) / 0.5;
    }

    std::string lower (std::string s)
    {
        for (auto& c : s)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return s;
    }

    bool has (const std::string& text, std::initializer_list<const char*> words)
    {
        for (const auto* w : words)
            if (text.find (w) != std::string::npos)
                return true;
        return false;
    }

    std::string formatMs (double ms)
    {
        return std::to_string (static_cast<int> (std::lround (std::abs (ms)))) + " ms";
    }

    std::string gridName (Tick grid)
    {
        switch (grid)
        {
            case kTicksPerQuarter: return "1/4";
            case kTicksPerQuarter / 2: return "1/8";
            case kTicksPerQuarter / 3: return "1/8T";
            case kTicksPerQuarter / 4: return "1/16";
            case kTicksPerQuarter / 6: return "1/16T";
            case kTicksPerQuarter / 8: return "1/32";
            default: return std::to_string (grid) + " ticks";
        }
    }
}

const char* laneName (Lane l)
{
    switch (l)
    {
        case Lane::all: return "all";
        case Lane::kick: return "kick";
        case Lane::snare: return "snare";
        case Lane::hats: return "hats";
        case Lane::other: return "other";
    }
    return "all";
}

Lane laneOfDrum (int pitch) noexcept
{
    switch (pitch)
    {
        case 35: case 36: return Lane::kick;
        case 37: case 38: case 39: case 40: return Lane::snare;
        case 42: case 44: case 46: case 51: case 53: case 59: case 54: case 70: case 82: return Lane::hats;
        default: return Lane::other;
    }
}

// ---- from MIDI -------------------------------------------------------------------------------

std::vector<TimedNote> notesOf (const Track& t, Tick from, Tick to)
{
    std::vector<TimedNote> out;
    for (const auto& n : arranging::playedNotes (t, from, to))
        out.push_back ({ static_cast<double> (n.on), n.pitch, n.velocity });
    return out;
}

namespace
{
    /** Onsets: notes closer than a few ticks start together (a chord, or kick and hi-hat). */
    std::vector<double> onsetTimes (const std::vector<TimedNote>& notes)
    {
        std::vector<double> t;
        for (const auto& n : notes)
            t.push_back (n.at);
        std::sort (t.begin(), t.end());
        std::vector<double> out;
        for (double x : t)
            if (out.empty() || x - out.back() > kBeat / 32.0)
                out.push_back (x);
        return out;
    }

    /** The share of onsets that land on a step another onset of the same bar has taken: too coarse a grid. */
    double collisions (const std::vector<double>& onsets, Tick grid)
    {
        if (onsets.empty())
            return 0.0;
        std::map<long long, int> taken;
        int clashes = 0;
        for (double t : onsets)
        {
            const auto step = static_cast<long long> (std::llround (t / static_cast<double> (grid)));
            if (taken[step]++ > 0)
                ++clashes;
        }
        return static_cast<double> (clashes) / static_cast<double> (onsets.size());
    }
}

Tick findGrid (const std::vector<TimedNote>& notes, const TimeSignature& sig)
{
    const auto onsets = onsetTimes (notes);
    const Tick bar = sig.ticksPerBar();
    constexpr Tick candidates[] = { kTicksPerQuarter / 2, kTicksPerQuarter / 3, kTicksPerQuarter / 4, kTicksPerQuarter / 6, kTicksPerQuarter / 8 };
    Tick best = kTicksPerQuarter / 4;
    double bestClash = 2.0;
    for (Tick g : candidates)
    {
        if (bar % g != 0)
            continue;
        const double c = collisions (onsets, g);
        if (c <= 0.08)
            return g;
        if (c < bestClash - 0.01)
        {
            bestClash = c;
            best = g;
        }
    }
    return best;
}

Groove fromNotes (const std::vector<TimedNote>& all, const TimeSignature& sig, const ExtractOptions& o)
{
    Groove g;
    g.source = "midi";
    std::vector<TimedNote> notes;
    for (const auto& n : all)
        if (n.at >= static_cast<double> (o.from) - kBeat / 8.0 && n.at < static_cast<double> (o.to))
            notes.push_back (n);
    if (notes.size() < 4)
        return g;

    g.grid = o.grid > 0 ? o.grid : findGrid (notes, sig);
    const Tick bar = sig.ticksPerBar();
    if (bar % g.grid != 0)
        g.grid = kTicksPerQuarter / 4;
    g.length = bar * std::clamp (o.bars, 1, 8);
    const int steps = g.steps();
    const double grid = static_cast<double> (g.grid), length = static_cast<double> (g.length);

    // Which repeats of the pattern the notes cover.
    double first = notes.front().at, last = notes.front().at;
    for (const auto& n : notes)
    {
        first = std::min (first, n.at);
        last = std::max (last, n.at);
    }
    const auto firstRepeat = static_cast<long long> (std::floor (std::round (first / grid) * grid / length));
    const auto lastRepeat = static_cast<long long> (std::floor (std::round (last / grid) * grid / length));
    const double repeats = static_cast<double> (std::max<long long> (1, lastRepeat - firstRepeat + 1));

    // For each lane and step: the offsets and velocities, one per repeat (an onset of more notes counts once).
    struct Cell
    {
        double offsetSum = 0.0, velocity = 0.0;
        int notes = 0;
    };
    std::array<std::vector<std::map<long long, Cell>>, kLanes> cells;
    for (auto& lane : cells)
        lane.resize (static_cast<std::size_t> (steps));
    for (const auto& n : notes)
    {
        const double nominal = std::round (n.at / grid) * grid;
        const auto repeat = static_cast<long long> (std::floor (nominal / length));
        const int step = static_cast<int> (std::llround (positiveMod (nominal, length) / grid)) % steps;
        const double offset = n.at - nominal;
        const auto add = [&] (Lane l) {
            auto& c = cells[static_cast<std::size_t> (l)][static_cast<std::size_t> (step)][repeat];
            c.offsetSum += offset;
            c.velocity = std::max (c.velocity, static_cast<double> (n.velocity));
            ++c.notes;
        };
        add (Lane::all);
        if (o.drums)
            add (laneOfDrum (n.pitch));
    }

    for (int l = 0; l < kLanes; ++l)
    {
        if (l > 0 && ! o.drums)
            continue;
        std::vector<Step> lane (static_cast<std::size_t> (steps));
        bool any = false;
        for (int s = 0; s < steps; ++s)
        {
            const auto& byRepeat = cells[static_cast<std::size_t> (l)][static_cast<std::size_t> (s)];
            if (byRepeat.empty())
                continue;
            std::vector<double> offsets;
            double vel = 0.0;
            for (const auto& [repeat, c] : byRepeat)
            {
                offsets.push_back (c.offsetSum / c.notes);
                vel += c.velocity;
            }
            auto& st = lane[static_cast<std::size_t> (s)];
            st.offset = median (offsets);
            st.velocity = std::clamp (vel / static_cast<double> (byRepeat.size()), 1.0, 127.0);
            st.rate = std::min (1.0, static_cast<double> (byRepeat.size()) / repeats);
            any = true;
        }
        if (any)
            g.lanes[static_cast<std::size_t> (l)] = std::move (lane);
    }
    // In a drum lane, a step played in fewer than a fifth of the repeats is noise (or a stray sound found in audio).
    if (repeats >= 4.0)
        for (int l = 1; l < kLanes; ++l)
            for (auto& st : g.lanes[static_cast<std::size_t> (l)])
                if (st.played() && st.rate < 0.2)
                    st = {};
    // A drum lane with only one step played (one crash) says little: drop it, the notes use `all`.
    for (int l = 1; l < kLanes; ++l)
    {
        auto& lane = g.lanes[static_cast<std::size_t> (l)];
        if (std::count_if (lane.begin(), lane.end(), [] (const Step& s) { return s.played(); }) < 2)
            lane.clear();
    }
    return g;
}

// ---- from audio -----------------------------------------------------------------------------

namespace
{
    /** One band's hits: time (seconds), peak level (dB) and the band's loudest level. */
    struct BandHit
    {
        double seconds = 0.0, levelDb = 0.0, peak = 0.0;
    };

    /**
        Hits in one band. The band's loudness is followed with a smoothing of
        `tau` seconds (long enough that a kick's low wave doesn't ripple); a hit
        is where it rises `riseDb` above what it was just before. Its time is
        where the level first went 3 dB above the level before, less the
        smoothing delay.
    */
    std::vector<BandHit> bandHits (const std::vector<float>& x, double rate, double tau, double riseDb, double rangeDb, double minGap)
    {
        std::vector<BandHit> out;
        const double a = std::exp (-1.0 / (tau * rate));
        const auto step = static_cast<std::size_t> (std::max (1.0, rate * 0.0005)); // 0.5 ms
        std::vector<double> e;
        std::vector<float> peakAt;
        double env = 0.0;
        float peak = 0.0f;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            env = (1.0 - a) * static_cast<double> (x[i]) * x[i] + a * env;
            peak = std::max (peak, std::abs (x[i]));
            if ((i + 1) % step == 0)
            {
                e.push_back (10.0 * std::log10 (env + 1.0e-14));
                peakAt.push_back (peak);
                peak = 0.0f;
            }
        }
        if (e.size() < 8)
            return out;
        const double loudest = *std::max_element (e.begin(), e.end());
        const double dt = static_cast<double> (step) / rate;
        const auto jMin = static_cast<std::size_t> (std::ceil ((3.0 * tau + 0.001) / dt));
        const auto jMax = jMin + static_cast<std::size_t> (std::ceil (0.012 / dt));
        const auto before = [&] (std::size_t k) {
            double m = -300.0;
            for (std::size_t j = jMin; j <= jMax && j <= k; ++j)
                m = std::max (m, e[k - j]);
            return k >= jMin ? m : loudest - rangeDb - 20.0;
        };
        const auto gap = static_cast<std::size_t> (std::max (1.0, minGap / dt));
        const auto settle = static_cast<std::size_t> (std::ceil ((3.0 * tau + 0.004) / dt));
        std::size_t k = 0;
        while (k < e.size())
        {
            const double base = before (k);
            if (e[k] - base < riseDb || e[k] < loudest - rangeDb)
            {
                ++k;
                continue;
            }
            // Walk back to where the level first went 3 dB over the level before.
            std::size_t start = k;
            while (start > 0 && k - start < jMin && e[start - 1] - base >= 3.0)
                --start;
            double top = e[k];
            double peakLevel = 0.0;
            for (std::size_t j = start; j < std::min (e.size(), k + settle); ++j)
            {
                top = std::max (top, e[j]);
                peakLevel = std::max (peakLevel, static_cast<double> (peakAt[j]));
            }
            const double seconds = std::max (0.0, static_cast<double> (start) * dt - tau * 0.7);
            out.push_back ({ seconds, top, peakLevel });
            k = std::max (k + 1, start + gap);
        }
        return out;
    }
}

std::vector<Hit> findHits (const std::vector<float>& mono, double sampleRate, double sensitivity)
{
    // Three bands, each with its own hits, so a soft hi-hat in the tail of a loud kick is still found:
    // low (kick, under about 150 Hz), middle (snare, toms: 300 Hz .. 5 kHz) and high (hi-hats, cymbals).
    std::vector<Hit> out;
    if (mono.size() < 1024 || sampleRate <= 0.0)
        return out;
    constexpr double pi = 3.14159265358979;
    const double lowA = std::exp (-2.0 * pi * 150.0 / sampleRate);
    const double midA = std::exp (-2.0 * pi * 300.0 / sampleRate);
    const double topA = std::exp (-2.0 * pi * 5000.0 / sampleRate);
    const std::size_t n = mono.size();
    std::array<std::vector<float>, 3> band;
    for (auto& b : band)
        b.resize (n);
    double l1 = 0.0, l2 = 0.0, m1 = 0.0, m2 = 0.0, t1 = 0.0, t2 = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double x = mono[i];
        l1 = (1.0 - lowA) * x + lowA * l1;
        l2 = (1.0 - lowA) * l1 + lowA * l2;
        m1 = (1.0 - midA) * x + midA * m1;
        m2 = (1.0 - midA) * m1 + midA * m2;
        t1 = (1.0 - topA) * x + topA * t1;
        t2 = (1.0 - topA) * t1 + topA * t2;
        band[0][i] = static_cast<float> (l2);
        band[1][i] = static_cast<float> (t2 - m2);
        band[2][i] = static_cast<float> (x - t2);
    }

    const double sens = std::clamp (sensitivity, 0.0, 1.0);
    const double riseDb = 12.0 - 6.0 * sens;   // 12 dB .. 6 dB
    const double rangeDb = 24.0 + 16.0 * sens; // hits up to this far under the band's loudest
    const auto low = bandHits (band[0], sampleRate, 0.004, riseDb, rangeDb * 0.7, 0.06);
    const auto mid = bandHits (band[1], sampleRate, 0.0012, riseDb, rangeDb * 0.6, 0.03);
    const auto high = bandHits (band[2], sampleRate, 0.0006, riseDb, rangeDb, 0.03);
    const auto loudestOf = [] (const std::vector<BandHit>& v) {
        double m = -300.0;
        for (const auto& h : v)
            m = std::max (m, h.levelDb);
        return m;
    };
    const double midTop = loudestOf (mid);
    const auto near = [] (const std::vector<BandHit>& v, double t, double within, double minLevel) {
        for (const auto& h : v)
            if (std::abs (h.seconds - t) <= within && h.levelDb >= minLevel)
                return true;
        return false;
    };
    const auto topPeak = [] (const std::vector<BandHit>& v) {
        double m = 0.0;
        for (const auto& h : v)
            m = std::max (m, h.peak);
        return m > 0.0 ? m : 1.0;
    };
    const double lowPeak = topPeak (low), midPeak = topPeak (mid);

    // A low and a middle hit together: a kick (with its click), a snare (with its body), or both.
    // Tell them apart by how loud each band is against its loudest hit.
    std::vector<bool> lowUsed (low.size(), true), midUsed (mid.size(), true);
    for (std::size_t i = 0; i < low.size(); ++i)
        for (std::size_t j = 0; j < mid.size(); ++j)
            if (std::abs (low[i].seconds - mid[j].seconds) <= 0.008)
            {
                const double ratio = (mid[j].peak / midPeak) / std::max (1.0e-6, low[i].peak / lowPeak);
                if (ratio > 0.8)
                    lowUsed[i] = false;
                else if (ratio < 0.65)
                    midUsed[j] = false;
            }
    for (std::size_t i = 0; i < low.size(); ++i)
        if (lowUsed[i] && low[i].peak >= lowPeak * 0.1)
            out.push_back ({ low[i].seconds, low[i].peak, Lane::kick });
    for (std::size_t j = 0; j < mid.size(); ++j)
        if (midUsed[j] && mid[j].levelDb >= midTop - 20.0)
            out.push_back ({ mid[j].seconds, mid[j].peak, Lane::snare });
    // A hi-hat: a high hit without a snare at the same time (a snare's wires are high too).
    std::vector<BandHit> snares;
    for (std::size_t j = 0; j < mid.size(); ++j)
        if (midUsed[j])
            snares.push_back (mid[j]);
    for (const auto& h : high)
        if (! near (snares, h.seconds, 0.006, midTop - 20.0))
            out.push_back ({ h.seconds, h.peak, Lane::hats });
    std::sort (out.begin(), out.end(), [] (const Hit& a, const Hit& b) { return a.seconds < b.seconds; });

    // Strength: the peak against the loudest hit of the same lane (a hi-hat is quieter than a kick by nature).
    std::array<double, kLanes> laneLoudest {};
    for (const auto& hit : out)
        laneLoudest[static_cast<std::size_t> (hit.lane)] = std::max (laneLoudest[static_cast<std::size_t> (hit.lane)], hit.strength);
    for (auto& hit : out)
    {
        const double top = laneLoudest[static_cast<std::size_t> (hit.lane)];
        hit.strength = top > 0.0 ? std::clamp (hit.strength / top, 0.0, 1.0) : 1.0;
    }
    return out;
}

AudioGrid findAudioGrid (const std::vector<Hit>& hits, int beatsPerBar, double bpm)
{
    std::vector<style_from_audio::Onset> onsets;
    for (const auto& h : hits)
        onsets.push_back ({ h.seconds, h.lane == Lane::kick ? 2.0 : (h.lane == Lane::snare ? 1.2 : 0.6) * (0.5 + h.strength), h.lane == Lane::kick, false });
    const auto grid = style_from_audio::findGrid (onsets, beatsPerBar, 0.0, bpm);
    AudioGrid out { grid.bpm, grid.firstDownbeat, grid.confidence };
    if (hits.empty() || beatsPerBar < 2)
        return out;

    // Which beat is beat 1: kicks on it, no snare on it, and a loop usually starts on it.
    const double beat = 60.0 / std::max (1.0, out.bpm);
    double firstHit = hits.front().seconds;
    for (const auto& h : hits)
        firstHit = std::min (firstHit, h.seconds);
    double bestScore = -1.0e9, bestDownbeat = out.firstDownbeat;
    for (int k = 0; k < beatsPerBar; ++k)
    {
        const double downbeat = out.firstDownbeat + k * beat;
        double score = 0.0;
        for (const auto& h : hits)
        {
            const double beats = (h.seconds - downbeat) / beat;
            const double nearest = std::round (beats);
            if (std::abs (beats - nearest) > 0.15)
                continue;
            const auto index = ((static_cast<long long> (nearest) % beatsPerBar) + beatsPerBar) % beatsPerBar;
            const double w = 0.5 + h.strength;
            if (index == 0)
                score += (h.lane == Lane::kick ? 1.0 : (h.lane == Lane::snare ? -0.8 : 0.1)) * w;
            else if (beatsPerBar == 4 && (index == 1 || index == 3))
                score += (h.lane == Lane::snare ? 0.5 : 0.0) * w;
        }
        const double first = (firstHit - downbeat) / beat;
        if (std::abs (first - std::round (first)) < 0.15
            && ((static_cast<long long> (std::round (first)) % beatsPerBar) + beatsPerBar) % beatsPerBar == 0)
            score += 3.0;
        if (score > bestScore + 1.0e-9)
        {
            bestScore = score;
            bestDownbeat = downbeat;
        }
    }
    // Bar 1 at or just before the first hit.
    const double bar = beat * beatsPerBar;
    out.firstDownbeat = bestDownbeat - std::ceil ((bestDownbeat - firstHit - 0.15 * beat) / bar) * bar;
    return out;
}

std::vector<TimedNote> hitsToNotes (const std::vector<Hit>& hits, const AudioGrid& grid)
{
    std::vector<TimedNote> out;
    const double beatSeconds = 60.0 / std::max (1.0, grid.bpm);
    for (const auto& h : hits)
    {
        const double tick = (h.seconds - grid.firstDownbeat) / beatSeconds * kBeat;
        if (tick < -kBeat / 8.0)
            continue;
        const int pitch = h.lane == Lane::kick ? 36 : (h.lane == Lane::snare ? 38 : (h.lane == Lane::hats ? 42 : 60));
        // A SoundFont plays a note about as loud as velocity squared, so the square root gives the velocity back.
        const int velocity = std::clamp (static_cast<int> (std::lround (127.0 * std::sqrt (h.strength))), 1, 127);
        out.push_back ({ tick, pitch, velocity });
    }
    return out;
}

Groove fromHits (const std::vector<Hit>& hits, const TimeSignature& sig, double bpm, ExtractOptions o)
{
    return fromHits (hits, sig, findAudioGrid (hits, sig.numerator, bpm), o);
}

Groove fromHits (const std::vector<Hit>& hits, const TimeSignature& sig, const AudioGrid& grid, ExtractOptions o)
{
    const bool lanes = std::any_of (hits.begin(), hits.end(), [] (const Hit& h) { return h.lane != Lane::all; });
    o.drums = o.drums || lanes;
    auto g = fromNotes (hitsToNotes (hits, grid), sig, o);
    g.source = "audio";
    g.bpm = grid.bpm;
    return g;
}

// ---- from words ------------------------------------------------------------------------------

Feel feelFromWords (const std::string& original, double bpm, int beatsPerBar)
{
    Feel f;
    f.bpm = bpm > 0.0 ? bpm : 120.0;
    f.beatsPerBar = std::max (1, beatsPerBar);
    const auto text = lower (original);
    f.name = original.substr (0, 48);
    if (f.name.empty())
        f.name = "Feel";

    double degree = 1.0; // "a bit" .. "very"
    if (has (text, { "slight", "a bit", "a little", "subtle", "gentle swing", "light" }))
        degree = 0.55;
    if (has (text, { "very", "really", "heavy", "hard swing", "deep", "way ", "super", "extremely" }))
        degree = 1.6;

    if (has (text, { "16th", "sixteenth", "16ths" }))
        f.grid = kTicksPerQuarter / 4;
    if (has (text, { "swing", "swung", "jazz" }))
        f.swing = 50.0 + 12.0 * degree;
    if (has (text, { "shuffle", "triplet" }))
        f.swing = 66.0;
    if (has (text, { "straight", "even" }) && ! has (text, { "swing", "shuffle" }))
        f.swing = 50.0;
    if (has (text, { "lazy", "laid back", "laid-back", "laidback", "behind", "relaxed", "late", "dragging", "drag" }))
        f.behindMs = 14.0 * degree;
    if (has (text, { "pushed", "ahead", "rushing", "rushed", "urgent", "driving", "on top", "eager", "forward" }))
        f.behindMs = -9.0 * degree;
    if (has (text, { "tight", "on the grid", "locked", "machine" }) && ! has (text, { "behind", "ahead", "lazy", "pushed" }))
        f.behindMs = 0.0;

    // Numbers: "62%" is the swing, "12 ms" the offset.
    std::smatch m;
    if (std::regex_search (text, m, std::regex (R"((\d{2}(?:\.\d+)?)\s*%)")))
        f.swing = std::clamp (std::stod (m[1].str()), 50.0, 75.0);
    if (std::regex_search (text, m, std::regex (R"((\d{1,2}(?:\.\d+)?)\s*ms)")))
    {
        const double ms = std::stod (m[1].str());
        f.behindMs = has (text, { "ahead", "pushed", "early", "rush" }) ? -ms : ms;
    }

    // Lanes: "lazy snare", "snare late", "kick ahead", "hats pushed".
    const auto laneShift = [&] (std::initializer_list<const char*> names, double& ms) {
        for (const auto* n : names)
        {
            const auto at = text.find (n);
            if (at == std::string::npos)
                continue;
            const auto around = text.substr (at >= 12 ? at - 12 : 0, 40);
            if (has (around, { "lazy", "late", "behind", "laid back", "drag" }))
                ms = 10.0 * degree;
            else if (has (around, { "ahead", "early", "pushed", "rush", "on top" }))
                ms = -7.0 * degree;
        }
    };
    laneShift ({ "snare", "backbeat" }, f.snareMs);
    laneShift ({ "kick", "bass drum" }, f.kickMs);
    laneShift ({ "hat", "hi-hat", "hihat", "cymbal", "ride" }, f.hatsMs);

    // Accents.
    const Tick bar = kTicksPerQuarter * f.beatsPerBar;
    const int steps = static_cast<int> (bar / f.grid);
    const int perBeat = static_cast<int> (kTicksPerQuarter / f.grid);
    double contrast = 1.0;
    if (has (text, { "soft", "gentle", "smooth", "even dynamics", "flat" }))
        contrast = 0.5;
    if (has (text, { "punchy", "accented", "hard-hitting", "strong accents", "aggressive" }))
        contrast = 1.6;
    f.accents.assign (static_cast<std::size_t> (steps), 0.0);
    for (int s = 0; s < steps; ++s)
    {
        const int beat = s / perBeat;
        const bool onBeat = s % perBeat == 0;
        double v = onBeat ? 100.0 : (s % perBeat == perBeat / 2 ? 84.0 : 76.0);
        if (onBeat && beat == 0)
            v = 106.0;
        if (has (text, { "backbeat", "2 and 4", "two and four", "2 & 4" }) && onBeat)
            v = beat % 2 == 1 ? 116.0 : 92.0;
        if (has (text, { "off-beat", "offbeat", "off beat", "upbeat", "reggae", "ska" }))
            v = (s % perBeat == perBeat / 2 && perBeat > 1) ? 112.0 : (onBeat ? 82.0 : 70.0);
        if (has (text, { "four on the floor", "on the beat", "downbeat" }) && onBeat)
            v = 114.0;
        f.accents[static_cast<std::size_t> (s)] = std::clamp (90.0 + (v - 90.0) * contrast, 1.0, 127.0);
    }
    return f;
}

Groove fromFeel (const Feel& f)
{
    Groove g;
    g.source = "words";
    g.name = f.name;
    g.grid = std::max<Tick> (kTicksPerQuarter / 8, f.grid);
    g.length = kTicksPerQuarter * std::max (1, f.beatsPerBar);
    g.bpm = f.bpm;
    const int steps = g.steps();
    const double toTicks = 1.0 / msPerTick (f.bpm);
    const double swingTicks = (std::clamp (f.swing, 50.0, 75.0) / 100.0 * 2.0 - 1.0) * static_cast<double> (g.grid);
    const auto laneOf = [&] (double extraMs) {
        std::vector<Step> lane (static_cast<std::size_t> (steps));
        for (int s = 0; s < steps; ++s)
        {
            auto& st = lane[static_cast<std::size_t> (s)];
            st.offset = (f.behindMs + extraMs) * toTicks + (s % 2 == 1 ? swingTicks : 0.0);
            double v = 90.0;
            if (! f.accents.empty())
                v = f.accents[static_cast<std::size_t> (s) % f.accents.size()];
            else
                v = s % static_cast<int> (std::max<Tick> (1, kTicksPerQuarter / g.grid)) == 0 ? 100.0 : 82.0;
            st.velocity = std::clamp (v, 1.0, 127.0);
            st.rate = 1.0;
        }
        return lane;
    };
    g.lanes[0] = laneOf (0.0);
    if (f.kickMs != 0.0 || f.snareMs != 0.0 || f.hatsMs != 0.0)
    {
        g.lanes[static_cast<int> (Lane::kick)] = laneOf (f.kickMs);
        g.lanes[static_cast<int> (Lane::snare)] = laneOf (f.snareMs);
        g.lanes[static_cast<int> (Lane::hats)] = laneOf (f.hatsMs);
    }
    g.about = describe (g);
    return g;
}

std::vector<Groove> builtIns (double bpm)
{
    std::vector<Groove> out;
    const auto add = [&] (const std::string& name, Feel f) {
        f.name = name;
        f.bpm = bpm;
        auto g = fromFeel (f);
        g.source = "built-in";
        out.push_back (std::move (g));
    };
    Feel straight;
    straight.grid = kTicksPerQuarter / 4;
    add ("Straight 16ths", straight);
    for (double s : { 54.0, 58.0, 62.0, 66.0 })
    {
        Feel f;
        f.swing = s;
        add ("Swing " + std::to_string (static_cast<int> (s)) + "% (8ths)", f);
    }
    Feel shuffle16;
    shuffle16.grid = kTicksPerQuarter / 4;
    shuffle16.swing = 62.0;
    add ("Swing 62% (16ths)", shuffle16);
    add ("Laid back", feelFromWords ("laid back, lazy snare", bpm));
    add ("Pushed", feelFromWords ("pushed, ahead of the beat", bpm));
    add ("Backbeat", feelFromWords ("backbeat, punchy", bpm));
    add ("Lazy swing", feelFromWords ("lazy swing, behind the beat", bpm));
    return out;
}

// ---- applying ----------------------------------------------------------------------------

double offsetAt (const Groove& g, double songTick, Lane l)
{
    return offsetIn (prepare (g, l), songTick);
}

double accentAt (const Groove& g, double songTick, Lane l)
{
    return accentIn (prepare (g, l), songTick);
}

namespace
{
    int applyPrepared (Clip& clip, const Groove& g, const std::array<Prepared, kLanes>& lanes, const ApplyOptions& o)
    {
        int changed = 0;
        const double grid = static_cast<double> (g.grid);
        for (auto& n : clip.notes)
        {
            const Tick song = clip.start + n.start;
            if (song < o.from || song >= o.to)
                continue;
            const Lane lane = o.drums ? laneOfDrum (n.pitch) : Lane::all;
            const double amount = o.drums ? o.laneAmount[static_cast<std::size_t> (lane)] : 1.0;
            const auto& p = lanes[static_cast<std::size_t> (o.drums && g.hasLane (lane) ? lane : Lane::all)];

            double t = static_cast<double> (song);
            const double straight = std::round (t / grid) * grid;
            t += std::clamp (o.quantise, 0.0, 1.0) * (straight - t);
            t += std::clamp (o.timing, 0.0, 1.5) * amount * offsetIn (p, t);
            const Tick start = std::max<Tick> (0, std::llround (t) - clip.start);

            const double accent = accentIn (p, t);
            const double v = static_cast<double> (n.velocity) * (1.0 + std::clamp (o.velocity, 0.0, 1.5) * amount * (accent - 1.0));
            const auto velocity = static_cast<std::uint8_t> (std::clamp<long> (std::lround (v), 1, 127));
            if (start != n.start || velocity != n.velocity)
                ++changed;
            n.start = start;
            n.velocity = velocity;
        }
        if (clip.loopLength <= 0)
        {
            const Tick latest = std::max<Tick> (clip.length, 1);
            for (auto& n : clip.notes)
                n.start = std::min (n.start, latest - 1);
        }
        return changed;
    }
}

int apply (Clip& clip, const Groove& g, const ApplyOptions& o)
{
    if (g.empty() || g.grid <= 0)
        return 0;
    std::array<Prepared, kLanes> lanes;
    for (int l = 0; l < kLanes; ++l)
        lanes[static_cast<std::size_t> (l)] = prepare (g, static_cast<Lane> (l));
    const int changed = applyPrepared (clip, g, lanes, o);
    edit::normalise (clip);
    edit::removeOverlaps (clip);
    return changed;
}

int apply (Song& song, const std::vector<TrackId>& ids, const Groove& g, ApplyOptions o)
{
    int changed = 0;
    for (auto& t : song.tracks)
    {
        if (! ids.empty() && std::find (ids.begin(), ids.end(), t.id) == ids.end())
            continue;
        if (t.output.kind == TrackOutput::Kind::audio)
            continue;
        auto options = o;
        options.drums = o.drums || arranging::isDrumTrack (t);
        std::vector<ClipId> clips;
        for (const auto& c : t.clips)
            if (c != nullptr && c->end() > o.from && c->start < o.to && ! c->notes.empty())
                clips.push_back (c->id);
        for (auto id : clips)
            edit::modifyClip (song, id, [&] (Clip& c) { changed += apply (c, g, options); });
    }
    return changed;
}

// ---- in numbers ---------------------------------------------------------------------------

Summary summarise (const Groove& g)
{
    Summary s;
    if (g.empty())
        return s;
    const double mpt = msPerTick (g.bpm);
    const auto& all = g.lane (Lane::all);
    const auto offsets = filledOffsets (all);
    const int n = static_cast<int> (all.size());
    const bool binary = g.grid > 0 && (kTicksPerQuarter % g.grid == 0) && ((kTicksPerQuarter / g.grid) % 2 == 0 || g.grid == kTicksPerQuarter);

    if (binary && n >= 2)
    {
        double sum = 0.0;
        int count = 0;
        for (int i = 0; i + 1 < n; i += 2)
            if (all[static_cast<std::size_t> (i + 1)].played())
            {
                const double pair = 2.0 * static_cast<double> (g.grid);
                const double first = static_cast<double> (g.grid) + offsets[static_cast<std::size_t> (i + 1)] - offsets[static_cast<std::size_t> (i)];
                sum += 100.0 * first / pair;
                ++count;
            }
        if (count > 0)
            s.swing = std::clamp (sum / count, 25.0, 80.0);
    }

    for (int l = 0; l < kLanes; ++l)
    {
        const auto& lane = g.lanes[static_cast<std::size_t> (l)];
        double sum = 0.0;
        int count = 0;
        for (int i = 0; i < static_cast<int> (lane.size()); i += (binary ? 2 : 1))
            if (lane[static_cast<std::size_t> (i)].played())
            {
                sum += lane[static_cast<std::size_t> (i)].offset;
                ++count;
            }
        if (count == 0)
            for (const auto& st : lane)
                if (st.played())
                {
                    sum += st.offset;
                    ++count;
                }
        s.behindTicks[static_cast<std::size_t> (l)] = count > 0 ? sum / count : 0.0;
        s.behindMs[static_cast<std::size_t> (l)] = s.behindTicks[static_cast<std::size_t> (l)] * mpt;
    }

    double vsum = 0.0;
    int vcount = 0;
    for (const auto& st : all)
        if (st.played())
        {
            vsum += st.velocity;
            ++vcount;
        }
    for (const auto& st : all)
        s.accents.push_back (st.played() && vsum > 0.0 ? st.velocity / (vsum / vcount) : 0.0);

    double spread = 0.0;
    int count = 0;
    for (int i = 0; i < n; i += (binary ? 2 : 1))
        if (all[static_cast<std::size_t> (i)].played())
        {
            spread += std::abs (all[static_cast<std::size_t> (i)].offset - s.behindTicks[0]);
            ++count;
        }
    s.spreadMs = count > 0 ? spread / count * mpt : 0.0;
    return s;
}

std::string describe (const Groove& g)
{
    if (g.empty())
        return "no groove";
    const auto s = summarise (g);
    std::vector<std::string> parts;
    const bool triplet = g.grid > 0 && (kTicksPerQuarter / g.grid) % 3 == 0;
    if (triplet)
        parts.push_back ("triplet feel (" + gridName (g.grid) + ")");
    else if (s.swing >= 52.5)
        parts.push_back ("swing " + std::to_string (static_cast<int> (std::lround (s.swing))) + "% (" + (g.grid >= kTicksPerQuarter / 2 ? "8ths" : "16ths") + ")");
    else
        parts.push_back ("straight " + gridName (g.grid));
    const double all = s.behindMs[0];
    if (g.bpm > 0.0 || g.source != "midi")
    {
        if (all >= 4.0)
            parts.push_back (formatMs (all) + " behind the beat");
        else if (all <= -4.0)
            parts.push_back (formatMs (all) + " ahead of the beat");
    }
    for (Lane l : { Lane::kick, Lane::snare, Lane::hats })
        if (g.hasLane (l))
        {
            const double d = s.behindMs[static_cast<std::size_t> (l)] - all;
            if (std::abs (d) >= 4.0)
                parts.push_back (std::string (laneName (l)) + " " + formatMs (d) + (d > 0 ? " later" : " earlier"));
        }
    // Accents on beats.
    const int perBeat = std::max (1, static_cast<int> (kTicksPerQuarter / std::max<Tick> (1, g.grid)));
    std::vector<int> loudBeats;
    bool offbeats = false;
    for (std::size_t i = 0; i < s.accents.size(); ++i)
    {
        if (s.accents[i] >= 1.12 && static_cast<int> (i) % perBeat == 0)
            loudBeats.push_back (static_cast<int> (i) / perBeat + 1);
        if (s.accents[i] >= 1.12 && perBeat > 1 && static_cast<int> (i) % perBeat == perBeat / 2)
            offbeats = true;
    }
    if (offbeats && loudBeats.empty())
        parts.push_back ("accents on the off-beats");
    else if (! loudBeats.empty() && loudBeats.size() <= 4)
    {
        std::string b;
        for (std::size_t i = 0; i < loudBeats.size(); ++i)
            b += (i == 0 ? "" : (i + 1 == loudBeats.size() ? " and " : ", ")) + std::to_string (loudBeats[i]);
        parts.push_back ("accents on " + b);
    }
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i)
        out += (i == 0 ? "" : ", ") + parts[i];
    if (! out.empty())
        out[0] = static_cast<char> (std::toupper (static_cast<unsigned char> (out[0])));
    return out;
}

// ---- comparing ---------------------------------------------------------------------------

Match compare (const Groove& part, const Groove& target, double bpm, Lane lane)
{
    Match m;
    if (part.empty() || target.empty())
        return m;
    const double mpt = msPerTick (bpm);
    const auto& a = laneFor (part, lane);
    const auto& b = laneFor (target, lane);
    // Compare step by step at the target's grid: find the part's step at the same place.
    std::vector<double> va, vb;
    double err = 0.0;
    for (std::size_t i = 0; i < b.size(); ++i)
    {
        if (! b[i].played())
            continue;
        const double at = static_cast<double> (i) * static_cast<double> (target.grid);
        if (part.grid <= 0 || std::fmod (at, static_cast<double> (part.grid)) > 0.5)
            continue;
        const auto j = static_cast<std::size_t> (std::llround (positiveMod (at, static_cast<double> (part.length)) / static_cast<double> (part.grid)));
        if (j >= a.size() || ! a[j].played())
            continue;
        err += std::abs (a[j].offset - b[i].offset);
        va.push_back (a[j].velocity);
        vb.push_back (b[i].velocity);
        ++m.steps;
    }
    if (m.steps == 0)
        return m;
    m.timingErrorTicks = err / m.steps;
    m.timingErrorMs = m.timingErrorTicks * mpt;
    const double ma = std::accumulate (va.begin(), va.end(), 0.0) / static_cast<double> (va.size());
    const double mb = std::accumulate (vb.begin(), vb.end(), 0.0) / static_cast<double> (vb.size());
    double sab = 0.0, saa = 0.0, sbb = 0.0;
    for (std::size_t i = 0; i < va.size(); ++i)
    {
        sab += (va[i] - ma) * (vb[i] - mb);
        saa += (va[i] - ma) * (va[i] - ma);
        sbb += (vb[i] - mb) * (vb[i] - mb);
    }
    if (saa < 1.0e-6 && sbb < 1.0e-6)
        m.accentCorrelation = 1.0;
    else if (saa < 1.0e-6 || sbb < 1.0e-6)
        m.accentCorrelation = 0.0;
    else
        m.accentCorrelation = sab / std::sqrt (saa * sbb);
    m.swingDifference = std::abs (summarise (part).swing - summarise (target).swing);
    m.score = 60.0 * std::max (0.0, 1.0 - m.timingErrorMs / 25.0) + 25.0 * std::max (0.0, m.accentCorrelation)
              + 15.0 * std::max (0.0, 1.0 - m.swingDifference / 10.0);
    return m;
}

BeforeAfter compareChange (const Track& before, const Track& after, const Groove& g, const TimeSignature& sig, double bpm, Tick from, Tick to, bool drums)
{
    BeforeAfter out;
    if (g.empty() || g.grid <= 0)
        return out;
    // Wider than the range, so notes that moved a little across its edges still pair up.
    const Tick margin = g.grid;
    auto a = notesOf (before, std::max<Tick> (0, from - margin), to == std::numeric_limits<Tick>::max() ? to : to + margin);
    auto b = notesOf (after, std::max<Tick> (0, from - margin), to == std::numeric_limits<Tick>::max() ? to : to + margin);
    const auto order = [] (const TimedNote& x, const TimedNote& y) { return x.at != y.at ? x.at < y.at : x.pitch < y.pitch; };
    std::sort (a.begin(), a.end(), order);
    std::sort (b.begin(), b.end(), order);
    if (a.size() != b.size())
        return out;
    const double grid = static_cast<double> (g.grid);
    std::vector<TimedNote> pa, pb;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].at < static_cast<double> (from) || a[i].at >= static_cast<double> (to))
            continue;
        if (std::abs (a[i].at - std::round (a[i].at / grid) * grid) > grid / 6.0)
            continue;
        pa.push_back (a[i]);
        pb.push_back (b[i]);
    }
    out.notes = static_cast<int> (pa.size());
    const ExtractOptions e { g.grid, std::max (1, static_cast<int> (g.length / std::max<Tick> (1, sig.ticksPerBar()))), 0, std::numeric_limits<Tick>::max(), drums };
    out.before = compare (fromNotes (pa, sig, e), g, bpm);
    out.after = compare (fromNotes (pb, sig, e), g, bpm);
    return out;
}

// ---- JSON ------------------------------------------------------------------------------------

std::string toJson (const Groove& g)
{
    json lanes = json::object();
    for (int l = 0; l < kLanes; ++l)
    {
        const auto& lane = g.lanes[static_cast<std::size_t> (l)];
        if (lane.empty())
            continue;
        json steps = json::array();
        for (const auto& s : lane)
            steps.push_back ({ std::round (s.offset * 100.0) / 100.0, std::round (s.velocity * 10.0) / 10.0, std::round (s.rate * 100.0) / 100.0 });
        lanes[laneName (static_cast<Lane> (l))] = steps;
    }
    json j = { { "format", "arranger-groove" }, { "version", 1 }, { "name", g.name }, { "source", g.source }, { "about", g.about },
               { "grid", g.grid }, { "length", g.length }, { "bpm", g.bpm }, { "lanes", lanes } };
    return j.dump (1);
}

Groove fromJson (const std::string& text)
{
    try
    {
        const auto j = json::parse (text);
        if (j.value ("format", std::string()) != "arranger-groove")
            throw std::runtime_error ("not a groove file");
        Groove g;
        g.name = j.value ("name", std::string ("Groove"));
        g.source = j.value ("source", std::string());
        g.about = j.value ("about", std::string());
        g.grid = j.value ("grid", kTicksPerQuarter / 4);
        g.length = j.value ("length", kTicksPerQuarter * 4);
        g.bpm = j.value ("bpm", 0.0);
        if (g.grid <= 0 || g.length < g.grid || g.length % g.grid != 0 || g.steps() > 1024)
            throw std::runtime_error ("the groove's grid or length is not valid");
        if (j.contains ("lanes"))
            for (int l = 0; l < kLanes; ++l)
            {
                const auto name = laneName (static_cast<Lane> (l));
                if (! j["lanes"].contains (name))
                    continue;
                std::vector<Step> lane;
                for (const auto& s : j["lanes"][name])
                    lane.push_back ({ s.at (0).get<double>(), std::clamp (s.at (1).get<double>(), 0.0, 127.0), s.size() > 2 ? s.at (2).get<double>() : 1.0 });
                lane.resize (static_cast<std::size_t> (g.steps()));
                g.lanes[static_cast<std::size_t> (l)] = std::move (lane);
            }
        if (g.empty())
            throw std::runtime_error ("the groove has no steps");
        return g;
    }
    catch (const json::exception& e)
    {
        throw std::runtime_error (std::string ("not a groove file: ") + e.what());
    }
}

} // namespace arranger::groove

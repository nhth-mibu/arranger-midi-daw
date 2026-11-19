#include "arranger/core/SongForm.h"

#include "arranger/core/Arranging.h"
#include "arranger/core/SongEdit.h"
#include "arranger/core/Theory.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>

namespace arranger::form
{

namespace
{
    // ---- small helpers ----------------------------------------------------------------
    std::string lower (std::string s)
    {
        for (auto& c : s)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return s;
    }

    std::string trim (std::string_view s)
    {
        const auto a = s.find_first_not_of (" \t\r\n");
        if (a == std::string_view::npos)
            return {};
        const auto b = s.find_last_not_of (" \t\r\n");
        return std::string (s.substr (a, b - a + 1));
    }

    bool sameName (std::string_view a, std::string_view b) { return lower (trim (a)) == lower (trim (b)); }

    /** "Verse 2" -> "Verse"; "Chorus" -> "Chorus". */
    std::string baseName (const std::string& name)
    {
        auto s = trim (name);
        while (! s.empty() && (std::isdigit (static_cast<unsigned char> (s.back())) != 0 || s.back() == ' '))
            s.pop_back();
        return s.empty() ? trim (name) : s;
    }

    Tick barOf (const Song& s) { return std::max<Tick> (1, s.timeSignature.ticksPerBar()); }
    Tick beatOf (const Song& s) { return std::max<Tick> (1, s.timeSignature.ticksPerBeat()); }
    constexpr Tick kPush = kTicksPerQuarter / 4; ///< a note this much before a section belongs to it (a push)

    int clampVelocity (double v) { return std::clamp (static_cast<int> (std::lround (v)), 1, 127); }

    bool hasTimeline (const Song& s)
    {
        for (const auto& t : s.tracks)
            if (! t.clips.empty() || ! t.audio.empty())
                return true;
        return false;
    }

    bool isMidi (const Track& t) { return t.output.kind != TrackOutput::Kind::audio; }

    // ---- what a track plays, in song time ------------------------------------------------
    struct Lane
    {
        std::vector<Note> notes; ///< `start` is a song position here
        std::vector<ControlEvent> controls; ///< `at` is a song position here
    };

    void sortLane (Lane& l)
    {
        std::stable_sort (l.notes.begin(), l.notes.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
        std::stable_sort (l.controls.begin(), l.controls.end(), [] (const ControlEvent& a, const ControlEvent& b) { return a.at < b.at; });
    }

    Lane timelineLane (const Track& t)
    {
        Lane lane;
        for (const auto& c : t.clips)
        {
            const auto played = edit::playedClip (*c);
            for (auto n : played.notes)
            {
                n.start += c->start;
                lane.notes.push_back (n);
            }
            for (auto e : played.controls)
            {
                e.at += c->start;
                lane.controls.push_back (e);
            }
        }
        sortLane (lane);
        return lane;
    }

    /** A Clip grid slot looped over `length` ticks, from 0. */
    Lane sceneLane (const Track& t, int scene, Tick length)
    {
        Lane lane;
        if (scene < 0 || scene >= static_cast<int> (t.slots.size()) || t.slots[static_cast<std::size_t> (scene)] == nullptr)
            return lane;
        const auto played = edit::playedClip (*t.slots[static_cast<std::size_t> (scene)]);
        const Tick cycle = std::max<Tick> (1, played.length);
        for (Tick base = 0; base < length; base += cycle)
        {
            for (auto n : played.notes)
                if (base + n.start < length)
                {
                    n.start += base;
                    n.length = std::min (n.length, length - n.start);
                    lane.notes.push_back (n);
                }
            for (auto e : played.controls)
                if (base + e.at < length)
                {
                    e.at += base;
                    lane.controls.push_back (e);
                }
        }
        sortLane (lane);
        return lane;
    }

    double energy (const Lane& l, Tick from, Tick to)
    {
        double e = 0.0;
        for (const auto& n : l.notes)
            if (n.start >= from && n.start < to)
                e += n.velocity / 127.0;
        return e;
    }

    std::size_t noteCount (const Lane& l, Tick from, Tick to)
    {
        return static_cast<std::size_t> (std::count_if (l.notes.begin(), l.notes.end(), [&] (const Note& n) { return n.start >= from && n.start < to; }));
    }

    double meanVelocity (const Lane& l, Tick from, Tick to, double otherwise)
    {
        double sum = 0.0;
        int count = 0;
        for (const auto& n : l.notes)
            if (n.start >= from && n.start < to)
            {
                sum += n.velocity;
                ++count;
            }
        return count > 0 ? sum / count : otherwise;
    }

    /** Removes the notes that start in [from, to) and cuts the notes that sound into it at `from`. */
    void clearWindow (Lane& l, Tick from, Tick to)
    {
        std::vector<Note> kept;
        kept.reserve (l.notes.size());
        for (auto n : l.notes)
        {
            if (n.start >= from && n.start < to)
                continue;
            if (n.start < from && n.end() > from)
                n.length = std::max<Tick> (1, from - n.start);
            kept.push_back (n);
        }
        l.notes = std::move (kept);
    }

    // ---- section kinds -------------------------------------------------------------------
    struct KindWord
    {
        const char* word;
        const char* kind;
    };
    constexpr KindWord kKindWords[] = {
        { "pre-chorus", "prechorus" }, { "prechorus", "prechorus" }, { "pre chorus", "prechorus" }, { "build", "prechorus" },
        { "intro", "intro" }, { "verse", "verse" }, { "chorus", "chorus" }, { "refrain", "chorus" }, { "hook", "chorus" },
        { "bridge", "bridge" }, { "middle", "bridge" }, { "solo", "solo" }, { "instrumental", "solo" }, { "break", "solo" },
        { "outro", "outro" }, { "ending", "outro" }, { "coda", "outro" }, { "end", "outro" },
    };
}

std::string kindOf (std::string_view sectionName)
{
    const auto n = lower (std::string (sectionName));
    for (const auto& k : kKindWords)
        if (n.find (k.word) != std::string::npos)
            return k.kind;
    return "other";
}

// ---- finding sections --------------------------------------------------------------------
namespace
{
    std::vector<SongSection> sceneSections (const Song& s)
    {
        std::vector<SongSection> out;
        const Tick bar = barOf (s);
        Tick at = 0;
        for (int scene = 0; scene < s.sceneCount(); ++scene)
        {
            Tick longest = 0;
            for (const auto& t : s.tracks)
            {
                const auto i = static_cast<std::size_t> (scene);
                if (i < t.slots.size() && t.slots[i] != nullptr)
                    longest = std::max (longest, t.slots[i]->length);
                if (i < t.audioSlots.size() && t.audioSlots[i] != nullptr)
                    longest = std::max (longest, t.audioSlots[i]->length);
            }
            if (longest <= 0)
                continue;
            Tick length = (longest + bar - 1) / bar * bar;
            while (length < bar * 4)
                length *= 2;
            SongSection sec;
            sec.name = scene < static_cast<int> (s.scenes.size()) && ! trim (s.scenes[static_cast<std::size_t> (scene)]).empty()
                           ? trim (s.scenes[static_cast<std::size_t> (scene)])
                           : "Scene " + std::to_string (scene + 1);
            sec.scene = scene;
            sec.start = at;
            sec.end = at + length;
            at = sec.end;
            out.push_back (sec);
        }
        // Names must differ.
        std::map<std::string, int> seen;
        for (auto& sec : out)
            if (const int n = ++seen[lower (sec.name)]; n > 1)
                sec.name += " " + std::to_string (n);
        return out;
    }

    constexpr std::size_t kAudioSteps = 8; ///< loudness steps in a bar
    constexpr float kAudioQuiet = 0.12f;   ///< below this (against the loud parts) a recording counts as resting

    bool soundsLikeLead (const std::string& name)
    {
        const auto n = lower (name);
        for (const char* w : { "vocal", "vox", "voice", "lead", "melody", "solo", "sing" })
            if (n.find (w) != std::string::npos)
                return true;
        return false;
    }

    /** The loudness (RMS) of an audio track, eighth by eighth to `end`, against its loud parts (0..1). Empty when no file could be read. */
    std::vector<float> measureLevels (const Song& s, const Track& t, Tick end, const SampleLookup& audio)
    {
        if (! audio)
            return {};
        const Tick step = barOf (s) / static_cast<Tick> (kAudioSteps);
        const auto steps = static_cast<std::size_t> (end / step);
        std::vector<float> level (steps, 0.0f);
        bool any = false;
        for (const auto& c : t.audio)
        {
            const auto sample = audio (c->file);
            if (sample == nullptr || sample->frames() == 0 || sample->sampleRate <= 0.0)
                continue;
            any = true;
            const double speed = edit::audioSpeed (s, *c);
            const double gain = std::pow (10.0, static_cast<double> (c->gainDb) / 20.0);
            const double clipSeconds = s.tempo.secondsAt (static_cast<double> (c->start));
            const auto fileFrame = [&] (Tick at) {
                const double seconds = c->offset + (s.tempo.secondsAt (static_cast<double> (at)) - clipSeconds) * speed;
                return static_cast<std::size_t> (std::clamp (seconds * sample->sampleRate, 0.0, static_cast<double> (sample->frames())));
            };
            const auto first = static_cast<std::size_t> (std::max<Tick> (0, c->start) / step);
            for (auto k = first; k < steps && static_cast<Tick> (k) * step < c->end(); ++k)
            {
                const Tick a = std::max (c->start, static_cast<Tick> (k) * step), b = std::min (c->end(), static_cast<Tick> (k + 1) * step);
                const auto f0 = fileFrame (a), f1 = fileFrame (b);
                if (f1 <= f0)
                    continue;
                double sum = 0.0;
                std::size_t count = 0;
                const std::size_t hop = std::max<std::size_t> (1, (f1 - f0) / 2048); // enough frames for a level
                for (auto f = f0; f < f1; f += hop, ++count)
                {
                    double v = 0.0;
                    for (const auto& ch : sample->channels)
                        v += ch[f];
                    v /= static_cast<double> (sample->channels.size());
                    sum += v * v;
                }
                const auto rms = static_cast<float> (std::sqrt (sum / static_cast<double> (std::max<std::size_t> (1, count))) * gain);
                level[k] = std::max (level[k], rms);
            }
        }
        if (! any)
            return {};
        // Against the loud parts: the level that one step in ten is above.
        auto sorted = level;
        std::sort (sorted.begin(), sorted.end());
        const float loud = sorted.empty() ? 0.0f : sorted[sorted.size() * 9 / 10];
        if (loud <= 1.0e-5f)
            return std::vector<float> (steps, 0.0f);
        for (auto& v : level)
            v = std::min (1.0f, v / loud);
        return level;
    }

    /** The notes of a phrase as (pitch, sixteenth) pairs, for comparing phrases. */
    using Print = std::set<std::pair<int, int>>;

    struct Phrase
    {
        Tick from = 0, to = 0;
        std::vector<Print> prints;   ///< by track
        std::vector<double> density; ///< notes per bar, by track
        double energy = 0.0;         ///< per bar
        bool melody = false;
    };
}

std::vector<SongSection> findSections (const Song& s, const DeviceLibrary* devices, const SampleLookup& audio)
{
    if (! hasTimeline (s))
        return sceneSections (s);

    const Tick bar = barOf (s);
    std::vector<Lane> lanes;
    std::vector<TrackId> ids;
    for (const auto& t : s.tracks)
        if (isMidi (t) && ! t.clips.empty())
        {
            lanes.push_back (timelineLane (t));
            ids.push_back (t.id);
        }
    // The song ends with the bar of its last note (not with empty clip ends or a ringing tail); audio counts whole.
    Tick last = 0;
    for (const auto& l : lanes)
        for (const auto& n : l.notes)
            last = std::max (last, n.start + 1);
    for (const auto& t : s.tracks)
        if (! isMidi (t))
            for (const auto& c : t.audio)
                last = std::max (last, c->end());
    const Tick end = std::max (bar, (last + bar - 1) / bar * bar);
    const int bars = static_cast<int> (end / bar);
    if (bars < 8)
        return { SongSection { "Song", 0, end, -1 } };

    const auto roles = arranging::guessRoles (s, devices);
    TrackId melody = 0;
    for (const auto& [id, r] : roles)
        if (r == arranging::Role::melody)
            melody = id;

    // Audio tracks: how loud they play, eighth by eighth (when the audio can be read), else where they have clips.
    struct AudioLane
    {
        const Track* track = nullptr;
        std::vector<float> level; ///< by eighth, 0..1 against the track's loud parts; empty: not measured
        bool lead = false;
    };
    std::vector<AudioLane> audioTracks;
    for (const auto& t : s.tracks)
        if (! isMidi (t) && ! t.audio.empty())
            audioTracks.push_back ({ &t, measureLevels (s, t, end, audio), t.id == melody || soundsLikeLead (t.name) });

    // Bar by bar: what each track plays.
    const Tick sixteenth = kTicksPerQuarter / 4;
    std::vector<Phrase> phrases;
    for (Tick from = 0; from < end; from += bar)
    {
        Phrase p;
        p.from = from;
        p.to = std::min (end, from + bar);
        for (std::size_t i = 0; i < lanes.size(); ++i)
        {
            Print print;
            std::size_t count = 0;
            for (const auto& n : lanes[i].notes)
                if (n.start >= p.from && n.start < p.to)
                {
                    print.insert ({ n.pitch, static_cast<int> ((n.start - p.from + sixteenth / 2) / sixteenth) });
                    ++count;
                    p.energy += n.velocity / 127.0;
                }
            p.prints.push_back (std::move (print));
            p.density.push_back (static_cast<double> (count));
            if (ids[i] == melody && count >= 1)
                p.melody = true;
        }
        for (const auto& a : audioTracks)
        {
            if (a.level.empty())
            {
                Tick covered = 0;
                for (const auto& c : a.track->audio)
                    covered += std::max<Tick> (0, std::min (c->end(), p.to) - std::max (c->start, p.from));
                p.density.push_back (covered * 2 >= p.to - p.from ? 4.0 : 0.0);
                p.prints.emplace_back();
                continue;
            }
            // Loudness as "notes": a bar played loud all through counts 8; a print of the loudness shape.
            Print print;
            double sum = 0.0;
            const auto bar0 = static_cast<std::size_t> (p.from / bar) * kAudioSteps;
            for (std::size_t k = 0; k < kAudioSteps && bar0 + k < a.level.size(); ++k)
            {
                const float v = a.level[bar0 + k];
                if (v < kAudioQuiet)
                    continue;
                sum += v;
                print.insert ({ -1 - (v < 0.4f ? 0 : (v < 0.75f ? 1 : 2)), static_cast<int> (k) });
            }
            p.density.push_back (sum);
            p.energy += sum / static_cast<double> (kAudioSteps) * 4.0;
            if (a.lead && sum >= 2.0)
                p.melody = true;
            p.prints.push_back (std::move (print));
        }
        phrases.push_back (std::move (p));
    }

    // Two bars: how alike they are, over the tracks that play in either.
    const auto similarity = [&] (const Phrase& a, const Phrase& b) {
        double common = 0.0, all = 0.0;
        for (std::size_t t = 0; t < a.prints.size(); ++t)
        {
            const auto& x = a.prints[t];
            const auto& y = b.prints[t];
            std::size_t both = 0;
            for (const auto& e : x)
                both += y.count (e);
            common += static_cast<double> (both);
            all += static_cast<double> (x.size() + y.size() - both);
        }
        return all > 0.0 ? common / all : 1.0;
    };

    // How much the texture changes at each bar line: the two bars before against the two bars after.
    const auto n = phrases.size();
    const auto tracks = phrases.front().density.size();
    std::vector<double> novelty (n + 1, 0.0);
    for (std::size_t at = 1; at < n; ++at)
    {
        const std::size_t w = std::min<std::size_t> ({ 2, at, n - at });
        double d = 0.0;
        for (std::size_t t = 0; t < tracks; ++t)
        {
            double before = 0.0, after = 0.0;
            for (std::size_t k = 0; k < w; ++k)
            {
                before += phrases[at - 1 - k].density[t];
                after += phrases[at + k].density[t];
            }
            before /= static_cast<double> (w);
            after /= static_cast<double> (w);
            d += std::abs (std::log1p (before) - std::log1p (after));
            if ((before < 0.5) != (after < 0.5) && std::max (before, after) >= 1.0)
                d += 0.75; // a track comes in or goes out
        }
        novelty[at] = d;
    }
    double most = 0.0;
    for (auto v : novelty)
        most = std::max (most, v);
    const double threshold = std::max (1.0, 0.25 * static_cast<double> (tracks));

    // The strongest changes first, at least two bars apart; a change on a 4-bar line counts a little more.
    // Sections are mostly 4, 8 or 16 bars long: a change on those lines counts more.
    const auto weight = [&] (std::size_t at) { return novelty[at] * (at % 8 == 0 ? 1.4 : (at % 4 == 0 ? 1.15 : (at % 2 == 0 ? 0.85 : 0.6))); };
    std::vector<std::size_t> order;
    for (std::size_t at = 1; at < n; ++at)
        if (weight (at) >= threshold && novelty[at] >= novelty[at - 1] * 0.8 && novelty[at] >= novelty[at + 1] * 0.8)
            order.push_back (at);
    std::stable_sort (order.begin(), order.end(), [&] (std::size_t x, std::size_t y) { return weight (x) > weight (y); });
    std::set<std::size_t> cuts;
    for (auto at : order)
    {
        // At least 4 bars apart; 2 for a very clear change (a short interlude).
        const std::size_t gap = weight (at) >= threshold * 3.0 ? 2 : 4;
        bool near = at < 2 || at + 2 > n;
        for (auto c : cuts)
            if ((c > at ? c - at : at - c) < gap)
                near = true;
        if (! near)
            cuts.insert (at);
    }
    cuts.insert (n);

    // Long sections are cut where the music starts again, else every 8 bars.
    std::vector<std::pair<std::size_t, std::size_t>> runs; ///< [first, last) bar
    std::size_t first = 0;
    for (auto cut : cuts)
    {
        for (std::size_t a = first; a < cut;)
        {
            std::size_t b = cut;
            // 16 bars whose second half plays other music (a solo, then a chorus): two sections.
            if (cut - a == 16)
            {
                double alike = 0.0;
                for (std::size_t k = 0; k < 4; ++k)
                    alike += similarity (phrases[a + k], phrases[a + 8 + k]) / 4.0;
                if (alike < 0.25)
                    b = a + 8;
            }
            if (cut - a > 16)
            {
                b = a + 8;
                for (std::size_t k = a + 8; k + 4 <= cut && k <= a + 12; k += 4)
                    if (similarity (phrases[k], phrases[a]) >= 0.6)
                    {
                        b = k;
                        break;
                    }
            }
            runs.emplace_back (a, b);
            a = b;
        }
        first = cut;
    }
    // A last piece of one bar goes with the section before it.
    if (runs.size() >= 2 && runs.back().second - runs.back().first == 1)
    {
        runs[runs.size() - 2].second = runs.back().second;
        runs.pop_back();
    }

    // Group similar runs.
    struct Run
    {
        std::size_t first = 0, last = 0;
        int group = -1;
        double energy = 0.0;
        bool melody = false;
    };
    std::vector<Run> rs;
    for (auto [a, b] : runs)
    {
        Run r { a, b, -1, 0.0, false };
        std::size_t withMelody = 0;
        for (auto i = a; i < b; ++i)
        {
            r.energy += phrases[i].energy;
            withMelody += phrases[i].melody ? 1 : 0;
        }
        r.energy /= static_cast<double> (b - a);
        r.melody = withMelody * 2 >= b - a;
        rs.push_back (r);
    }
    const auto runSimilarity = [&] (const Run& x, const Run& y) {
        const auto count = std::min (x.last - x.first, y.last - y.first);
        double sum = 0.0;
        for (std::size_t k = 0; k < count; ++k)
            sum += similarity (phrases[x.first + k], phrases[y.first + k]);
        const double lengths = static_cast<double> (count) / static_cast<double> (std::max (x.last - x.first, y.last - y.first));
        return count > 0 ? sum / static_cast<double> (count) * (0.6 + 0.4 * lengths) : 0.0;
    };
    int groups = 0;
    // With a melody in the song, a first or last part without it is the intro or the outro: a group of its own.
    const auto ownGroup = [&] (std::size_t i) { return melody != 0 && rs.size() >= 3 && (i == 0 || i + 1 == rs.size()) && ! rs[i].melody; };
    for (std::size_t i = 0; i < rs.size(); ++i)
    {
        if (ownGroup (i))
        {
            rs[i].group = groups++;
            continue;
        }
        double best = 0.45;
        for (std::size_t j = 0; j < i; ++j)
            if (rs[i].melody != rs[j].melody || ownGroup (j))
                continue; // with and without the melody: not the same part
            else if (const double sim = runSimilarity (rs[i], rs[j]); sim >= best)
            {
                best = sim;
                rs[i].group = rs[j].group;
            }
        if (rs[i].group < 0)
            rs[i].group = groups++;
    }

    // Name the groups.
    std::vector<int> members (static_cast<std::size_t> (groups), 0);
    std::vector<double> groupEnergy (static_cast<std::size_t> (groups), 0.0);
    for (const auto& r : rs)
    {
        ++members[static_cast<std::size_t> (r.group)];
        groupEnergy[static_cast<std::size_t> (r.group)] += r.energy;
    }
    for (int g = 0; g < groups; ++g)
        groupEnergy[static_cast<std::size_t> (g)] /= std::max (1, members[static_cast<std::size_t> (g)]);
    double meanEnergy = 0.0;
    for (const auto& r : rs)
        meanEnergy += r.energy;
    meanEnergy /= static_cast<double> (std::max<std::size_t> (1, rs.size()));

    std::vector<std::string> groupName (static_cast<std::size_t> (groups));
    const auto& firstRun = rs.front();
    const auto& lastRun = rs.back();
    if (rs.size() >= 3 && members[static_cast<std::size_t> (firstRun.group)] == 1 && (! firstRun.melody || firstRun.last - firstRun.first <= 4))
        groupName[static_cast<std::size_t> (firstRun.group)] = "Intro";
    if (rs.size() >= 3 && members[static_cast<std::size_t> (lastRun.group)] == 1 && groupName[static_cast<std::size_t> (lastRun.group)].empty()
        && (! lastRun.melody || lastRun.last - lastRun.first <= 4 || lastRun.energy < meanEnergy))
        groupName[static_cast<std::size_t> (lastRun.group)] = "Outro";

    // The repeated groups: the busiest is the chorus, the first other one the verse.
    int chorus = -1, verse = -1;
    for (int g = 0; g < groups; ++g)
        if (members[static_cast<std::size_t> (g)] >= 2 && groupName[static_cast<std::size_t> (g)].empty()
            && (chorus < 0 || groupEnergy[static_cast<std::size_t> (g)] > groupEnergy[static_cast<std::size_t> (chorus)]))
            chorus = g;
    int repeated = 0;
    for (int g = 0; g < groups; ++g)
        if (members[static_cast<std::size_t> (g)] >= 2 && groupName[static_cast<std::size_t> (g)].empty())
            ++repeated;
    if (chorus >= 0 && repeated == 1 && groupEnergy[static_cast<std::size_t> (chorus)] < meanEnergy * 1.05)
    {
        verse = chorus; // one repeated part that is not busier than the rest: call it the verse
        chorus = -1;
    }
    for (const auto& r : rs)
        if (verse < 0 && r.group != chorus && members[static_cast<std::size_t> (r.group)] >= 2 && groupName[static_cast<std::size_t> (r.group)].empty())
            verse = r.group;
    if (chorus >= 0)
        groupName[static_cast<std::size_t> (chorus)] = "Chorus";
    if (verse >= 0)
        groupName[static_cast<std::size_t> (verse)] = "Verse";
    // A repeated group that always comes just before the chorus: the pre-chorus.
    for (int g = 0; g < groups && chorus >= 0; ++g)
    {
        if (! groupName[static_cast<std::size_t> (g)].empty() || members[static_cast<std::size_t> (g)] < 2)
            continue;
        bool before = true;
        for (std::size_t i = 0; i < rs.size(); ++i)
            if (rs[i].group == g && (i + 1 >= rs.size() || rs[i + 1].group != chorus))
                before = false;
        if (before)
            groupName[static_cast<std::size_t> (g)] = "Pre-Chorus";
    }
    char letter = 'C';
    bool bridge = false;
    for (const auto& r : rs)
    {
        auto& name = groupName[static_cast<std::size_t> (r.group)];
        if (! name.empty())
            continue;
        if (! r.melody && melody != 0)
            name = "Instrumental";
        else if (! bridge && members[static_cast<std::size_t> (r.group)] == 1)
        {
            name = "Bridge";
            bridge = true;
        }
        else
            name = std::string ("Part ") + letter++;
    }

    // Number the names that come more than once.
    std::map<std::string, int> total, seen;
    for (const auto& r : rs)
        ++total[groupName[static_cast<std::size_t> (r.group)]];
    std::vector<SongSection> out;
    for (const auto& r : rs)
    {
        const auto& name = groupName[static_cast<std::size_t> (r.group)];
        SongSection sec;
        sec.name = total[name] > 1 ? name + " " + std::to_string (++seen[name]) : name;
        sec.start = phrases[r.first].from;
        sec.end = phrases[r.last - 1].to;
        out.push_back (sec);
    }
    return out;
}

std::vector<SongSection> sectionsOf (const Song& s, const DeviceLibrary* devices, const SampleLookup& audio)
{
    return s.sections.empty() ? findSections (s, devices, audio) : s.sections;
}

std::string problemWith (const std::vector<SongSection>& sections)
{
    std::set<std::string> names;
    const SongSection* previous = nullptr;
    for (const auto& sec : sections)
    {
        if (trim (sec.name).empty())
            return "A section has no name.";
        if (! names.insert (lower (trim (sec.name))).second)
            return "Two sections are called \"" + sec.name + "\". Give each section its own name.";
        if (sec.end <= sec.start || sec.start < 0)
            return "\"" + sec.name + "\" has no length.";
        if (sec.scene < 0 && previous != nullptr && sec.start < previous->end)
            return "\"" + sec.name + "\" starts before \"" + previous->name + "\" ends.";
        if (sec.scene < 0)
            previous = &sec;
    }
    return {};
}

std::vector<SongSection> parseSections (const Song& s, std::string_view text)
{
    std::vector<SongSection> out;
    const Tick bar = barOf (s);
    std::istringstream in { std::string (text) };
    std::string line;
    while (std::getline (in, line))
    {
        line = trim (line);
        if (line.empty())
            continue;
        // The bars are at the end: "Verse 1: 5-12", "Chorus 13 - 20", "Tag 33".
        auto pos = line.size();
        const auto skipDigits = [&] {
            while (pos > 0 && std::isdigit (static_cast<unsigned char> (line[pos - 1])) != 0)
                --pos;
        };
        const auto skipSpaces = [&] {
            while (pos > 0 && line[pos - 1] == ' ')
                --pos;
        };
        const auto endDigits = pos;
        skipDigits();
        if (pos == endDigits)
            throw std::runtime_error ("\"" + line + "\": give the bars after the name, like \"Verse 1: 5-12\".");
        const int last = std::stoi (line.substr (pos, endDigits - pos));
        int firstBar = last;
        skipSpaces();
        if (pos > 0 && (line[pos - 1] == '-' || line[pos - 1] == '~'))
        {
            --pos;
            skipSpaces();
            const auto e = pos;
            skipDigits();
            if (pos == e)
                throw std::runtime_error ("\"" + line + "\": give the bars like 5-12.");
            firstBar = std::stoi (line.substr (pos, e - pos));
        }
        auto name = trim (line.substr (0, pos));
        while (! name.empty() && (name.back() == ':' || name.back() == ',' || name.back() == ' ' || name.back() == '='))
            name.pop_back();
        name = trim (name);
        if (name.empty())
            throw std::runtime_error ("\"" + line + "\": the section needs a name.");
        if (firstBar < 1 || last < firstBar)
            throw std::runtime_error ("\"" + line + "\": bars start at 1 and the last bar is not before the first.");
        out.push_back ({ name, static_cast<Tick> (firstBar - 1) * bar, static_cast<Tick> (last) * bar, -1 });
    }
    std::stable_sort (out.begin(), out.end(), [] (const SongSection& a, const SongSection& b) { return a.start < b.start; });
    if (const auto problem = problemWith (out); ! problem.empty())
        throw std::runtime_error (problem);
    return out;
}

std::string sectionsText (const Song& s, const std::vector<SongSection>& sections)
{
    const Tick bar = barOf (s);
    std::string out;
    for (const auto& sec : sections)
    {
        if (sec.scene >= 0)
            continue;
        out += sec.name + ": " + std::to_string (sec.start / bar + 1) + "-" + std::to_string ((sec.end - 1) / bar + 1) + "\n";
    }
    return out;
}

// ---- presets and form text -----------------------------------------------------------------
const char* presetId (Preset p)
{
    switch (p)
    {
        case Preset::shortVersion: return "short";
        case Preset::longIntro: return "long_intro";
        case Preset::gig: return "gig";
        case Preset::instrumentalBreak: return "instrumental_break";
    }
    return "short";
}

std::string presetName (Preset p)
{
    switch (p)
    {
        case Preset::shortVersion: return "Short version";
        case Preset::longIntro: return "Long intro";
        case Preset::gig: return "Gig version";
        case Preset::instrumentalBreak: return "Instrumental break";
    }
    return "Short version";
}

std::optional<Preset> parsePreset (std::string_view text)
{
    const auto t = lower (trim (text));
    for (auto p : kPresets)
        if (t == presetId (p) || t == lower (presetName (p)))
            return p;
    if (t == "short_version" || t == "compact" || t == "radio" || t == "radio_edit")
        return Preset::shortVersion;
    if (t == "long" || t == "build" || t == "longer_build")
        return Preset::longIntro;
    if (t == "live" || t == "gig_version")
        return Preset::gig;
    if (t == "break" || t == "solo" || t == "instrumental")
        return Preset::instrumentalBreak;
    return std::nullopt;
}

namespace
{
    std::vector<std::string> kinds (const std::vector<SongSection>& sections)
    {
        std::vector<std::string> out;
        for (const auto& s : sections)
            out.push_back (kindOf (s.name));
        return out;
    }

    int firstOf (const std::vector<std::string>& k, const char* kind, int from = 0)
    {
        for (int i = std::max (0, from); i < static_cast<int> (k.size()); ++i)
            if (k[static_cast<std::size_t> (i)] == kind)
                return i;
        return -1;
    }

    int lastOf (const std::vector<std::string>& k, const char* kind)
    {
        for (int i = static_cast<int> (k.size()) - 1; i >= 0; --i)
            if (k[static_cast<std::size_t> (i)] == kind)
                return i;
        return -1;
    }

    FormPart partOf (const SongSection& s) { return FormPart { s.name, 0, {}, Transition::automatic, {} }; }

    /** Does the track play in the section? */
    bool playsIn (const Song& song, TrackId id, const SongSection& sec)
    {
        const auto* t = edit::findTrack (song, id);
        if (t == nullptr)
            return false;
        if (sec.scene >= 0)
            return sec.scene < static_cast<int> (t->slots.size()) && t->slots[static_cast<std::size_t> (sec.scene)] != nullptr;
        for (const auto& c : t->clips)
            if (c->start < sec.end && c->end() > sec.start)
                for (const auto& n : edit::playedClip (*c).notes)
                    if (c->start + n.start >= sec.start && c->start + n.start < sec.end)
                        return true;
        return false;
    }
}

SongForm makePreset (const Song& song, const std::vector<SongSection>& sections, Preset preset, const DeviceLibrary* devices, TrackId melodyChoice)
{
    if (sections.empty())
        throw std::runtime_error ("The song has no sections yet. Find them or type them first.");
    const auto k = kinds (sections);
    const auto roles = arranging::guessRoles (song, devices, melodyChoice);
    TrackId melody = 0;
    std::vector<TrackId> notRhythm; ///< tracks other than drums and bass
    for (const auto& [id, r] : roles)
    {
        if (r == arranging::Role::melody)
            melody = id;
        if (r != arranging::Role::drums && r != arranging::Role::bass)
            notRhythm.push_back (id);
    }

    SongForm f;
    f.name = presetName (preset);
    const Tick bar = barOf (song);
    const int n = static_cast<int> (sections.size());
    const auto barsOf = [&] (int i) { return static_cast<int> (sections[static_cast<std::size_t> (i)].length() / bar); };
    const int intro = k.front() == "intro" ? 0 : -1;

    switch (preset)
    {
        case Preset::shortVersion:
        {
            const int verse = firstOf (k, "verse");
            const int firstChorus = firstOf (k, "chorus");
            const int lastChorus = lastOf (k, "chorus");
            const int outro = k.back() == "outro" && n > 1 ? n - 1 : -1;
            if (firstChorus >= 0)
            {
                if (intro >= 0)
                {
                    auto p = partOf (sections[0]);
                    if (barsOf (0) > 4)
                        p.bars = -4; // the end of the intro leads into the verse
                    f.parts.push_back (p);
                }
                if (verse >= 0 && verse < firstChorus)
                {
                    f.parts.push_back (partOf (sections[static_cast<std::size_t> (verse)]));
                    // The pre-chorus between them stays.
                    for (int i = verse + 1; i < firstChorus; ++i)
                        if (k[static_cast<std::size_t> (i)] == "prechorus")
                            f.parts.push_back (partOf (sections[static_cast<std::size_t> (i)]));
                }
                f.parts.push_back (partOf (sections[static_cast<std::size_t> (firstChorus)]));
                if (lastChorus > firstChorus)
                    f.parts.push_back (partOf (sections[static_cast<std::size_t> (lastChorus)]));
                if (outro >= 0)
                {
                    auto p = partOf (sections[static_cast<std::size_t> (outro)]);
                    if (barsOf (outro) > 8)
                        p.bars = 4;
                    f.parts.push_back (p);
                }
            }
            else
            {
                // No chorus by name: each kind of section once, then the last section.
                std::set<std::string> seen;
                for (int i = 0; i < n; ++i)
                    if (seen.insert (lower (baseName (sections[static_cast<std::size_t> (i)].name))).second)
                        f.parts.push_back (partOf (sections[static_cast<std::size_t> (i)]));
                if (! sameName (f.parts.back().section, sections.back().name))
                    f.parts.push_back (partOf (sections.back()));
                // Still the whole song: leave out the middle third.
                if (static_cast<int> (f.parts.size()) == n && n >= 4)
                    f.parts.erase (f.parts.begin() + n / 3, f.parts.begin() + n / 3 + std::max (1, n / 3));
            }
            break;
        }
        case Preset::longIntro:
        {
            const int source = intro >= 0 ? 0 : 0;
            auto build = partOf (sections[static_cast<std::size_t> (source)]);
            build.label = (intro >= 0 ? sections[0].name : std::string ("Intro")) + " (build)";
            if (intro < 0 && barsOf (0) > 4)
                build.bars = 4;
            // The build: drums and bass only, when they play there; else everything but the melody.
            bool rhythmPlays = false;
            for (const auto& [id, r] : roles)
                if ((r == arranging::Role::drums || r == arranging::Role::bass) && playsIn (song, id, sections[static_cast<std::size_t> (source)]))
                    rhythmPlays = true;
            if (rhythmPlays)
                build.muted = notRhythm;
            else if (melody != 0)
                build.muted = { melody };
            f.parts.push_back (build);
            for (int i = 0; i < n; ++i)
                f.parts.push_back (partOf (sections[static_cast<std::size_t> (i)]));
            break;
        }
        case Preset::gig:
        {
            if (intro >= 0)
            {
                auto vamp = partOf (sections[0]);
                vamp.label = sections[0].name + " (vamp)";
                f.parts.push_back (vamp);
            }
            else
            {
                auto countIn = partOf (sections[0]);
                countIn.label = "Intro";
                countIn.bars = std::min (4, std::max (1, barsOf (0)));
                if (melody != 0)
                    countIn.muted = { melody };
                f.parts.push_back (countIn);
            }
            int last = lastOf (k, "chorus");
            if (last < 0)
                last = k.back() == "outro" && n > 1 ? n - 2 : n - 1;
            for (int i = 0; i < n; ++i)
            {
                f.parts.push_back (partOf (sections[static_cast<std::size_t> (i)]));
                if (i == last)
                {
                    auto again = partOf (sections[static_cast<std::size_t> (i)]);
                    again.label = sections[static_cast<std::size_t> (i)].name + " (again)";
                    f.parts.push_back (again);
                }
            }
            f.ending = Transition::hold;
            break;
        }
        case Preset::instrumentalBreak:
        {
            int at = lastOf (k, "chorus");
            if (at < 0)
                at = k.back() == "outro" && n > 1 ? n - 1 : n;
            int source = firstOf (k, "verse");
            if (source < 0)
                source = at < n ? at : n - 1;
            for (int i = 0; i < n; ++i)
            {
                if (i == at)
                {
                    auto solo = partOf (sections[static_cast<std::size_t> (source)]);
                    solo.label = melody != 0 ? "Solo" : "Instrumental";
                    if (melody != 0)
                        solo.muted = { melody };
                    f.parts.push_back (solo);
                }
                f.parts.push_back (partOf (sections[static_cast<std::size_t> (i)]));
            }
            if (at >= n)
            {
                auto solo = partOf (sections[static_cast<std::size_t> (source)]);
                solo.label = melody != 0 ? "Solo" : "Instrumental";
                if (melody != 0)
                    solo.muted = { melody };
                f.parts.push_back (solo);
            }
            break;
        }
    }
    return f;
}

namespace
{
    const SongSection* sectionNamed (const std::vector<SongSection>& sections, std::string_view name)
    {
        for (const auto& s : sections)
            if (sameName (s.name, name))
                return &s;
        // "Chorus" finds "Chorus 1" when only the number is missing.
        for (const auto& s : sections)
            if (sameName (baseName (s.name), name))
                return &s;
        return nullptr;
    }
}

SongForm parseForm (const Song& song, const std::vector<SongSection>& sections, const std::string& name, std::string_view order)
{
    SongForm f;
    f.name = trim (name).empty() ? "New Form" : trim (name);
    std::string text (order);
    // Arrows work as commas.
    for (const char* arrow : { "\xE2\x86\x92", "->", ">", ";", "\n" })
        for (std::size_t p; (p = text.find (arrow)) != std::string::npos;)
            text.replace (p, std::string (arrow).size(), ",");
    const auto roles = arranging::guessRoles (song);
    std::istringstream in (text);
    std::string item;
    while (std::getline (in, item, ','))
    {
        item = trim (item);
        if (item.empty())
            continue;
        std::vector<TrackId> muted;
        if (const auto open = item.find ('('); open != std::string::npos)
        {
            const auto close = item.find (')', open);
            auto inside = lower (trim (item.substr (open + 1, close == std::string::npos ? std::string::npos : close - open - 1)));
            item = trim (item.substr (0, open) + (close == std::string::npos ? "" : item.substr (close + 1)));
            for (const char* word : { "no ", "without ", "mute ", "muted " })
                if (inside.rfind (word, 0) == 0)
                    inside = inside.substr (std::string (word).size());
            std::istringstream names (inside);
            std::string one;
            while (std::getline (names, one, '+'))
            {
                std::istringstream more (one);
                std::string w;
                while (std::getline (more, w, '&'))
                {
                    w = trim (w);
                    if (w.rfind ("and ", 0) == 0)
                        w = trim (w.substr (4));
                    if (w.empty())
                        continue;
                    bool found = false;
                    for (const auto& [id, r] : roles)
                        if (w == arranging::roleName (r) || (w == "vocal" && r == arranging::Role::melody))
                        {
                            muted.push_back (id);
                            found = true;
                        }
                    for (const auto& t : song.tracks)
                        if (! found && sameName (t.name, w))
                        {
                            muted.push_back (t.id);
                            found = true;
                        }
                    if (! found)
                        throw std::runtime_error ("No track or role \"" + w + "\" to rest. Use a track name, or melody, bass, drums, chords.");
                }
            }
        }
        int times = 1;
        {
            const auto l = lower (item);
            auto p = l.find_last_of ("x*");
            const auto times2 = l.rfind ("\xC3\x97"); // ×
            std::size_t cut = std::string::npos, digits = std::string::npos;
            if (times2 != std::string::npos && times2 + 2 < l.size())
            {
                cut = times2;
                digits = times2 + 2;
            }
            else if (p != std::string::npos && p + 1 < l.size() && p > 0 && l[p - 1] == ' ')
            {
                cut = p;
                digits = p + 1;
            }
            if (cut != std::string::npos)
            {
                const auto d = trim (l.substr (digits));
                if (! d.empty() && std::all_of (d.begin(), d.end(), [] (char c) { return std::isdigit (static_cast<unsigned char> (c)) != 0; }))
                {
                    times = std::clamp (std::stoi (d), 1, 16);
                    item = trim (item.substr (0, cut));
                }
            }
        }
        const auto* sec = sectionNamed (sections, item);
        if (sec == nullptr)
        {
            std::string names;
            for (const auto& s : sections)
                names += (names.empty() ? "" : ", ") + s.name;
            throw std::runtime_error ("No section \"" + item + "\". The sections are: " + names + ".");
        }
        for (int i = 0; i < times; ++i)
            f.parts.push_back (FormPart { sec->name, 0, muted, Transition::automatic, {} });
    }
    if (f.parts.empty())
        throw std::runtime_error ("Give the sections in order, like \"Intro, Verse 1, Chorus 1, Chorus 1, Outro\".");
    return f;
}

std::string describe (const SongForm& f)
{
    std::string out;
    for (std::size_t i = 0; i < f.parts.size();)
    {
        std::size_t j = i + 1;
        while (j < f.parts.size() && f.parts[j].section == f.parts[i].section && f.parts[j].muted == f.parts[i].muted && f.parts[j].bars == f.parts[i].bars
               && f.parts[j].label == f.parts[i].label)
            ++j;
        const auto& p = f.parts[i];
        out += (out.empty() ? "" : " \xE2\x86\x92 ") + (p.label.empty() ? p.section : p.label);
        if (p.bars != 0)
            out += " (" + std::string (p.bars > 0 ? "first " : "last ") + std::to_string (std::abs (p.bars)) + " bars)";
        if (j - i > 1)
            out += " \xC3\x97" + std::to_string (j - i);
        i = j;
    }
    return out;
}

const char* transitionId (Transition t)
{
    switch (t)
    {
        case Transition::automatic: return "automatic";
        case Transition::none: return "none";
        case Transition::fill: return "fill";
        case Transition::pickup: return "pickup";
        case Transition::hold: return "hold";
        case Transition::stop: return "stop";
    }
    return "automatic";
}

std::string transitionName (Transition t)
{
    switch (t)
    {
        case Transition::automatic: return "Automatic";
        case Transition::none: return "Straight cut";
        case Transition::fill: return "Drum fill";
        case Transition::pickup: return "Lead-in";
        case Transition::hold: return "Hold the chord";
        case Transition::stop: return "Break";
    }
    return "Automatic";
}

std::optional<Transition> parseTransition (std::string_view text)
{
    const auto t = lower (trim (text));
    for (auto k : { Transition::automatic, Transition::none, Transition::fill, Transition::pickup, Transition::hold, Transition::stop })
        if (t == transitionId (k) || t == lower (transitionName (k)))
            return k;
    if (t == "auto")
        return Transition::automatic;
    if (t == "cut" || t == "straight")
        return Transition::none;
    if (t == "lead_in" || t == "lead-in" || t == "leadin" || t == "walk_up")
        return Transition::pickup;
    if (t == "held_note" || t == "held" || t == "final_chord" || t == "ring")
        return Transition::hold;
    if (t == "rest" || t == "break")
        return Transition::stop;
    return std::nullopt;
}

// ---- rendering -------------------------------------------------------------------------
namespace
{
    struct Placed
    {
        const SongSection* section = nullptr;
        std::string name;  ///< the part's name in the form
        Tick from = 0, to = 0; ///< in the source (for a scene: in the looped scene, from 0)
        Tick at = 0;       ///< in the form
        std::set<TrackId> muted;
        Transition into = Transition::automatic;
        bool naturalNext = false; ///< the next part continues it in the original
        Tick length() const { return to - from; }
    };

    /** The notes of a source lane that belong to [from, to): pushes just before `from` come in, pushes just before `to` go out. */
    void placeNotes (const Lane& src, Lane& out, Tick from, Tick to, Tick at, bool keepTail, bool takePushes)
    {
        for (const auto& n : src.notes)
        {
            const bool inside = n.start >= from && n.start < to;
            const bool pushIn = takePushes && n.start < from && n.start >= from - kPush && n.end() > from + kPush;
            const bool pushOut = n.start >= to - kPush && n.end() > to + kPush && ! keepTail;
            if ((! inside && ! pushIn) || (inside && pushOut))
                continue;
            Note c = n;
            c.start = at + (n.start - from);
            if (! keepTail)
                c.length = std::max<Tick> (1, std::min (n.end(), to) - n.start);
            if (c.start < 0)
                continue;
            out.notes.push_back (c);
        }
        // The controls: where they stand at the start, then those inside.
        std::map<std::pair<int, int>, ControlEvent> state;
        for (const auto& e : src.controls)
        {
            if (e.at >= from)
                break;
            state[{ static_cast<int> (e.kind), e.number }] = e;
        }
        for (auto [key, e] : state)
        {
            e.at = at;
            out.controls.push_back (e);
        }
        bool pedal = state.count ({ static_cast<int> (ControlEvent::Kind::controller), 64 }) != 0
                     && state[{ static_cast<int> (ControlEvent::Kind::controller), 64 }].value >= 64;
        for (const auto& e : src.controls)
            if (e.at >= from && e.at < to)
            {
                ControlEvent c = e;
                c.at = at + (e.at - from);
                out.controls.push_back (c);
                if (e.kind == ControlEvent::Kind::controller && e.number == 64)
                    pedal = e.value >= 64;
            }
        if (pedal && ! keepTail)
            out.controls.push_back ({ at + (to - from) - 1, ControlEvent::Kind::controller, 64, 0 });
    }

    /** Which GM drum notes to use for a fill on this track. */
    struct DrumNotes
    {
        int kick = 36, snare = 38, crash = 49;
        std::vector<int> toms; ///< high to low
    };

    DrumNotes drumNotesFor (const Track& t, const Lane& lane)
    {
        DrumNotes d;
        std::map<int, int> used;
        for (const auto& n : lane.notes)
            ++used[n.pitch];
        const auto has = [&] (int p) {
            if (t.output.kind == TrackOutput::Kind::sampler)
                return t.kit.padForNote (p) != nullptr;
            return true; // a General MIDI kit has them all
        };
        const auto most = [&] (std::initializer_list<int> choices, int otherwise) {
            int best = -1, count = -1;
            for (int p : choices)
                if (has (p) && (used.count (p) != 0 ? used[p] : 0) > count)
                {
                    best = p;
                    count = used.count (p) != 0 ? used[p] : 0;
                }
            return best >= 0 ? best : otherwise;
        };
        d.kick = most ({ 36, 35 }, 36);
        d.snare = most ({ 38, 40, 37 }, 38);
        d.crash = most ({ 49, 57, 55, 52 }, 49);
        for (int p : { 50, 48, 47, 45, 43, 41 })
            if (has (p))
                d.toms.push_back (p);
        if (t.output.kind == TrackOutput::Kind::sampler && d.toms.empty())
            d.toms = { d.snare };
        return d;
    }

    std::optional<theory::Key> keyOf (const Song& s, const std::vector<Lane>& lanes, const std::vector<bool>& drums, Tick from, Tick to)
    {
        std::vector<Note> notes;
        for (std::size_t i = 0; i < lanes.size(); ++i)
            if (! drums[i])
                for (const auto& n : lanes[i].notes)
                    if (n.start >= from && n.start < to)
                        notes.push_back (n);
        if (const auto guess = theory::findKey (notes, from, to); guess && guess->score > 0.5)
            return guess->key;
        if (const auto k = theory::parseKey (s.key))
            return *k;
        return std::nullopt;
    }

    int nearest (int pitchClass, int around)
    {
        int best = around;
        int bestDist = 99;
        for (int p = around - 6; p <= around + 6; ++p)
            if (((p % 12) + 12) % 12 == pitchClass && std::abs (p - around) < bestDist)
            {
                best = p;
                bestDist = std::abs (p - around);
            }
        return std::clamp (best, 0, 127);
    }
}

Rendered render (const Song& src, const SongForm& form, const std::vector<SongSection>& sections, const DeviceLibrary* devices)
{
    if (form.parts.empty())
        throw std::runtime_error ("The form \"" + form.name + "\" has no parts.");
    Rendered r;
    const Tick bar = barOf (src);
    const Tick beat = beatOf (src);
    const Tick half = std::max<Tick> (beat, beat * (src.timeSignature.numerator / 2));

    // ---- where each part goes
    std::vector<Placed> parts;
    Tick at = 0;
    std::map<std::string, int> labelCount;
    for (const auto& p : form.parts)
    {
        const auto* sec = sectionNamed (sections, p.section);
        if (sec == nullptr)
            throw std::runtime_error ("The form \"" + form.name + "\" uses a section \"" + p.section + "\" that is not there. Change the form or the sections.");
        Placed pl;
        pl.section = sec;
        pl.name = p.label.empty() ? sec->name : p.label;
        const Tick start = sec->scene >= 0 ? 0 : sec->start;
        const Tick end = sec->scene >= 0 ? sec->length() : sec->end;
        pl.from = start;
        pl.to = end;
        if (p.bars > 0)
            pl.to = std::min (end, start + bar * p.bars);
        else if (p.bars < 0)
            pl.from = std::max (start, end + bar * p.bars);
        pl.at = at;
        pl.muted = std::set<TrackId> (p.muted.begin(), p.muted.end());
        pl.into = p.into;
        at += pl.length();
        parts.push_back (pl);
    }
    for (std::size_t i = 0; i + 1 < parts.size(); ++i)
        parts[i].naturalNext = parts[i].section->scene < 0 && parts[i + 1].section->scene < 0 && parts[i].to == parts[i + 1].from;
    r.length = at;

    // ---- the source, track by track
    const auto roles = arranging::guessRoles (src, devices);
    const auto roleOf = [&] (TrackId id) {
        const auto it = roles.find (id);
        return it != roles.end() ? it->second : arranging::Role::other;
    };
    std::vector<Lane> source;
    std::vector<bool> drums;
    for (const auto& t : src.tracks)
    {
        source.push_back (isMidi (t) ? timelineLane (t) : Lane {});
        drums.push_back (isMidi (t) && (arranging::isDrumTrack (t, devices) || roleOf (t.id) == arranging::Role::drums));
    }

    // ---- the parts, written out
    std::vector<Lane> out (src.tracks.size());
    for (std::size_t pi = 0; pi < parts.size(); ++pi)
    {
        const auto& p = parts[pi];
        const bool naturalPrev = pi > 0 && parts[pi - 1].naturalNext;
        for (std::size_t ti = 0; ti < src.tracks.size(); ++ti)
        {
            const auto& t = src.tracks[ti];
            if (! isMidi (t) || p.muted.count (t.id) != 0)
                continue;
            if (p.section->scene >= 0)
                placeNotes (sceneLane (t, p.section->scene, p.section->length()), out[ti], p.from, p.to, p.at, false, false);
            else
                placeNotes (source[ti], out[ti], p.from, p.to, p.at, p.naturalNext, ! naturalPrev && p.from > 0);
        }
    }

    // ---- the joins
    const auto energyAt = [&] (Tick from, Tick to) {
        double e = 0.0;
        for (const auto& l : out)
            e += energy (l, from, to);
        return e;
    };
    const auto drumTrack = [&] (const std::set<TrackId>& muted) -> int {
        int best = -1;
        std::size_t most = 0;
        for (std::size_t ti = 0; ti < src.tracks.size(); ++ti)
            if (drums[ti] && muted.count (src.tracks[ti].id) == 0 && source[ti].notes.size() > most)
            {
                most = source[ti].notes.size();
                best = static_cast<int> (ti);
            }
        return best;
    };
    const auto bassTrack = [&] (const std::set<TrackId>& muted) -> int {
        for (std::size_t ti = 0; ti < src.tracks.size(); ++ti)
            if (roleOf (src.tracks[ti].id) == arranging::Role::bass && ! drums[ti] && muted.count (src.tracks[ti].id) == 0 && ! source[ti].notes.empty())
                return static_cast<int> (ti);
        return -1;
    };

    // The style's fills: by letter (A..D), the rhythm parts of "Fill In AA" and so on.
    std::map<char, std::vector<std::pair<int, Lane>>> styleFills; ///< letter -> (source channel, notes from 0)
    Tick styleFillLength = bar;
    if (src.style)
        for (int scene = 0; scene < static_cast<int> (src.scenes.size()); ++scene)
        {
            const auto name = lower (trim (src.scenes[static_cast<std::size_t> (scene)]));
            if (name.rfind ("fill in ", 0) != 0 || name.size() < 10 || name[8] != name[9])
                continue;
            const char letter = static_cast<char> (std::toupper (static_cast<unsigned char> (name[8])));
            for (const auto& t : src.tracks)
                if ((t.styleChannel == 9 || t.styleChannel == 10) && scene < static_cast<int> (t.slots.size()) && t.slots[static_cast<std::size_t> (scene)] != nullptr)
                {
                    const Tick length = std::min (bar, t.slots[static_cast<std::size_t> (scene)]->length);
                    styleFillLength = length;
                    styleFills[letter].emplace_back (t.styleChannel, sceneLane (t, scene, length));
                }
        }
    // Section energy, to choose the fill that leads to a section like it (A quiet .. D busy).
    std::vector<double> sectionEnergy;
    for (const auto& sec : sections)
    {
        double e = 0.0;
        if (sec.scene < 0)
            for (const auto& l : source)
                e += energy (l, sec.start, sec.end);
        sectionEnergy.push_back (e / std::max (1.0, static_cast<double> (sec.length()) / static_cast<double> (bar)));
    }
    const auto fillLetter = [&] (const SongSection* sec) -> char {
        if (styleFills.empty())
            return 0;
        std::vector<double> sorted = sectionEnergy;
        std::sort (sorted.begin(), sorted.end());
        double e = 0.0;
        for (std::size_t i = 0; i < sections.size(); ++i)
            if (&sections[i] == sec)
                e = sectionEnergy[i];
        const auto rank = static_cast<double> (std::lower_bound (sorted.begin(), sorted.end(), e) - sorted.begin()) / static_cast<double> (std::max<std::size_t> (1, sorted.size()));
        std::vector<char> letters;
        for (const auto& [l, unused] : styleFills)
            letters.push_back (l);
        const auto i = std::min (letters.size() - 1, static_cast<std::size_t> (rank * static_cast<double> (letters.size())));
        return letters[i];
    };

    struct Join
    {
        double ratio = 1.0;
        bool leadIn = false;
    };
    std::vector<Join> joins;
    for (std::size_t pi = 1; pi < parts.size(); ++pi)
    {
        const auto& a = parts[pi - 1];
        const auto& b = parts[pi];
        Boundary bd;
        bd.part = static_cast<int> (pi);
        bd.at = b.at;
        bd.from = a.name;
        bd.to = b.name;
        bd.natural = a.naturalNext && a.muted == b.muted;
        const double ea = energyAt (b.at - std::min (bar, a.length()), b.at) / std::max (0.25, static_cast<double> (std::min (bar, a.length())) / static_cast<double> (bar));
        const double eb = energyAt (b.at, b.at + std::min (bar, b.length())) / std::max (0.25, static_cast<double> (std::min (bar, b.length())) / static_cast<double> (bar));
        const double ratio = (eb + 1.0) / (ea + 1.0);
        const int dt = drumTrack (b.muted);
        const bool hasDrums = dt >= 0 && (noteCount (out[static_cast<std::size_t> (dt)], b.at - bar, b.at + bar) > 0);
        const bool hasBass = bassTrack (b.muted) >= 0;
        // What led into the next section in the original.
        const bool leadIn = b.section->scene < 0 && b.from >= half && b.from == b.section->start && ! (a.section == b.section && a.to == b.from);
        bool leadInFill = false;
        if (leadIn && dt >= 0)
        {
            const auto& d = source[static_cast<std::size_t> (dt)];
            const double before = static_cast<double> (noteCount (d, b.from - half, b.from)) / static_cast<double> (half);
            const double usual = static_cast<double> (noteCount (d, std::max<Tick> (0, b.from - bar * 2), b.from - half)) / static_cast<double> (std::max<Tick> (1, std::min (b.from - half, bar * 2 - half)));
            leadInFill = before > usual * 1.3 && before > 0.0;
        }

        if (bd.natural)
        {
            bd.recommended = Transition::none;
            bd.why = "As in the original.";
        }
        else if (leadIn && leadInFill)
        {
            bd.recommended = Transition::pickup;
            bd.why = "In the original a fill leads into " + b.section->name + (b.name != b.section->name ? " (the music of " + b.name + ")" : std::string())
                     + ". The lead-in uses it.";
        }
        else if (a.section == b.section)
        {
            bd.recommended = hasDrums ? Transition::fill : Transition::none;
            bd.why = hasDrums ? "The same section again. A drum fill marks the repeat." : "The same section again. It loops as it is.";
        }
        else if (ratio >= 1.3 && hasDrums)
        {
            bd.recommended = Transition::fill;
            bd.why = b.name + " is busier than the end of " + a.name + ". A drum fill builds into it.";
        }
        else if (ratio <= 0.7)
        {
            bd.recommended = Transition::hold;
            bd.why = b.name + " is quieter than " + a.name + ". The band holds the chord, so the drop does not sound like a cut.";
        }
        else if (leadIn)
        {
            bd.recommended = Transition::pickup;
            bd.why = "The lead-in of " + b.section->name + " from the original fits both sides.";
        }
        else if (hasDrums)
        {
            bd.recommended = Transition::fill;
            bd.why = "A new join. A drum fill joins the two sections.";
        }
        else if (hasBass)
        {
            bd.recommended = Transition::pickup;
            bd.why = "A new join with no drums. The bass walks up to " + b.name + ".";
        }
        else
        {
            bd.recommended = Transition::hold;
            bd.why = "A new join with no drums or bass. Holding the chord makes it smooth.";
        }
        bd.options.push_back (bd.recommended);
        for (auto k : { Transition::fill, Transition::pickup, Transition::hold, Transition::stop, Transition::none })
            if (k != bd.recommended && (k != Transition::fill || dt >= 0) && (k != Transition::pickup || leadIn || hasBass))
                bd.options.push_back (k);
        bd.chosen = b.into == Transition::automatic ? bd.recommended : b.into;
        r.boundaries.push_back (bd);
        joins.push_back ({ ratio, leadIn });
    }


    // ---- the transitions, written into the notes at the end of the part before each join
    for (std::size_t bi = 0; bi < r.boundaries.size(); ++bi)
    {
        const auto& bd = r.boundaries[bi];
        const auto& a = parts[static_cast<std::size_t> (bd.part - 1)];
        const auto& b = parts[static_cast<std::size_t> (bd.part)];
        const Tick j = bd.at;
        const Tick room = std::min (a.length(), bar);
        switch (bd.chosen)
        {
            case Transition::automatic:
            case Transition::none:
                break;
            case Transition::fill:
            {
                const int dt = drumTrack (b.muted);
                if (dt < 0)
                {
                    r.notes.push_back ("No drum track: the fill into " + b.name + " was left out.");
                    break;
                }
                auto& lane = out[static_cast<std::size_t> (dt)];
                const auto& track = src.tracks[static_cast<std::size_t> (dt)];
                const double vel = meanVelocity (lane, j - bar, j, 96.0);
                const auto d = drumNotesFor (track, source[static_cast<std::size_t> (dt)]);
                if (const char letter = fillLetter (b.section); letter != 0)
                {
                    // The style's own Fill In: its rhythm parts on the drum track.
                    const Tick length = std::min (room, styleFillLength);
                    clearWindow (lane, j - length, j);
                    for (const auto& [channel, notes] : styleFills[letter])
                        for (auto n : notes.notes)
                            if (n.start >= styleFillLength - length)
                            {
                                n.start = j - styleFillLength + n.start;
                                n.length = std::max<Tick> (1, std::min (n.length, j - n.start));
                                lane.notes.push_back (n);
                            }
                    r.notes.push_back ("The fill into " + b.name + " is the style's Fill In " + std::string (2, letter) + ".");
                }
                else
                {
                    // A fill of sixteenths, snare to the low toms, louder to the end: one beat, or two into a big lift.
                    const Tick length = std::min (room, joins[bi].ratio >= 1.6 || kindOf (a.name) == "intro" ? beat * 2 : beat);
                    const Tick step = kTicksPerQuarter / 4;
                    const auto steps = static_cast<int> (length / step);
                    // Keep the kick on the first beat of the fill, clear the rest.
                    clearWindow (lane, j - length, j);
                    std::vector<int> run;
                    const auto& toms = d.toms;
                    for (int i = 0; i < steps; ++i)
                    {
                        const double t = steps > 1 ? static_cast<double> (i) / (steps - 1) : 1.0;
                        if (t < 0.25 || toms.empty())
                            run.push_back (d.snare);
                        else
                        {
                            const auto k = std::min (toms.size() - 1, static_cast<std::size_t> ((t - 0.25) / 0.75 * static_cast<double> (toms.size())));
                            run.push_back (toms[k]);
                        }
                    }
                    for (int i = 0; i < steps; ++i)
                    {
                        const Tick when = j - length + step * i;
                        const double t = steps > 1 ? static_cast<double> (i) / (steps - 1) : 1.0;
                        // Accents on the beats, a crescendo through the fill.
                        const double accent = (when % beat == 0) ? 1.08 : 0.94;
                        lane.notes.push_back ({ when, step - 10, static_cast<std::uint8_t> (run[static_cast<std::size_t> (i)]),
                                                static_cast<std::uint8_t> (clampVelocity (vel * (0.78 + 0.4 * t) * accent)) });
                        if (when % beat == 0)
                            lane.notes.push_back ({ when, step - 10, static_cast<std::uint8_t> (d.kick), static_cast<std::uint8_t> (clampVelocity (vel)) });
                    }
                }
                // A crash on the first beat of the next part, when it has none.
                bool crash = false;
                for (const auto& n : lane.notes)
                    if (n.start >= j && n.start < j + kPush && (n.pitch == 49 || n.pitch == 57 || n.pitch == 55 || n.pitch == 52))
                        crash = true;
                if (! crash)
                {
                    lane.notes.push_back ({ j, beat, static_cast<std::uint8_t> (d.crash), static_cast<std::uint8_t> (clampVelocity (vel * 1.15)) });
                    bool kick = false;
                    for (const auto& n : lane.notes)
                        if (n.start >= j && n.start < j + kPush && (n.pitch == 35 || n.pitch == 36))
                            kick = true;
                    if (! kick)
                        lane.notes.push_back ({ j, beat / 2, static_cast<std::uint8_t> (d.kick), static_cast<std::uint8_t> (clampVelocity (vel * 1.1)) });
                }
                break;
            }
            case Transition::pickup:
            {
                const Tick window = std::min (room, half);
                if (joins[bi].leadIn)
                {
                    // The bars before the section in the original: what it expects to come from.
                    for (std::size_t ti = 0; ti < src.tracks.size(); ++ti)
                    {
                        const auto& t = src.tracks[ti];
                        if (! isMidi (t) || b.muted.count (t.id) != 0)
                            continue;
                        auto& lane = out[ti];
                        clearWindow (lane, j - window, j);
                        for (const auto& n : source[ti].notes)
                        {
                            if (n.start < b.from - window || n.start >= b.from)
                                continue;
                            if (n.start >= b.from - kPush && n.end() > b.from + kPush)
                                continue; // a push: already in the part
                            Note c = n;
                            c.start = j - (b.from - n.start);
                            lane.notes.push_back (c);
                        }
                    }
                }
                else if (const int bt = bassTrack (b.muted); bt >= 0)
                {
                    // A bass walk-up: two eighths into the first note of the next part.
                    auto& lane = out[static_cast<std::size_t> (bt)];
                    int target = -1;
                    for (const auto& n : lane.notes)
                        if (n.start >= j && n.start < j + beat)
                        {
                            target = n.pitch;
                            break;
                        }
                    if (target < 0)
                    {
                        r.notes.push_back ("The bass does not play at the start of " + b.name + ": no walk-up.");
                        break;
                    }
                    const double vel = meanVelocity (lane, j - bar, j, 90.0);
                    const Tick w = std::min (room, beat);
                    clearWindow (lane, j - w, j);
                    const int first = target - 2 >= 24 ? target - 2 : target + 2;
                    const int second = target - 1 >= 24 ? target - 1 : target + 1;
                    lane.notes.push_back ({ j - w, w / 2 - 10, static_cast<std::uint8_t> (first), static_cast<std::uint8_t> (clampVelocity (vel * 0.92)) });
                    lane.notes.push_back ({ j - w / 2, w / 2 - 10, static_cast<std::uint8_t> (second), static_cast<std::uint8_t> (clampVelocity (vel)) });
                }
                else
                    r.notes.push_back ("No lead-in or bass for the join into " + b.name + ".");
                break;
            }
            case Transition::hold:
            {
                const Tick window = std::min (room, half);
                const Tick holdAt = j - window;
                for (std::size_t ti = 0; ti < src.tracks.size(); ++ti)
                {
                    auto& lane = out[ti];
                    if (drums[ti])
                    {
                        const auto d = drumNotesFor (src.tracks[ti], source[ti]);
                        const double vel = meanVelocity (lane, j - bar, j, 96.0);
                        const bool played = noteCount (lane, j - bar, j) > 0;
                        clearWindow (lane, holdAt, j);
                        if (played)
                        {
                            lane.notes.push_back ({ holdAt, beat, static_cast<std::uint8_t> (d.crash), static_cast<std::uint8_t> (clampVelocity (vel * 1.05)) });
                            lane.notes.push_back ({ holdAt, beat / 2, static_cast<std::uint8_t> (d.kick), static_cast<std::uint8_t> (clampVelocity (vel)) });
                        }
                        continue;
                    }
                    // The notes that sound at the hold (or start just after it) ring to the join; the rest go.
                    std::vector<Note> kept;
                    for (auto n : lane.notes)
                    {
                        if (n.start >= holdAt + kPush && n.start < j)
                            continue;
                        if (n.start < holdAt + kPush && n.end() > holdAt && n.start < j)
                            n.length = std::max<Tick> (n.length, j - n.start - 10);
                        kept.push_back (n);
                    }
                    lane.notes = std::move (kept);
                }
                break;
            }
            case Transition::stop:
            {
                const Tick window = std::min (room, beat);
                for (auto& lane : out)
                    clearWindow (lane, j - window, j);
                break;
            }
        }
    }

    // ---- the ending
    const auto& last = parts.back();
    r.ending = form.ending;
    if (r.ending == Transition::automatic)
        r.ending = kindOf (last.name) == "outro" || kindOf (last.section->name) == "outro" ? Transition::none : Transition::hold;
    if (r.ending != Transition::hold)
        r.ending = Transition::none;
    if (r.ending == Transition::hold)
    {
        // A final chord of one bar in the song's key, each part in its own register.
        const Tick end = r.length;
        const auto key = keyOf (src, out, drums, std::max<Tick> (0, end - bar * 8), end);
        if (! key)
        {
            r.notes.push_back ("The key is not clear: no final chord.");
            r.ending = Transition::none;
        }
        else
        {
            const int root = key->tonic;
            const bool minor = key->mode == theory::Mode::minor || key->mode == theory::Mode::dorian || key->mode == theory::Mode::phrygian || key->mode == theory::Mode::locrian;
            const int third = (root + (minor ? 3 : 4)) % 12, fifth = (root + 7) % 12;
            for (std::size_t ti = 0; ti < src.tracks.size(); ++ti)
            {
                auto& lane = out[ti];
                if (last.muted.count (src.tracks[ti].id) != 0 || noteCount (lane, end - bar, end) == 0)
                    continue;
                const double vel = meanVelocity (lane, end - bar, end, 90.0);
                if (drums[ti])
                {
                    const auto d = drumNotesFor (src.tracks[ti], source[ti]);
                    lane.notes.push_back ({ end, bar, static_cast<std::uint8_t> (d.crash), static_cast<std::uint8_t> (clampVelocity (vel * 1.1)) });
                    lane.notes.push_back ({ end, beat, static_cast<std::uint8_t> (d.kick), static_cast<std::uint8_t> (clampVelocity (vel * 1.05)) });
                    continue;
                }
                int lastPitch = 60, low = 127, high = 0;
                double sum = 0.0;
                int count = 0;
                for (const auto& n : lane.notes)
                    if (n.start >= end - bar && n.start < end)
                    {
                        lastPitch = n.pitch;
                        low = std::min<int> (low, n.pitch);
                        high = std::max<int> (high, n.pitch);
                        sum += n.pitch;
                        ++count;
                    }
                const int mean = static_cast<int> (std::lround (sum / std::max (1, count)));
                std::vector<int> pitches;
                switch (roleOf (src.tracks[ti].id))
                {
                    case arranging::Role::bass: pitches = { nearest (root, low) }; break;
                    case arranging::Role::melody: pitches = { nearest (root, lastPitch) }; break;
                    case arranging::Role::chords:
                    {
                        const int r0 = nearest (root, mean - 4);
                        pitches = { r0, nearest (third, r0 + 4), nearest (fifth, r0 + 7) };
                        if (high - low >= 12)
                            pitches.push_back (r0 + 12);
                        break;
                    }
                    default:
                    {
                        const int r0 = nearest (root, mean);
                        pitches = { r0, nearest (fifth, r0 + 7) };
                        break;
                    }
                }
                for (int p : pitches)
                    lane.notes.push_back ({ end, bar - kPush, static_cast<std::uint8_t> (std::clamp (p, 0, 127)), static_cast<std::uint8_t> (clampVelocity (vel)) });
            }
            r.length += bar;
        }
    }

    // ---- the song as it would be
    Song& song = r.song;
    song = src;
    song.forms = src.forms;
    song.loop.enabled = false;
    std::vector<TempoPoint> tempo;
    for (std::size_t ti = 0; ti < song.tracks.size(); ++ti)
    {
        auto& t = song.tracks[ti];
        const auto& from = src.tracks[ti];
        t.clips.clear();
        t.audio.clear();
        t.marks.clear();
        // The MIDI: one clip for the whole form.
        if (isMidi (from) && ! out[ti].notes.empty())
        {
            Clip clip;
            clip.name = form.name;
            clip.start = 0;
            clip.length = r.length;
            for (auto n : out[ti].notes)
                if (n.start >= 0 && n.start < r.length)
                {
                    n.length = std::max<Tick> (1, std::min (n.length, r.length - n.start));
                    clip.notes.push_back (n);
                }
            for (const auto& e : out[ti].controls)
                if (e.at >= 0 && e.at < r.length)
                    clip.controls.push_back (e);
            // Two notes of the same pitch at once (a fill over a kept note): one is enough.
            edit::normalise (clip);
            edit::removeOverlaps (clip);
            edit::addClip (song, t.id, std::move (clip));
        }
    }

    // Audio, automation, chords and lyrics, and the tempo: part by part.
    for (std::size_t pi = 0; pi < parts.size(); ++pi)
    {
        const auto& p = parts[pi];
        const bool cutIn = pi == 0 || ! parts[pi - 1].naturalNext;
        const bool cutOut = ! p.naturalNext;
        const bool timeline = p.section->scene < 0;
        if (timeline)
        {
            if (tempo.empty() || std::abs (tempo.back().bpm - src.tempo.tempoAt (static_cast<double> (p.from))) > 1e-6)
                tempo.push_back ({ p.at, src.tempo.tempoAt (static_cast<double> (p.from)) });
            for (const auto& tp : src.tempo.getPoints())
                if (tp.at > p.from && tp.at < p.to)
                    tempo.push_back ({ p.at + (tp.at - p.from), tp.bpm });
        }
        for (std::size_t ti = 0; ti < song.tracks.size(); ++ti)
        {
            auto& t = song.tracks[ti];
            const auto& from = src.tracks[ti];
            if (! timeline)
                continue;
            // Audio clips, cut to the part with short fades where the part starts or ends a cut.
            if (! isMidi (from) && p.muted.count (from.id) == 0)
                for (const auto& c : from.audio)
                {
                    const Tick a0 = std::max (c->start, p.from), a1 = std::min (c->end(), p.to);
                    if (a1 <= a0)
                        continue;
                    AudioClip piece = *c;
                    piece.id = 0;
                    piece.start = p.at + (a0 - p.from);
                    piece.length = a1 - a0;
                    piece.offset = c->offset + (src.tempo.secondsAt (static_cast<double> (a0)) - src.tempo.secondsAt (static_cast<double> (c->start))) * edit::audioSpeed (src, *c);
                    const Tick fade = kTicksPerQuarter / 16;
                    if (a0 > c->start)
                        piece.fadeIn = cutIn ? fade : 0;
                    if (a1 < c->end())
                        piece.fadeOut = cutOut ? fade : 0;
                    if (a0 == p.from && cutIn)
                        piece.fadeIn = std::max (piece.fadeIn, fade);
                    if (a1 == p.to && cutOut)
                        piece.fadeOut = std::max (piece.fadeOut, fade);
                    edit::addAudioClip (song, t.id, std::move (piece));
                }
            // Automation lines: the value at the start, the points inside, the value at the end.
            for (int lane = 0; lane < from.laneCount(); ++lane)
            {
                const auto* line = from.findLine (lane);
                auto* to = t.findLine (lane);
                if (line == nullptr || to == nullptr || line->empty())
                    continue;
                if (pi == 0)
                    to->clear();
                const float whenEmpty = static_cast<float> (from.mixerValue (lane));
                to->push_back ({ p.at, automation::valueAt (*line, static_cast<double> (p.from), whenEmpty) });
                for (const auto& pt : *line)
                    if (pt.at > p.from && pt.at < p.to)
                        to->push_back ({ p.at + (pt.at - p.from), pt.value });
                to->push_back ({ p.at + p.length() - 1, automation::valueAt (*line, static_cast<double> (p.to - 1), whenEmpty) });
            }
            // Score marks: the chord at the start, then the marks inside.
            if (p.muted.count (from.id) == 0)
            {
                const ScoreMark* chord = nullptr;
                bool chordAtStart = false;
                for (const auto& m : from.marks)
                {
                    if (m.kind == ScoreMark::Kind::chord && m.at <= p.from)
                    {
                        chord = &m;
                        chordAtStart = m.at == p.from;
                    }
                    if (m.at >= p.from && m.at < p.to)
                    {
                        ScoreMark c = m;
                        c.at = p.at + (m.at - p.from);
                        t.marks.push_back (c);
                    }
                }
                if (chord != nullptr && ! chordAtStart)
                    t.marks.push_back ({ ScoreMark::Kind::chord, p.at, chord->text });
            }
        }
    }
    for (auto& t : song.tracks)
    {
        std::stable_sort (t.marks.begin(), t.marks.end(), [] (const ScoreMark& x, const ScoreMark& y) { return x.at < y.at; });
        for (int lane = 0; lane < t.laneCount(); ++lane)
            if (auto* line = t.findLine (lane); line != nullptr && ! line->empty())
            {
                std::stable_sort (line->begin(), line->end(), [] (const AutomationPoint& x, const AutomationPoint& y) { return x.at < y.at; });
                line->erase (std::unique (line->begin(), line->end(), [] (const AutomationPoint& x, const AutomationPoint& y) { return x.at == y.at; }), line->end());
            }
    }
    if (tempo.empty())
        tempo.push_back ({ 0, src.baseTempo() });
    tempo.front().at = 0;
    song.tempo.setPoints (tempo);

    // The sections follow the parts.
    song.sections.clear();
    std::map<std::string, int> total, seen;
    for (const auto& p : parts)
        ++total[lower (p.name)];
    for (const auto& p : parts)
    {
        SongSection sec;
        sec.name = total[lower (p.name)] > 1 ? p.name + " (" + std::to_string (++seen[lower (p.name)]) + ")" : p.name;
        sec.start = p.at;
        sec.end = p.at + p.length();
        song.sections.push_back (sec);
    }
    if (r.ending == Transition::hold)
        song.sections.push_back ({ "Ending", r.length - bar, r.length, -1 });
    return r;
}

Song use (const Song& song, const SongForm& f, const std::vector<SongSection>& sections, const DeviceLibrary* devices)
{
    auto out = render (song, f, sections, devices).song;
    out.forms.clear();
    return out;
}

const SongForm* findForm (const Song& song, std::string_view name)
{
    for (const auto& f : song.forms)
        if (sameName (f.name, name))
            return &f;
    return nullptr;
}

void putForm (Song& song, SongForm f)
{
    f.name = trim (f.name).empty() ? "New Form" : trim (f.name);
    for (auto& g : song.forms)
        if (sameName (g.name, f.name))
        {
            g = std::move (f);
            return;
        }
    song.forms.push_back (std::move (f));
}

} // namespace arranger::form

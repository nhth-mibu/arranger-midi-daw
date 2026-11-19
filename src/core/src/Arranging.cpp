#include "arranger/core/Arranging.h"

#include "ArrangingDetail.h"

#include "arranger/core/GeneralMidi.h"
#include "arranger/core/SongEdit.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <set>
#include <stdexcept>

namespace arranger::arranging
{

// ---- helpers ---------------------------------------------------------------------
namespace detail
{
    Tick songEnd (const Song& s)
    {
        const Tick bar = barLength (s);
        return std::max<Tick> (bar, (s.contentEnd() + bar - 1) / bar * bar);
    }

    std::string trackName (const Song& s, TrackId id)
    {
        const auto* t = edit::findTrack (s, id);
        return t != nullptr ? t->name : "a track";
    }

    std::string lower (std::string s)
    {
        for (auto& c : s)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return s;
    }

    bool nameHas (const std::string& name, std::initializer_list<const char*> words)
    {
        const auto n = lower (name);
        for (const auto* w : words)
            if (n.find (w) != std::string::npos)
                return true;
        return false;
    }

    std::string pitchText (int pitch)
    {
        return std::string (pitchClassName (pitch)) + std::to_string (pitch / 12 - 1);
    }

    int editNotes (Track& t, Tick from, Tick to, const std::function<bool (Note&)>& change)
    {
        int changed = 0;
        for (auto& ptr : t.clips)
        {
            if (ptr == nullptr || ptr->end() <= from || ptr->start >= to)
                continue;
            Clip c = edit::playedClip (*ptr);
            bool any = false;
            std::vector<Note> kept;
            kept.reserve (c.notes.size());
            for (const auto& n : c.notes)
            {
                const Tick at = c.start + n.start;
                if (at < from || at >= to)
                {
                    kept.push_back (n);
                    continue;
                }
                Note songNote = n;
                songNote.start = at;
                const Note was = songNote;
                if (! change (songNote))
                {
                    ++changed;
                    any = true;
                    continue;
                }
                songNote.start = was.start; // only pitch, velocity and length change
                songNote.length = std::clamp<Tick> (songNote.length, 1, c.end() - at);
                if (! (songNote == was))
                {
                    ++changed;
                    any = true;
                }
                songNote.start -= c.start;
                kept.push_back (songNote);
            }
            if (! any)
                continue;
            c.notes = std::move (kept);
            edit::normalise (c);
            ptr = std::make_shared<const Clip> (std::move (c));
        }
        return changed;
    }

    int addNotes (Track& t, const std::vector<Note>& songNotes)
    {
        int added = 0;
        for (auto& ptr : t.clips)
        {
            if (ptr == nullptr)
                continue;
            std::vector<Note> mine;
            for (const auto& n : songNotes)
                if (n.start >= ptr->start && n.start < ptr->end())
                    mine.push_back (n);
            if (mine.empty())
                continue;
            Clip c = edit::playedClip (*ptr);
            for (auto n : mine)
            {
                n.start -= c.start;
                n.length = std::clamp<Tick> (n.length, 1, c.length - n.start);
                const bool there = std::any_of (c.notes.begin(), c.notes.end(), [&] (const Note& o) { return o.start == n.start && o.pitch == n.pitch; });
                if (there)
                    continue;
                c.notes.push_back (n);
                ++added;
            }
            edit::normalise (c);
            ptr = std::make_shared<const Clip> (std::move (c));
        }
        return added;
    }

    Proposal makeProposal (const Song& s, std::string kind, std::string id, std::string title, std::string because,
                           std::vector<TrackId> tracks, Tick from, Tick to)
    {
        Proposal p;
        p.kind = std::move (kind);
        p.id = std::move (id);
        p.title = std::move (title);
        p.because = std::move (because);
        p.tracks = std::move (tracks);
        p.from = from;
        p.to = to;
        for (auto id2 : p.tracks)
            if (const auto* t = edit::findTrack (s, id2))
                p.before.push_back (*t);
        return p;
    }

    void addOption (Proposal& p, std::string label, std::string summary, std::vector<Track> tracks, int changed)
    {
        if (changed <= 0 || tracks.empty())
            return;
        p.options.push_back ({ std::move (label), std::move (summary), std::move (tracks), changed });
    }

    std::string count (int n, const char* one, const char* many)
    {
        return std::to_string (n) + " " + (n == 1 ? one : many);
    }
}

using namespace detail;

// ---- what the tracks play --------------------------------------------------------
std::vector<PlayedNote> playedNotes (const Track& t, Tick from, Tick to)
{
    std::vector<PlayedNote> out;
    if (t.output.kind == TrackOutput::Kind::audio)
        return out;
    for (const auto& ptr : t.clips)
    {
        if (ptr == nullptr || ptr->end() <= from || ptr->start >= to)
            continue;
        const bool looping = ptr->loopLength > 0 && ptr->loopLength < ptr->length;
        const Clip& c = *ptr;
        const auto add = [&] (const Note& n, Tick base, Tick cycleEnd) {
            const Tick on = c.start + base + n.start;
            if (on < from || on >= to)
                return;
            const Tick off = c.start + std::min (base + n.end(), cycleEnd);
            out.push_back ({ on, std::max (on + 1, off), n.pitch, n.velocity });
        };
        if (! looping)
        {
            for (const auto& n : c.notes)
                add (n, 0, c.length);
            continue;
        }
        for (Tick base = 0; base < c.length; base += c.loopLength)
        {
            const Tick cycleEnd = std::min (base + c.loopLength, c.length);
            for (const auto& n : c.notes)
                if (n.start < c.loopLength && base + n.start < c.length)
                    add (n, base, cycleEnd);
        }
    }
    std::sort (out.begin(), out.end(), [] (const PlayedNote& a, const PlayedNote& b) { return a.on != b.on ? a.on < b.on : a.pitch < b.pitch; });
    return out;
}

const char* roleName (Role r)
{
    switch (r)
    {
        case Role::drums: return "drums";
        case Role::bass: return "bass";
        case Role::melody: return "melody";
        case Role::chords: return "chords";
        case Role::other: break;
    }
    return "other";
}

bool isDrumTrack (const Track& t, const DeviceLibrary* devices)
{
    switch (t.output.kind)
    {
        case TrackOutput::Kind::sampler: return true;
        case TrackOutput::Kind::soundFont: return t.output.channel == 10;
        case TrackOutput::Kind::midiPort:
        {
            if (devices == nullptr)
                return t.output.channel == 10;
            const auto& ch = devices->forPort (t.output.port).drumChannels;
            return std::find (ch.begin(), ch.end(), t.output.channel) != ch.end();
        }
        default: break;
    }
    return false;
}

namespace
{
    struct TrackStat
    {
        TrackId id = 0;
        double meanPitch = 0.0;
        double chordShare = 0.0; ///< notes that start while another note of the track sounds
        std::size_t notes = 0;
    };

    TrackStat statOf (const Track& t)
    {
        TrackStat st;
        st.id = t.id;
        const auto notes = playedNotes (t);
        st.notes = notes.size();
        if (notes.empty())
            return st;
        double sum = 0.0;
        Tick sounding = std::numeric_limits<Tick>::min();
        int stacked = 0;
        for (const auto& n : notes)
        {
            sum += n.pitch;
            if (n.on + 10 < sounding) // not "sounding - 10": sounding starts at the lowest Tick, and that overflows
                ++stacked;
            sounding = std::max (sounding, n.off);
        }
        st.meanPitch = sum / static_cast<double> (notes.size());
        st.chordShare = static_cast<double> (stacked) / static_cast<double> (notes.size());
        return st;
    }
}

std::map<TrackId, Role> guessRoles (const Song& s, const DeviceLibrary* devices, TrackId melody)
{
    std::map<TrackId, Role> roles;
    std::vector<TrackStat> mono;
    for (const auto& t : s.tracks)
    {
        const auto st = statOf (t);
        if (st.notes == 0)
            continue;
        if (isDrumTrack (t, devices) || nameHas (t.name, { "drum", "kit", "perc" }))
            roles[t.id] = Role::drums;
        else if (nameHas (t.name, { "bass" }))
            roles[t.id] = Role::bass;
        else if (nameHas (t.name, { "pad", "string", "chord", "piano", "keys", "organ", "guitar", "rhodes", "comp", "choir" }) && st.chordShare >= 0.15)
            roles[t.id] = Role::chords;
        else if (st.chordShare >= 0.3)
            roles[t.id] = Role::chords;
        else if (st.meanPitch < 52.0)
            roles[t.id] = Role::bass;
        else
        {
            roles[t.id] = Role::other;
            mono.push_back (st);
        }
    }

    TrackId chosen = 0;
    if (melody != 0 && roles.count (melody) != 0)
        chosen = melody;
    if (chosen == 0)
        for (const auto& st : mono)
            if (nameHas (detail::trackName (s, st.id), { "melody", "lead", "vocal", "vox", "voice", "solo", "sing", "tune" }))
                chosen = st.id;
    if (chosen == 0)
    {
        double best = 55.0;
        for (const auto& st : mono)
            if (st.meanPitch >= best && st.chordShare < 0.25)
            {
                best = st.meanPitch;
                chosen = st.id;
            }
    }
    if (chosen != 0)
        roles[chosen] = Role::melody;
    return roles;
}

std::string barsText (const Song& s, Tick from, Tick to)
{
    const Tick bar = barLength (s);
    const auto a = from / bar + 1;
    const auto b = std::max<Tick> (from + 1, to - 1) / bar + 1;
    return a == b ? "bar " + std::to_string (a) : "bars " + std::to_string (a) + "-" + std::to_string (b);
}

// ---- proposals -------------------------------------------------------------------
bool isCurrent (const Song& s, const Proposal& p)
{
    for (const auto& was : p.before)
    {
        const auto* now = edit::findTrack (s, was.id);
        if (now == nullptr || ! (*now == was))
            return false;
    }
    return true;
}

std::string problemWith (const Song& s, const Option& o)
{
    if (o.tracks.empty())
        return "the option changes nothing";
    for (const auto& t : o.tracks)
    {
        if (edit::findTrack (s, t.id) == nullptr)
            return "the track " + t.name + " is not in the song any more";
        if (t.output.channel < 1 || t.output.channel > 16)
            return "a MIDI channel must be 1-16";
        for (const auto& c : t.clips)
        {
            if (c == nullptr)
                continue;
            for (const auto& n : c->notes)
                if (n.pitch > 127 || n.velocity < 1 || n.velocity > 127 || n.length <= 0 || n.start < 0 || n.start >= c->length)
                    return "a note in " + t.name + " is out of range";
        }
    }
    return {};
}

Song withOption (const Song& s, const Option& o)
{
    Song out = s;
    for (const auto& t : o.tracks)
        if (auto* there = edit::findTrack (out, t.id))
            *there = t;
    return out;
}

void apply (Song& s, const Proposal& p, int option)
{
    if (option < 0 || option >= static_cast<int> (p.options.size()))
        throw std::runtime_error ("the proposal has no option " + std::to_string (option + 1));
    if (! isCurrent (s, p))
        throw std::runtime_error ("the tracks changed since the proposal was made. Check the arrangement again.");
    const auto& o = p.options[static_cast<std::size_t> (option)];
    if (const auto why = problemWith (s, o); ! why.empty())
        throw std::runtime_error (why);
    s = withOption (s, o);
}

void Preferences::took (const Proposal& p, int option)
{
    ++counts[p.kind].taken;
    ++counts[p.kind + "/" + std::to_string (option)].taken;
}

void Preferences::dismissed (const Proposal& p)
{
    ++counts[p.kind].dismissed;
}

double Preferences::weight (const std::string& kind) const
{
    const auto it = counts.find (kind);
    if (it == counts.end())
        return 1.0;
    const double w = (1.0 + it->second.taken) / (1.0 + it->second.dismissed);
    return std::clamp (w, 0.25, 4.0);
}

int Preferences::favouriteOption (const std::string& kind, int options) const
{
    int best = -1, most = 0;
    for (int i = 0; i < options; ++i)
        if (const auto it = counts.find (kind + "/" + std::to_string (i)); it != counts.end() && it->second.taken > most)
        {
            most = it->second.taken;
            best = i;
        }
    return best;
}

std::string Preferences::toJson() const
{
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [k, c] : counts)
        j[k] = { c.taken, c.dismissed };
    return j.dump();
}

Preferences Preferences::fromJson (const std::string& text)
{
    Preferences p;
    try
    {
        const auto j = nlohmann::json::parse (text);
        if (j.is_object())
            for (const auto& [k, v] : j.items())
                if (v.is_array() && v.size() == 2 && v[0].is_number_integer() && v[1].is_number_integer())
                    p.counts[k] = { std::max (0, v[0].get<int>()), std::max (0, v[1].get<int>()) };
    }
    catch (const nlohmann::json::exception&)
    {
    }
    return p;
}

void rank (std::vector<Proposal>& list, const Preferences* prefs)
{
    for (auto& p : list)
    {
        if (prefs == nullptr)
            continue;
        p.weight *= prefs->weight (p.kind);
        // The option the user takes most often comes first.
        const int fav = prefs->favouriteOption (p.kind, static_cast<int> (p.options.size()));
        if (fav > 0)
            std::rotate (p.options.begin(), p.options.begin() + fav, p.options.begin() + fav + 1);
    }
    std::stable_sort (list.begin(), list.end(), [] (const Proposal& a, const Proposal& b) { return a.weight > b.weight; });
}

// ---- the arrangement doctor ----------------------------------------------------
const char* goalId (Goal g)
{
    switch (g)
    {
        case Goal::standOut: return "stand_out";
        case Goal::clearMelody: return "clear_melody";
        case Goal::lessClutter: return "less_clutter";
        case Goal::moreVariation: return "more_variation";
    }
    return "less_clutter";
}

std::string goalName (Goal g)
{
    switch (g)
    {
        case Goal::standOut: return "Make a section stand out";
        case Goal::clearMelody: return "Give the melody room";
        case Goal::lessClutter: return "Make busy bars clearer";
        case Goal::moreVariation: return "Repeat less";
    }
    return {};
}

std::optional<Goal> parseGoal (std::string_view text)
{
    for (auto g : { Goal::standOut, Goal::clearMelody, Goal::lessClutter, Goal::moreVariation })
        if (text == goalId (g))
            return g;
    return std::nullopt;
}

namespace
{
    struct Context
    {
        const Song& song;
        const DoctorRequest& request;
        std::map<TrackId, Role> roles;
        TrackId melody = 0;
        Tick from = 0, to = 0, bar = 1, beat = 1;
        std::map<TrackId, std::vector<PlayedNote>> notes; ///< the whole song, per track with notes

        Role role (TrackId id) const
        {
            const auto it = roles.find (id);
            return it != roles.end() ? it->second : Role::other;
        }
        bool pitched (TrackId id) const { return roles.count (id) != 0 && role (id) != Role::drums; }
        /** Tracks the doctor may change in a lighter way: not the melody. */
        bool editable (TrackId id) const { return roles.count (id) != 0 && id != melody; }
        const Track& track (TrackId id) const { return *edit::findTrack (song, id); }

        int notesIn (TrackId id, Tick a, Tick b) const
        {
            const auto it = notes.find (id);
            if (it == notes.end())
                return 0;
            return static_cast<int> (std::count_if (it->second.begin(), it->second.end(), [&] (const PlayedNote& n) { return n.on >= a && n.on < b; }));
        }
        std::vector<PlayedNote> melodyNotes() const
        {
            const auto it = notes.find (melody);
            return it != notes.end() ? it->second : std::vector<PlayedNote> {};
        }
        int barCount (Tick a, Tick b) const { return static_cast<int> (std::max<Tick> (1, (b - a + bar - 1) / bar)); }
    };

    double median (std::vector<double> v)
    {
        if (v.empty())
            return 0.0;
        std::sort (v.begin(), v.end());
        return v[v.size() / 2];
    }

    std::string one (double v) { return std::to_string (static_cast<long long> (std::llround (v))); }

    std::string idOf (const char* kind, TrackId track, const Song& s, Tick from, Tick to)
    {
        const Tick bar = barLength (s);
        return std::string (kind) + ":" + std::to_string (track) + ":" + std::to_string (from / bar + 1) + "-" + std::to_string ((to - 1) / bar + 1);
    }

    /** The track (of `allowed`) with the most notes in [a, b). */
    TrackId busiest (const Context& cx, Tick a, Tick b, const std::function<bool (TrackId)>& allowed)
    {
        TrackId best = 0;
        int most = 0;
        for (const auto& [id, _] : cx.notes)
            if (allowed (id))
                if (const int n = cx.notesIn (id, a, b); n > most)
                {
                    most = n;
                    best = id;
                }
        return best;
    }

    // ---- edits ----
    bool onBeat (const Context& cx, Tick at) { return at % cx.beat < cx.beat / 8; }

    void optionOnBeats (const Context& cx, Proposal& p, TrackId id, Tick a, Tick b, const char* label = "Play on the beats only")
    {
        Track t = cx.track (id);
        const int all = cx.notesIn (id, a, b);
        const int n = editNotes (t, a, b, [&] (Note& note) { return onBeat (cx, note.start); });
        if (n > 0 && n < all)
            addOption (p, label, "Removes " + std::to_string (n) + " of " + std::to_string (all) + " notes in " + t.name + " that start between the beats.", { std::move (t) }, n);
    }

    /** At each chord (notes that start together), keeps the lowest and the highest note. */
    void optionOuterNotes (const Context& cx, Proposal& p, TrackId id, Tick a, Tick b)
    {
        Track t = cx.track (id);
        const auto played = playedNotes (t, a, b);
        std::map<Tick, std::pair<int, int>> outer; // onset -> lowest, highest
        for (const auto& n : played)
        {
            auto [it, isNew] = outer.try_emplace (n.on, n.pitch, n.pitch);
            if (! isNew)
                it->second = { std::min (it->second.first, n.pitch), std::max (it->second.second, n.pitch) };
        }
        const int n = editNotes (t, a, b, [&] (Note& note) {
            const auto it = outer.find (note.start);
            return it == outer.end() || note.pitch == it->second.first || note.pitch == it->second.second;
        });
        addOption (p, "Keep only the outer notes of each chord", "Removes " + count (n, "inner chord note", "inner chord notes") + " in " + t.name + "; the top and bottom notes stay.", { std::move (t) }, n);
    }

    bool isHat (int pitch) { return pitch == 42 || pitch == 44 || pitch == 46; }

    void optionNoHats (const Context& cx, Proposal& p, TrackId id, Tick a, Tick b, const std::string& where)
    {
        Track t = cx.track (id);
        const int n = editNotes (t, a, b, [] (Note& note) { return ! isHat (note.pitch); });
        addOption (p, "Leave out the hi-hats", "Removes " + count (n, "hi-hat", "hi-hats") + " in " + t.name + " " + where + ".", { std::move (t) }, n);
    }

    /** Removes notes that another track already plays (the same note name) at the same moment. */
    void optionNoDoubles (const Context& cx, Proposal& p, TrackId id, Tick a, Tick b)
    {
        Track t = cx.track (id);
        std::vector<PlayedNote> others;
        for (const auto& [other, list] : cx.notes)
            if (other != id && cx.pitched (other))
                for (const auto& n : list)
                    if (n.off > a && n.on < b)
                        others.push_back (n);
        const int n = editNotes (t, a, b, [&] (Note& note) {
            for (const auto& o : others)
                if (o.on <= note.start && note.start < o.off && o.pitch % 12 == note.pitch % 12)
                    return false;
            return true;
        });
        addOption (p, "Remove notes other tracks double", "Removes " + count (n, "note", "notes") + " in " + t.name + " that another track already plays at the same time.", { std::move (t) }, n);
    }

    // ---- the checks ----
    void checkDensity (Context& cx, std::vector<Finding>& out)
    {
        std::vector<double> perBar;
        std::vector<int> counts;
        for (Tick b = cx.from; b < cx.to; b += cx.bar)
        {
            int n = 0;
            for (const auto& [id, _] : cx.notes)
                if (cx.pitched (id) && id != cx.melody)
                    n += cx.notesIn (id, b, b + cx.bar);
            counts.push_back (n);
            if (n > 0)
                perBar.push_back (n);
        }
        const double typical = median (perBar);
        if (typical <= 0.0)
            return;
        const double limit = std::max ({ typical * 1.5, typical + 6.0, 12.0 });
        for (std::size_t i = 0; i < counts.size();)
        {
            if (counts[i] <= limit)
            {
                ++i;
                continue;
            }
            std::size_t j = i;
            int total = 0;
            while (j < counts.size() && counts[j] > limit)
                total += counts[j++];
            const Tick a = cx.from + static_cast<Tick> (i) * cx.bar, b = cx.from + static_cast<Tick> (j) * cx.bar;
            const double mean = static_cast<double> (total) / static_cast<double> (j - i);
            const auto top = busiest (cx, a, b, [&] (TrackId id) { return cx.pitched (id) && id != cx.melody; });
            Finding f;
            f.kind = "density";
            f.from = a;
            f.to = b;
            f.amount = mean / typical;
            f.tracks = { top };
            f.text = "The accompaniment is busy in " + barsText (cx.song, a, b) + ": " + one (mean) + " notes a bar, against " + one (typical) + " in a typical bar. "
                     + trackName (cx.song, top) + " plays " + std::to_string (cx.notesIn (top, a, b)) + " of them.";
            out.push_back (f);
            i = j;
        }
    }

    /** Notes of another track that sound with the melody, inside the melody's range in that bar. */
    std::vector<PlayedNote> clashes (const Context& cx, TrackId id, Tick a, Tick b)
    {
        std::vector<PlayedNote> out;
        const auto mel = cx.melodyNotes();
        const auto it = cx.notes.find (id);
        if (it == cx.notes.end())
            return out;
        for (Tick bar = a; bar < b; bar += cx.bar)
        {
            int lo = 128, hi = -1;
            for (const auto& m : mel)
                if (m.on < bar + cx.bar && m.off > bar)
                {
                    lo = std::min (lo, m.pitch);
                    hi = std::max (hi, m.pitch);
                }
            if (hi < 0)
                continue;
            for (const auto& n : it->second)
            {
                if (n.on < bar || n.on >= bar + cx.bar || n.pitch < lo - 1 || n.pitch > hi + 1)
                    continue;
                const bool together = std::any_of (mel.begin(), mel.end(), [&] (const PlayedNote& m) { return n.on < m.off && m.on < n.off; });
                if (together)
                    out.push_back (n);
            }
        }
        return out;
    }

    void checkOverlap (Context& cx, std::vector<Finding>& out)
    {
        if (cx.melody == 0)
            return;
        for (const auto& [id, _] : cx.notes)
        {
            if (! cx.pitched (id) || id == cx.melody)
                continue;
            // Bars where the track has 2 or more clashes, joined into spans.
            std::vector<int> perBar;
            for (Tick b = cx.from; b < cx.to; b += cx.bar)
                perBar.push_back (static_cast<int> (clashes (cx, id, b, b + cx.bar).size()));
            for (std::size_t i = 0; i < perBar.size();)
            {
                if (perBar[i] < 2)
                {
                    ++i;
                    continue;
                }
                std::size_t j = i;
                int total = 0;
                while (j < perBar.size() && perBar[j] >= 2)
                    total += perBar[j++];
                const Tick a = cx.from + static_cast<Tick> (i) * cx.bar, b = cx.from + static_cast<Tick> (j) * cx.bar;
                int lo = 128, hi = -1;
                for (const auto& m : cx.melodyNotes())
                    if (m.on < b && m.off > a)
                    {
                        lo = std::min (lo, m.pitch);
                        hi = std::max (hi, m.pitch);
                    }
                Finding f;
                f.kind = "overlap";
                f.from = a;
                f.to = b;
                f.tracks = { id };
                f.amount = static_cast<double> (total);
                f.text = trackName (cx.song, id) + " plays inside the melody's range (" + pitchText (lo) + "-" + pitchText (hi) + ") in " + barsText (cx.song, a, b) + ": "
                         + count (total, "note sounds", "notes sound") + " together with the melody.";
                out.push_back (f);
                i = j;
            }
        }
    }

    std::string barPrint (const std::vector<PlayedNote>& notes, Tick a, Tick b)
    {
        std::string s;
        for (const auto& n : notes)
            if (n.on >= a && n.on < b)
                s += std::to_string (n.on - a) + ":" + std::to_string (n.pitch) + ",";
        return s;
    }

    void checkRepetition (Context& cx, std::vector<Finding>& out)
    {
        for (const auto& [id, list] : cx.notes)
        {
            if (id == cx.melody)
                continue;
            std::vector<std::string> prints;
            for (Tick b = cx.from; b < cx.to; b += cx.bar)
                prints.push_back (barPrint (list, b, b + cx.bar));
            const auto repeats = [&] (std::size_t k) {
                return ! prints[k].empty() && ((k >= 1 && prints[k] == prints[k - 1]) || (k >= 2 && prints[k] == prints[k - 2]));
            };
            for (std::size_t i = 0; i < prints.size();)
            {
                if (prints[i].empty())
                {
                    ++i;
                    continue;
                }
                std::size_t j = i + 1;
                while (j < prints.size() && repeats (j))
                    ++j;
                if (j - i >= 8)
                {
                    const Tick a = cx.from + static_cast<Tick> (i) * cx.bar, b = cx.from + static_cast<Tick> (j) * cx.bar;
                    Finding f;
                    f.kind = "repetition";
                    f.from = a;
                    f.to = b;
                    f.tracks = { id };
                    f.amount = static_cast<double> (j - i) / 8.0;
                    f.text = std::string ("The same ") + (prints[i + 1] == prints[i] ? "bar repeats" : "two bars repeat") + " in " + trackName (cx.song, id) + " for "
                             + std::to_string (j - i) + " bars (" + barsText (cx.song, a, b) + ").";
                    out.push_back (f);
                }
                i = j;
            }
        }
    }

    struct SectionNumbers
    {
        double notesPerBar = 0.0, velocity = 0.0, pitch = 0.0;
        int tracks = 0, lowest = 128, highest = -1;
    };

    SectionNumbers numbersOf (const Context& cx, Tick a, Tick b)
    {
        SectionNumbers n;
        double vel = 0.0, pitch = 0.0;
        int all = 0, pitchedNotes = 0;
        for (const auto& [id, list] : cx.notes)
        {
            if (id == cx.melody)
                continue;
            bool plays = false;
            for (const auto& note : list)
            {
                if (note.on < a || note.on >= b)
                    continue;
                plays = true;
                ++all;
                vel += note.velocity;
                if (cx.pitched (id))
                {
                    ++pitchedNotes;
                    pitch += note.pitch;
                    n.lowest = std::min (n.lowest, note.pitch);
                    n.highest = std::max (n.highest, note.pitch);
                }
            }
            n.tracks += plays ? 1 : 0;
        }
        n.notesPerBar = static_cast<double> (all) / cx.barCount (a, b);
        n.velocity = all > 0 ? vel / all : 0.0;
        n.pitch = pitchedNotes > 0 ? pitch / pitchedNotes : 0.0;
        return n;
    }

    void checkContrast (Context& cx, std::vector<Finding>& out, SectionNumbers& target, SectionNumbers& before, Tick& beforeFrom)
    {
        if (cx.request.goal != Goal::standOut)
            return;
        const Tick len = cx.to - cx.from;
        beforeFrom = std::max<Tick> (0, cx.from - len);
        if (beforeFrom >= cx.from)
            return;
        target = numbersOf (cx, cx.from, cx.to);
        before = numbersOf (cx, beforeFrom, cx.from);
        const double ratio = before.notesPerBar > 0.0 ? target.notesPerBar / before.notesPerBar : 2.0;
        const double velGap = target.velocity - before.velocity;
        const double score = std::abs (ratio - 1.0) + std::abs (velGap) / 30.0 + std::abs (target.tracks - before.tracks) * 0.3;
        Finding f;
        f.kind = "contrast";
        f.from = cx.from;
        f.to = cx.to;
        f.amount = 1.0 / std::max (0.05, score);
        auto section = barsText (cx.song, cx.from, cx.to);
        section[0] = static_cast<char> (std::toupper (static_cast<unsigned char> (section[0])));
        f.text = section + " against " + barsText (cx.song, beforeFrom, cx.from) + " before it: " + one (target.notesPerBar) + " against "
                 + one (before.notesPerBar) + " accompaniment notes a bar, " + std::to_string (target.tracks) + " against " + std::to_string (before.tracks)
                 + " tracks, loudness (velocity) " + one (target.velocity) + " against " + one (before.velocity) + ".";
        f.text += score < 0.35 ? " The two sections sound much alike." : " The section already differs.";
        out.push_back (f);
    }

    // ---- proposals for each goal ----
    void proposeLessClutter (Context& cx, const std::vector<Finding>& findings, std::vector<Proposal>& out)
    {
        for (const auto& f : findings)
        {
            if (f.kind != "density" || f.tracks.empty() || f.tracks.front() == 0)
                continue;
            const auto id = f.tracks.front();
            const auto& name = trackName (cx.song, id);
            auto p = makeProposal (cx.song, "density", idOf ("density", id, cx.song, f.from, f.to), "Thin " + name + " in " + barsText (cx.song, f.from, f.to), f.text,
                                   { id }, f.from, f.to);
            optionOnBeats (cx, p, id, f.from, f.to);
            optionNoDoubles (cx, p, id, f.from, f.to);
            if (p.options.size() < 2 && cx.role (id) == Role::chords)
                optionOuterNotes (cx, p, id, f.from, f.to);
            p.weight = f.amount;
            if (! p.options.empty())
                out.push_back (std::move (p));
        }
    }

    void proposeClearMelody (Context& cx, const std::vector<Finding>& findings, std::vector<Proposal>& out)
    {
        for (const auto& f : findings)
        {
            if (f.kind != "overlap")
                continue;
            const auto id = f.tracks.front();
            const auto& name = trackName (cx.song, id);
            const auto hits = clashes (cx, id, f.from, f.to);
            const auto isHit = [&] (const Note& n) {
                return std::any_of (hits.begin(), hits.end(), [&] (const PlayedNote& h) { return h.on == n.start && h.pitch == n.pitch; });
            };
            auto p = makeProposal (cx.song, "overlap", idOf ("overlap", id, cx.song, f.from, f.to), "Give the melody room from " + name + " in " + barsText (cx.song, f.from, f.to),
                                   f.text + " The melody stays as it is.", { id }, f.from, f.to);
            {
                Track t = cx.track (id);
                const int n = editNotes (t, f.from, f.to, [&] (Note& note) {
                    if (isHit (note) && note.pitch >= 48)
                        note.pitch = static_cast<std::uint8_t> (note.pitch - 12);
                    return true;
                });
                addOption (p, "Move them an octave down", "Moves " + count (n, "note", "notes") + " of " + name + " an octave below the melody.", { std::move (t) }, n);
            }
            {
                const auto mel = cx.melodyNotes();
                Track t = cx.track (id);
                const int n = editNotes (t, f.from, f.to, [&] (Note& note) {
                    if (! isHit (note))
                        return true;
                    // Keep the note when it starts in a gap of the melody.
                    return std::none_of (mel.begin(), mel.end(), [&] (const PlayedNote& m) { return m.on <= note.start && note.start < m.off; });
                });
                addOption (p, "Play only in the melody's gaps", "Removes " + count (n, "note", "notes") + " of " + name + " that start while the melody sounds; notes in its rests stay.", { std::move (t) }, n);
            }
            p.weight = f.amount / 4.0;
            if (! p.options.empty())
                out.push_back (std::move (p));
        }
    }

    void proposeMoreVariation (Context& cx, const std::vector<Finding>& findings, std::vector<Proposal>& out)
    {
        for (const auto& f : findings)
        {
            if (f.kind != "repetition")
                continue;
            const auto id = f.tracks.front();
            const auto& name = trackName (cx.song, id);
            const auto role = cx.role (id);
            auto p = makeProposal (cx.song, "repetition", idOf ("repetition", id, cx.song, f.from, f.to), "Vary " + name + " in " + barsText (cx.song, f.from, f.to), f.text,
                                   { id }, f.from, f.to);
            const Tick phrase = cx.bar * 4;
            {
                // A breath before each new phrase: the second half of every 4th bar is left empty.
                Track t = cx.track (id);
                const int n = editNotes (t, f.from, f.to, [&] (Note& note) {
                    const Tick inPhrase = (note.start - f.from) % phrase;
                    return ! (inPhrase >= phrase - cx.bar / 2 && ! (role == Role::drums && note.pitch == 36 && onBeat (cx, note.start)));
                });
                addOption (p, "Leave space at each phrase end", "Leaves the second half of every 4th bar empty in " + name + " (" + count (n, "note", "notes") + " fewer).", { std::move (t) }, n);
            }
            // The second option changes every other 4 bars.
            const auto otherPhrase = [&] (Tick at) { return ((at - f.from) / phrase) % 2 == 1; };
            if (role == Role::drums)
            {
                Track t = cx.track (id);
                const int n = editNotes (t, f.from, f.to, [&] (Note& note) { return ! (otherPhrase (note.start) && isHat (note.pitch)); });
                addOption (p, "Leave out the hi-hats in every other 4 bars", "Removes " + count (n, "hi-hat", "hi-hats") + " in " + name + ".", { std::move (t) }, n);
            }
            else if (role == Role::bass)
            {
                Track t = cx.track (id);
                const int n = editNotes (t, f.from, f.to, [&] (Note& note) { return ! otherPhrase (note.start) || onBeat (cx, note.start); });
                addOption (p, "Play on the beats in every other 4 bars", "Removes " + count (n, "note", "notes") + " of " + name + " between the beats.", { std::move (t) }, n);
            }
            else
            {
                const auto mel = cx.melodyNotes();
                Track t = cx.track (id);
                const int n = editNotes (t, f.from, f.to, [&] (Note& note) {
                    if (! otherPhrase (note.start) || note.pitch + 12 > 108)
                        return true;
                    const int up = note.pitch + 12;
                    const bool hitsMelody = std::any_of (mel.begin(), mel.end(), [&] (const PlayedNote& m) {
                        return m.on < note.end() && note.start < m.off && std::abs (m.pitch - up) <= 2;
                    });
                    if (! hitsMelody)
                        note.pitch = static_cast<std::uint8_t> (up);
                    return true;
                });
                addOption (p, "Play every other 4 bars an octave up", "Moves " + count (n, "note", "notes") + " of " + name + " an octave up (not where they would hit the melody).", { std::move (t) }, n);
            }
            p.weight = f.amount;
            if (! p.options.empty())
                out.push_back (std::move (p));
        }
    }

    void proposeStandOut (Context& cx, const SectionNumbers& target, const SectionNumbers& before, Tick beforeFrom, std::vector<Proposal>& out)
    {
        const auto section = barsText (cx.song, cx.from, cx.to);
        // 1. Less before the section, so the section sounds bigger.
        if (beforeFrom < cx.from && target.notesPerBar < before.notesPerBar * 1.3 + 1.0)
        {
            const auto lighter = [&] (TrackId id) { return cx.editable (id) && (cx.role (id) == Role::chords || cx.role (id) == Role::other); };
            TrackId id = busiest (cx, beforeFrom, cx.from, lighter);
            if (id == 0)
                id = busiest (cx, beforeFrom, cx.from, [&] (TrackId t) { return cx.editable (t) && cx.role (t) == Role::drums; });
            if (id != 0)
            {
                const auto& name = trackName (cx.song, id);
                const auto where = barsText (cx.song, beforeFrom, cx.from);
                auto p = makeProposal (cx.song, "contrast", idOf ("contrast", id, cx.song, beforeFrom, cx.from), "Play " + name + " lighter in " + where,
                                       "The accompaniment plays " + one (before.notesPerBar) + " notes a bar in " + where + " and " + one (target.notesPerBar) + " in " + section
                                           + ". A lighter " + name + " before it makes " + section + " sound bigger.",
                                       { id }, beforeFrom, cx.from);
                if (cx.role (id) == Role::drums)
                {
                    optionNoHats (cx, p, id, beforeFrom, cx.from, "in " + where);
                    optionOnBeats (cx, p, id, beforeFrom, cx.from, "Play on the beats only");
                }
                else
                {
                    optionOnBeats (cx, p, id, beforeFrom, cx.from);
                    optionOuterNotes (cx, p, id, beforeFrom, cx.from);
                }
                p.weight = 1.5;
                if (! p.options.empty())
                    out.push_back (std::move (p));
            }
        }

        // 2. More energy in the section: the accompaniment plays harder.
        if (target.velocity - before.velocity < 15.0)
        {
            std::vector<TrackId> ids;
            for (const auto& [id, _] : cx.notes)
                if (cx.editable (id) && cx.notesIn (id, cx.from, cx.to) > 0)
                    ids.push_back (id);
            if (! ids.empty())
            {
                auto p = makeProposal (cx.song, "energy", idOf ("energy", 0, cx.song, cx.from, cx.to), "Play the accompaniment harder in " + section,
                                       "The accompaniment's loudness (velocity) is " + one (target.velocity) + " in " + section + " and " + one (before.velocity)
                                           + " before it. The melody stays as it is.",
                                       ids, cx.from, cx.to);
                for (int add : { 10, 20 })
                {
                    std::vector<Track> tracks;
                    int n = 0;
                    for (auto id : ids)
                    {
                        Track t = cx.track (id);
                        n += editNotes (t, cx.from, cx.to, [&] (Note& note) {
                            note.velocity = static_cast<std::uint8_t> (std::min (127, note.velocity + add));
                            return true;
                        });
                        tracks.push_back (std::move (t));
                    }
                    addOption (p, add == 10 ? "A little harder (+10)" : "Much harder (+20)", "Raises the velocity of " + count (n, "note", "notes") + " in " + count (static_cast<int> (ids.size()), "track", "tracks") + ".", std::move (tracks), n);
                }
                p.weight = 1.2;
                if (! p.options.empty())
                    out.push_back (std::move (p));
            }
        }

        // 3. A wider sound: double the chord part's outer notes.
        const auto chordTrack = busiest (cx, cx.from, cx.to, [&] (TrackId id) { return cx.editable (id) && cx.role (id) == Role::chords; });
        if (chordTrack != 0 && (target.highest - target.lowest) < (before.highest - before.lowest) + 7)
        {
            const auto& name = trackName (cx.song, chordTrack);
            auto p = makeProposal (cx.song, "register", idOf ("register", chordTrack, cx.song, cx.from, cx.to), "Make " + name + " wider in " + section,
                                   "The accompaniment covers " + pitchText (std::max (0, target.lowest)) + "-" + pitchText (std::max (0, target.highest)) + " in " + section
                                       + ", about the same range as before it. Octave doublings make the section sound fuller.",
                                   { chordTrack }, cx.from, cx.to);
            const auto played = playedNotes (cx.track (chordTrack), cx.from, cx.to);
            const auto mel = cx.melodyNotes();
            std::map<Tick, std::pair<PlayedNote, PlayedNote>> outer;
            for (const auto& n : played)
            {
                auto [it, isNew] = outer.try_emplace (n.on, n, n);
                if (! isNew)
                {
                    if (n.pitch < it->second.first.pitch)
                        it->second.first = n;
                    if (n.pitch > it->second.second.pitch)
                        it->second.second = n;
                }
            }
            for (int pass = 0; pass < 2; ++pass)
            {
                std::vector<Note> extra;
                for (const auto& [on, pair] : outer)
                {
                    const auto& src = pass == 0 ? pair.first : pair.second;
                    const int pitch = src.pitch + (pass == 0 ? -12 : 12);
                    if (pitch < 36 || pitch > 108)
                        continue;
                    const bool hitsMelody = std::any_of (mel.begin(), mel.end(), [&] (const PlayedNote& m) {
                        return m.on < src.off && src.on < m.off && std::abs (m.pitch - pitch) <= 2;
                    });
                    if (! hitsMelody)
                        extra.push_back ({ src.on, src.off - src.on, static_cast<std::uint8_t> (pitch), static_cast<std::uint8_t> (std::max (1, src.velocity - 10)) });
                }
                Track t = cx.track (chordTrack);
                const int n = addNotes (t, extra);
                if (pass == 0)
                    addOption (p, "Double the lowest note an octave down", "Adds " + count (n, "note", "notes") + " to " + name + ".", { std::move (t) }, n);
                else
                    addOption (p, "Double the top note an octave up", "Adds " + count (n, "note", "notes") + " to " + name + " (not where they would hit the melody).", { std::move (t) }, n);
            }
            p.weight = 1.0;
            if (! p.options.empty())
                out.push_back (std::move (p));
        }
    }
}

DoctorReport diagnose (const Song& s, const DoctorRequest& r)
{
    Context cx { s, r, {}, 0, 0, 0, barLength (s), beatLength (s), {} };
    DoctorReport report;
    cx.roles = guessRoles (s, r.devices, r.melody);
    for (const auto& [id, role] : cx.roles)
        if (role == Role::melody)
            cx.melody = id;
    for (const auto& t : s.tracks)
        if (cx.roles.count (t.id) != 0)
            cx.notes[t.id] = playedNotes (t);

    // The bars to look at, whole bars.
    cx.from = std::max<Tick> (0, r.from / cx.bar * cx.bar);
    cx.to = r.to > 0 ? (r.to + cx.bar - 1) / cx.bar * cx.bar : songEnd (s);
    cx.to = std::max (cx.to, cx.from + cx.bar);
    if (r.goal == Goal::standOut && (r.to <= r.from))
        throw std::runtime_error ("choose the bars of the section to lift (for example the chorus)");

    // standOut looks at the section and the bars before it; the other goals at their range.
    SectionNumbers target, before;
    Tick beforeFrom = cx.from;
    checkContrast (cx, report.findings, target, before, beforeFrom);
    checkDensity (cx, report.findings);
    checkOverlap (cx, report.findings);
    checkRepetition (cx, report.findings);
    std::stable_sort (report.findings.begin(), report.findings.end(), [] (const Finding& a, const Finding& b) {
        if (a.kind == "contrast" || b.kind == "contrast")
            return a.kind == "contrast" && b.kind != "contrast";
        return a.amount > b.amount;
    });

    switch (r.goal)
    {
        case Goal::standOut: proposeStandOut (cx, target, before, beforeFrom, report.proposals); break;
        case Goal::clearMelody: proposeClearMelody (cx, report.findings, report.proposals); break;
        case Goal::lessClutter: proposeLessClutter (cx, report.findings, report.proposals); break;
        case Goal::moreVariation: proposeMoreVariation (cx, report.findings, report.proposals); break;
    }
    rank (report.proposals, r.preferences);
    if (report.proposals.size() > r.mostProposals)
        report.proposals.resize (r.mostProposals);
    report.roles = cx.roles;
    report.melody = cx.melody;
    return report;
}

} // namespace arranger::arranging

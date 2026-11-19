#include "arranger/core/SongForm.h"

#include "arranger/core/Arranging.h"
#include "arranger/core/SongEdit.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace arranger::song_form
{

namespace
{
    std::string lower (std::string s)
    {
        for (auto& c : s)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return s;
    }

    std::string trim (const std::string& s)
    {
        const auto a = s.find_first_not_of (" \t\r\n");
        if (a == std::string::npos)
            return {};
        const auto b = s.find_last_not_of (" \t\r\n");
        return s.substr (a, b - a + 1);
    }

    Tick barOf (const Song& s) { return std::max<Tick> (1, s.timeSignature.ticksPerBar()); }

    Tick roundToBar (const Song& s, Tick t)
    {
        const Tick bar = barOf (s);
        return (t + bar / 2) / bar * bar;
    }

    bool makesNotes (const Track& t) { return t.output.kind != TrackOutput::Kind::audio; }

    /** Notes of every MIDI track that start in [from, to). */
    int notesIn (const Song& s, Tick from, Tick to)
    {
        int n = 0;
        for (const auto& t : s.tracks)
            if (makesNotes (t))
                n += static_cast<int> (arranging::playedNotes (t, from, to).size());
        return n;
    }

    /** A copy of the points of a line in [from, to), moved by `shift`, with the values at both edges. */
    void copyLine (const AutomationLine& src, Tick from, Tick to, Tick shift, AutomationLine& out)
    {
        if (src.empty())
            return;
        const auto put = [&] (Tick at, float value) {
            if (! out.empty() && out.back().at >= at)
            {
                if (out.back().at == at)
                    out.back().value = value;
                return;
            }
            out.push_back ({ at, value });
        };
        put (from + shift, automation::valueAt (src, static_cast<double> (from), 100.0f));
        for (const auto& p : src)
            if (p.at > from && p.at < to)
                put (p.at + shift, p.value);
        put (to - 1 + shift, automation::valueAt (src, static_cast<double> (to - 1), 100.0f));
    }

    std::vector<PluginSlot*> slotsOf (Track& t)
    {
        std::vector<PluginSlot*> out { &t.instrument };
        for (auto& e : t.effects)
            out.push_back (&e);
        return out;
    }

    std::vector<const PluginSlot*> slotsOf (const Track& t)
    {
        std::vector<const PluginSlot*> out { &t.instrument };
        for (const auto& e : t.effects)
            out.push_back (&e);
        return out;
    }

    /** Removes the notes of a track that start in [from, to); notes that sound into `from` end there when `cut`. */
    void clearNotes (Song& s, TrackId track, Tick from, Tick to, bool cut)
    {
        auto* t = edit::findTrack (s, track);
        if (t == nullptr)
            return;
        std::vector<ClipId> ids;
        for (const auto& c : t->clips)
            if (c != nullptr && c->end() > (cut ? 0 : from) && c->start < to)
                ids.push_back (c->id);
        for (auto id : ids)
            edit::modifyClip (s, id, [&] (Clip& c) {
                c = edit::playedClip (c);
                std::erase_if (c.notes, [&] (const Note& n) { const Tick on = c.start + n.start; return on >= from && on < to; });
                if (cut)
                    for (auto& n : c.notes)
                    {
                        const Tick on = c.start + n.start;
                        if (on < from && on + n.length > from)
                            n.length = std::max<Tick> (1, from - on - 10);
                    }
            });
    }

    bool hasNoteAt (const Song& s, TrackId track, Tick at, int pitch)
    {
        const auto* t = edit::findTrack (s, track);
        if (t == nullptr)
            return false;
        for (const auto& n : arranging::playedNotes (*t, at - 20, at + 20))
            if (n.pitch == pitch)
                return true;
        return false;
    }

    void addNote (Song& s, TrackId track, Tick at, Tick length, int pitch, int velocity)
    {
        if (at < 0 || hasNoteAt (s, track, at, pitch))
            return;
        edit::addTimelineNote (s, track, at, length, std::clamp (pitch, 0, 127), std::clamp (velocity, 1, 127));
    }

    /** The track that plays the bass: the bass role, else the lowest melodic part. */
    TrackId bassTrack (const Song& s, const DeviceLibrary* devices)
    {
        const auto roles = arranging::guessRoles (s, devices);
        for (const auto& [id, role] : roles)
            if (role == arranging::Role::bass)
                return id;
        TrackId best = 0;
        double lowest = 1000.0;
        for (const auto& t : s.tracks)
        {
            if (! makesNotes (t) || arranging::isDrumTrack (t, devices))
                continue;
            const auto notes = arranging::playedNotes (t);
            if (notes.empty())
                continue;
            double sum = 0.0;
            for (const auto& n : notes)
                sum += n.pitch;
            if (sum / static_cast<double> (notes.size()) < lowest)
            {
                lowest = sum / static_cast<double> (notes.size());
                best = t.id;
            }
        }
        return best;
    }

    std::vector<TrackId> drumTracks (const Song& s, const DeviceLibrary* devices)
    {
        std::vector<TrackId> out;
        for (const auto& t : s.tracks)
            if (makesNotes (t) && arranging::isDrumTrack (t, devices) && ! arranging::playedNotes (t).empty())
                out.push_back (t.id);
        return out;
    }

    /** Puts a transition into a built song, at the end of the part that ends at `at`. */
    void applyTransition (Song& s, const Transition& tr, Tick at, Tick partStart, double before, double after, const DeviceLibrary* devices)
    {
        if (tr.kind == Transition::Kind::none)
            return;
        const Tick beat = s.timeSignature.ticksPerBeat();
        const int beats = tr.beats > 0 ? tr.beats : defaultBeats (tr.kind, s.timeSignature);
        const Tick from = std::max (partStart, at - beat * beats);
        if (from >= at)
            return;
        const auto drums = drumTracks (s, devices);
        // A softer transition into a quieter part.
        const double lift = after >= before * 0.75 ? 1.0 : 0.75;
        const auto crashOnNext = [&] (TrackId d) {
            const auto* t = edit::findTrack (s, d);
            if (t != nullptr && ! arranging::playedNotes (*t, at, at + beat).empty() && lift >= 1.0)
            {
                addNote (s, d, at, beat / 2, 49, 112);
                addNote (s, d, at, beat / 4, 36, 110);
            }
        };

        switch (tr.kind)
        {
            case Transition::Kind::none:
                break;
            case Transition::Kind::fill:
            {
                for (auto d : drums)
                {
                    clearNotes (s, d, from, at, false);
                    const Tick step = beat / 4;
                    const int steps = static_cast<int> ((at - from) / step);
                    static constexpr int toms[] = { 50, 48, 47, 45, 43, 41 };
                    for (int i = 0; i < steps; ++i)
                    {
                        const double k = steps > 1 ? static_cast<double> (i) / (steps - 1) : 1.0;
                        // Snare first, then down the toms; louder to the end.
                        const int pitch = k < 0.34 ? 38 : toms[std::min<std::size_t> (5, static_cast<std::size_t> ((k - 0.34) / 0.66 * 6.0))];
                        addNote (s, d, from + i * step, step, pitch, static_cast<int> ((72.0 + 44.0 * k) * lift));
                    }
                    crashOnNext (d);
                }
                break;
            }
            case Transition::Kind::pickup:
            {
                const auto bass = bassTrack (s, devices);
                if (bass == 0)
                    break;
                // The first bass note of the next part, else the lowest note there.
                int target = -1;
                if (const auto* t = edit::findTrack (s, bass))
                {
                    const auto next = arranging::playedNotes (*t, at, at + beat);
                    if (! next.empty())
                        target = next.front().pitch;
                    else
                    {
                        const auto prev = arranging::playedNotes (*t, partStart, at);
                        int lowest = 128;
                        for (const auto& other : s.tracks)
                            if (makesNotes (other) && ! arranging::isDrumTrack (other, devices))
                                for (const auto& n : arranging::playedNotes (other, at, at + beat))
                                    lowest = std::min (lowest, n.pitch);
                        if (lowest < 128)
                        {
                            const int ref = prev.empty() ? 40 : prev.back().pitch;
                            target = lowest;
                            while (target - ref > 6)
                                target -= 12;
                            while (ref - target > 6)
                                target += 12;
                        }
                    }
                }
                if (target < 0)
                    break;
                clearNotes (s, bass, from, at, true);
                const Tick step = beat / 2;
                const int steps = std::max (1, static_cast<int> ((at - from) / step));
                for (int i = 0; i < steps; ++i)
                    addNote (s, bass, at - (steps - i) * step, step - 30, target - (steps - i), 88 + 4 * i);
                break;
            }
            case Transition::Kind::hold:
            {
                for (auto& t : s.tracks)
                {
                    if (! makesNotes (t))
                        continue;
                    const bool drum = arranging::isDrumTrack (t, devices);
                    const TrackId id = t.id;
                    if (drum)
                    {
                        clearNotes (s, id, from + 1, at, false);
                        addNote (s, id, from, beat / 2, 49, 110);
                        continue;
                    }
                    // The chord at the start of the hold rings to the next part; nothing else starts.
                    std::vector<ClipId> ids;
                    for (const auto& c : t.clips)
                        if (c != nullptr && c->end() > from - beat * 8 && c->start < at)
                            ids.push_back (c->id);
                    Tick chordAt = -1;
                    for (const auto& n : arranging::playedNotes (t, partStart, from + 1))
                        chordAt = std::max (chordAt, n.on);
                    for (auto cid : ids)
                        edit::modifyClip (s, cid, [&] (Clip& c) {
                            c = edit::playedClip (c);
                            std::erase_if (c.notes, [&] (const Note& n) { const Tick on = c.start + n.start; return on > from && on < at; });
                            for (auto& n : c.notes)
                            {
                                const Tick on = c.start + n.start;
                                if (on == chordAt || (on <= from && on + n.length > from))
                                    n.length = std::max (n.length, std::min (c.end(), at) - on - 20);
                            }
                        });
                }
                break;
            }
            case Transition::Kind::rest:
            {
                for (const auto& t : std::vector<Track> (s.tracks))
                    if (makesNotes (t))
                        clearNotes (s, t.id, from, at, true);
                for (auto d : drums)
                    crashOnNext (d);
                break;
            }
        }
    }
}

const char* kindId (Transition::Kind k)
{
    switch (k)
    {
        case Transition::Kind::none: return "none";
        case Transition::Kind::fill: return "fill";
        case Transition::Kind::pickup: return "pickup";
        case Transition::Kind::hold: return "hold";
        case Transition::Kind::rest: return "rest";
    }
    return "none";
}

std::string kindName (Transition::Kind k)
{
    switch (k)
    {
        case Transition::Kind::none: return "No change";
        case Transition::Kind::fill: return "Drum fill";
        case Transition::Kind::pickup: return "Bass pickup";
        case Transition::Kind::hold: return "Hold the chord";
        case Transition::Kind::rest: return "Break";
    }
    return "No change";
}

std::optional<Transition::Kind> parseKind (std::string_view text)
{
    const auto t = lower (std::string (text));
    for (auto k : { Transition::Kind::none, Transition::Kind::fill, Transition::Kind::pickup, Transition::Kind::hold, Transition::Kind::rest })
        if (t == kindId (k) || t == lower (kindName (k)))
            return k;
    if (t == "break" || t == "stop")
        return Transition::Kind::rest;
    if (t == "drum fill")
        return Transition::Kind::fill;
    return std::nullopt;
}

int defaultBeats (Transition::Kind k, const TimeSignature& sig)
{
    switch (k)
    {
        case Transition::Kind::fill: return std::min (2, sig.numerator);
        case Transition::Kind::pickup: return 1;
        case Transition::Kind::hold: return std::min (2, sig.numerator);
        case Transition::Kind::rest: return 1;
        case Transition::Kind::none: break;
    }
    return 0;
}

// ---- sections -----------------------------------------------------------------------------

const Section* findSection (const Song& s, std::uint32_t id)
{
    for (const auto& sec : s.sections)
        if (sec.id == id)
            return &sec;
    return nullptr;
}

void tidySections (Song& s)
{
    for (auto& sec : s.sections)
    {
        sec.start = std::max<Tick> (0, roundToBar (s, sec.start));
        sec.end = roundToBar (s, sec.end);
        if (sec.id == 0)
            sec.id = s.nextId++;
    }
    std::sort (s.sections.begin(), s.sections.end(), [] (const Section& a, const Section& b) { return a.start < b.start; });
    for (std::size_t i = 0; i + 1 < s.sections.size(); ++i)
        s.sections[i].end = std::min (s.sections[i].end, s.sections[i + 1].start);
    std::erase_if (s.sections, [] (const Section& sec) { return sec.end <= sec.start; });
}

std::vector<Section> findSections (Song& s)
{
    std::vector<Section> out;
    const Tick bar = barOf (s);
    const Tick end = (s.contentEnd() + bar - 1) / bar * bar;
    const auto bars = static_cast<int> (end / bar);
    if (bars < 2)
        return out;
    const int phrase = bars >= 24 ? 8 : (bars >= 8 ? 4 : bars);
    const int phrases = (bars + phrase - 1) / phrase;

    // What each phrase plays: track, pitch and place (to an 8th).
    std::vector<std::set<std::tuple<TrackId, int, Tick>>> played (static_cast<std::size_t> (phrases));
    for (const auto& t : s.tracks)
        if (makesNotes (t))
            for (const auto& n : arranging::playedNotes (t))
            {
                const auto p = static_cast<std::size_t> (std::min<Tick> (phrases - 1, n.on / (bar * phrase)));
                const Tick place = (n.on - static_cast<Tick> (p) * bar * phrase) / (kTicksPerQuarter / 2);
                played[p].insert ({ t.id, n.pitch, place });
            }
    const auto alike = [&] (std::size_t a, std::size_t b) {
        const auto& x = played[a];
        const auto& y = played[b];
        if (x.empty() && y.empty())
            return 1.0;
        std::size_t both = 0;
        for (const auto& e : x)
            both += y.count (e);
        return static_cast<double> (both) / static_cast<double> (x.size() + y.size() - both);
    };
    std::vector<int> group (static_cast<std::size_t> (phrases), -1);
    std::vector<std::size_t> firstOf;
    for (std::size_t p = 0; p < group.size(); ++p)
    {
        for (std::size_t g = 0; g < firstOf.size() && group[p] < 0; ++g)
            if (alike (p, firstOf[g]) >= 0.5)
                group[p] = static_cast<int> (g);
        if (group[p] < 0)
        {
            group[p] = static_cast<int> (firstOf.size());
            firstOf.push_back (p);
        }
    }
    // Neighbours of one group become one section.
    struct Part
    {
        int group;
        int fromPhrase, toPhrase;
    };
    std::vector<Part> parts;
    for (int p = 0; p < phrases; ++p)
    {
        if (! parts.empty() && parts.back().group == group[static_cast<std::size_t> (p)])
            parts.back().toPhrase = p + 1;
        else
            parts.push_back ({ group[static_cast<std::size_t> (p)], p, p + 1 });
    }

    std::map<int, int> count;
    for (const auto& p : parts)
        ++count[p.group];
    std::map<int, std::string> names;
    if (parts.size() >= 3 && count[parts.front().group] == 1)
        names[parts.front().group] = "Intro";
    if (parts.size() >= 3 && count[parts.back().group] == 1 && ! names.count (parts.back().group))
        names[parts.back().group] = "Ending";
    // The chorus: the group that comes most (the busier one on a tie).
    int chorus = -1;
    double best = -1.0;
    for (const auto& [g, n] : count)
    {
        if (names.count (g) || n < 2)
            continue;
        const auto& first = *std::find_if (parts.begin(), parts.end(), [g = g] (const Part& p) { return p.group == g; });
        const double busy = static_cast<double> (notesIn (s, first.fromPhrase * phrase * bar, first.toPhrase * phrase * bar)) / (first.toPhrase - first.fromPhrase);
        const double score = n * 10000.0 + busy;
        if (score > best)
        {
            best = score;
            chorus = g;
        }
    }
    if (chorus >= 0)
    {
        names[chorus] = "Chorus";
        for (std::size_t i = 1; i < parts.size(); ++i)
            if (parts[i].group == chorus && ! names.count (parts[i - 1].group))
            {
                names[parts[i - 1].group] = "Verse";
                break;
            }
    }
    const char* others[] = { "Bridge", "Part E", "Part F", "Part G", "Part H" };
    std::size_t next = 0;
    for (const auto& p : parts)
        if (! names.count (p.group))
            names[p.group] = next < std::size (others) ? others[next++] : "Part " + std::to_string (p.group + 1);

    std::map<int, int> seen;
    for (const auto& p : parts)
    {
        Section sec;
        sec.id = s.nextId++;
        sec.name = names[p.group];
        if (count[p.group] > 1)
            sec.name += " " + std::to_string (++seen[p.group]);
        sec.start = p.fromPhrase * phrase * bar;
        sec.end = std::min<Tick> (end, p.toPhrase * phrase * bar);
        out.push_back (sec);
    }
    s.sections = out;
    return out;
}

SongForm formFromNames (const Song& s, const std::string& name, const std::string& names, std::vector<std::string>* unknown)
{
    SongForm f;
    f.name = trim (name).empty() ? "Form" : trim (name);
    // Chunks between commas, bars and arrows; inside a chunk, the longest section names first.
    std::string text = names;
    for (const char* sep : { "->", "→", ">", "|", ";", "\n" })
        for (auto at = text.find (sep); at != std::string::npos; at = text.find (sep))
            text.replace (at, std::string (sep).size(), ",");
    std::vector<std::string> chunks;
    std::size_t at = 0;
    while (at <= text.size())
    {
        const auto comma = text.find (',', at);
        chunks.push_back (trim (text.substr (at, comma == std::string::npos ? std::string::npos : comma - at)));
        if (comma == std::string::npos)
            break;
        at = comma + 1;
    }
    for (const auto& chunk : chunks)
    {
        std::vector<std::string> words;
        std::size_t i = 0;
        while (i < chunk.size())
        {
            while (i < chunk.size() && std::isspace (static_cast<unsigned char> (chunk[i])))
                ++i;
            const auto from = i;
            while (i < chunk.size() && ! std::isspace (static_cast<unsigned char> (chunk[i])))
                ++i;
            if (i > from)
                words.push_back (chunk.substr (from, i - from));
        }
        std::size_t w = 0;
        while (w < words.size())
        {
            const auto word = lower (words[w]);
            // "x2" repeats the part before.
            if ((word.size() >= 2 && (word[0] == 'x' || word[0] == '*') && std::all_of (word.begin() + 1, word.end(), ::isdigit)) && ! f.parts.empty())
            {
                const int times = std::stoi (word.substr (1));
                for (int k = 1; k < std::min (times, 16); ++k)
                    f.parts.push_back (f.parts.back());
                ++w;
                continue;
            }
            std::size_t took = 0;
            std::uint32_t id = 0;
            for (std::size_t len = words.size() - w; len >= 1 && took == 0; --len)
            {
                std::string joined;
                for (std::size_t k = 0; k < len; ++k)
                    joined += (k > 0 ? " " : "") + lower (words[w + k]);
                for (const auto& sec : s.sections)
                    if (lower (sec.name) == joined)
                    {
                        id = sec.id;
                        took = len;
                        break;
                    }
            }
            if (took == 0)
                for (const auto& sec : s.sections) // "Verse" for "Verse 1"
                    if (lower (sec.name).rfind (word, 0) == 0)
                    {
                        id = sec.id;
                        took = 1;
                        break;
                    }
            if (took == 0)
            {
                if (unknown != nullptr)
                    unknown->push_back (words[w]);
                ++w;
                continue;
            }
            f.parts.push_back (id);
            w += took;
        }
    }
    fitTransitions (f);
    return f;
}

SongForm currentForm (const Song& s)
{
    SongForm f;
    f.name = "As on the Timeline";
    for (const auto& sec : s.sections)
        f.parts.push_back (sec.id);
    fitTransitions (f);
    return f;
}

std::string partsText (const Song& s, const SongForm& f)
{
    std::string out;
    for (std::size_t i = 0; i < f.parts.size();)
    {
        std::size_t j = i;
        while (j + 1 < f.parts.size() && f.parts[j + 1] == f.parts[i])
            ++j;
        const auto* sec = findSection (s, f.parts[i]);
        out += (out.empty() ? "" : ", ") + (sec != nullptr ? sec->name : std::string ("(missing)"));
        if (j > i)
            out += " x" + std::to_string (j - i + 1);
        i = j + 1;
    }
    return out;
}

void fitTransitions (SongForm& f)
{
    f.transitions.resize (f.parts.size() > 0 ? f.parts.size() - 1 : 0);
}

std::string problemWith (const Song& s, const SongForm& f)
{
    if (f.parts.empty())
        return "The form has no parts.";
    for (auto id : f.parts)
        if (findSection (s, id) == nullptr)
            return "A section of the form is not in the song now.";
    return {};
}

// ---- boundaries ----------------------------------------------------------------------------

std::vector<Tick> partStarts (const Song& s, const SongForm& f)
{
    std::vector<Tick> out;
    Tick at = 0;
    for (auto id : f.parts)
    {
        out.push_back (at);
        if (const auto* sec = findSection (s, id))
            at += sec->end - sec->start;
    }
    out.push_back (at);
    return out;
}

std::vector<Boundary> boundaries (const Song& s, const SongForm& f, const DeviceLibrary* devices)
{
    std::vector<Boundary> out;
    if (! problemWith (s, f).empty())
        return out;
    const auto starts = partStarts (s, f);
    const Tick bar = barOf (s);
    const bool drums = ! drumTracks (s, devices).empty();
    const bool bass = bassTrack (s, devices) != 0;
    for (std::size_t i = 0; i + 1 < f.parts.size(); ++i)
    {
        const auto& a = *findSection (s, f.parts[i]);
        const auto& b = *findSection (s, f.parts[i + 1]);
        Boundary x;
        x.index = static_cast<int> (i);
        x.from = a.id;
        x.to = b.id;
        x.at = starts[i + 1];
        x.isNew = a.end != b.start;
        x.densityBefore = notesIn (s, std::max (a.start, a.end - bar), a.end);
        x.densityAfter = notesIn (s, b.start, std::min (b.end, b.start + bar));
        x.kinds.push_back (Transition::Kind::none);
        if (drums)
            x.kinds.push_back (Transition::Kind::fill);
        if (bass)
            x.kinds.push_back (Transition::Kind::pickup);
        x.kinds.push_back (Transition::Kind::hold);
        x.kinds.push_back (Transition::Kind::rest);
        const auto canDo = [&] (Transition::Kind k) { return std::find (x.kinds.begin(), x.kinds.end(), k) != x.kinds.end(); };

        if (! x.isNew)
        {
            x.suggested = Transition::Kind::none;
            x.because = a.name + " goes into " + b.name + " in the song already.";
        }
        else if (x.densityAfter > x.densityBefore * 1.25 + 1.0)
        {
            x.suggested = canDo (Transition::Kind::fill) ? Transition::Kind::fill : (bass ? Transition::Kind::pickup : Transition::Kind::rest);
            x.because = b.name + " is busier than the end of " + a.name + ": " + (x.suggested == Transition::Kind::fill ? "a fill lifts into it." : "a lead-in lifts into it.");
        }
        else if (x.densityAfter < x.densityBefore * 0.75 - 1.0)
        {
            x.suggested = Transition::Kind::hold;
            x.because = b.name + " is quieter than the end of " + a.name + ": hold the last chord and let it settle.";
        }
        else if (bass)
        {
            x.suggested = Transition::Kind::pickup;
            x.because = a.name + " and " + b.name + " are alike in energy: a short bass run leads into the first note of " + b.name + ".";
        }
        else
        {
            x.suggested = canDo (Transition::Kind::fill) ? Transition::Kind::fill : Transition::Kind::rest;
            x.because = "A new join: mark it so it does not sound like a cut.";
        }
        out.push_back (std::move (x));
    }
    return out;
}

// ---- building ---------------------------------------------------------------------------------

Song build (const Song& source, const SongForm& form, const DeviceLibrary* devices)
{
    if (const auto problem = problemWith (source, form); ! problem.empty())
        throw std::runtime_error (problem);
    Song out = source;
    out.loop.enabled = false;
    out.sections.clear();
    for (auto& t : out.tracks)
    {
        t.clips.clear();
        t.audio.clear();
        t.marks.clear();
        for (int lane = 0; lane < Track::kFirstControlLane; ++lane)
            t.findLine (lane)->clear();
        for (auto* slot : slotsOf (t))
            for (auto& line : slot->automation)
                line.points.clear();
    }

    const auto starts = partStarts (source, form);
    std::vector<TempoPoint> tempo;
    for (std::size_t i = 0; i < form.parts.size(); ++i)
    {
        const auto& sec = *findSection (source, form.parts[i]);
        const Tick at = starts[i];
        const Tick shift = at - sec.start;
        const Tick length = sec.end - sec.start;

        Section placed;
        placed.id = out.nextId++;
        placed.name = sec.name;
        placed.start = at;
        placed.end = at + length;
        out.sections.push_back (placed);

        if (source.tempo.getPoints().size() > 1)
        {
            tempo.push_back ({ at, source.tempo.tempoAt (static_cast<double> (sec.start)) });
            for (const auto& p : source.tempo.getPoints())
                if (p.at > sec.start && p.at < sec.end)
                    tempo.push_back ({ p.at + shift, p.bpm });
        }

        for (std::size_t ti = 0; ti < source.tracks.size(); ++ti)
        {
            const auto& src = source.tracks[ti];
            auto& dst = out.tracks[ti];

            // MIDI: one clip for the part.
            Clip clip;
            clip.name = sec.name;
            clip.start = at;
            clip.length = length;
            for (const auto& ptr : src.clips)
            {
                if (ptr == nullptr || ptr->end() <= sec.start || ptr->start >= sec.end)
                    continue;
                const auto played = edit::playedClip (*ptr);
                for (const auto& n : played.notes)
                {
                    const Tick on = played.start + n.start;
                    if (on < sec.start || on >= sec.end)
                        continue;
                    Note copy = n;
                    copy.start = on - sec.start;
                    copy.length = std::min (n.length, sec.end - on);
                    clip.notes.push_back (copy);
                }
                for (const auto& c : played.controls)
                {
                    const Tick when = played.start + c.at;
                    if (when >= sec.start && when < sec.end)
                    {
                        auto copy = c;
                        copy.at = when - sec.start;
                        clip.controls.push_back (copy);
                    }
                }
            }
            if (! clip.notes.empty() || ! clip.controls.empty())
            {
                edit::normalise (clip);
                clip.id = out.nextId++;
                dst.clips.push_back (std::make_shared<const Clip> (std::move (clip)));
            }

            // Audio: the pieces of the clips inside the section.
            for (const auto& a : src.audio)
            {
                if (a == nullptr || a->end() <= sec.start || a->start >= sec.end)
                    continue;
                AudioClip piece = *a;
                piece.id = out.nextId++;
                const Tick from = std::max (a->start, sec.start), to = std::min (a->end(), sec.end);
                piece.offset = a->offset + (source.tempo.secondsAt (static_cast<double> (from)) - source.tempo.secondsAt (static_cast<double> (a->start))) * edit::audioSpeed (source, *a);
                piece.start = from + shift;
                piece.length = to - from;
                if (from > a->start)
                    piece.fadeIn = std::min<Tick> (kTicksPerQuarter / 48, piece.length / 2);
                if (to < a->end())
                    piece.fadeOut = std::min<Tick> (kTicksPerQuarter / 48, piece.length / 2);
                dst.audio.push_back (std::make_shared<const AudioClip> (piece));
            }

            for (const auto& m : src.marks)
                if (m.at >= sec.start && m.at < sec.end)
                {
                    auto copy = m;
                    copy.at += shift;
                    dst.marks.push_back (copy);
                }
            for (int lane = 0; lane < Track::kFirstControlLane; ++lane)
                copyLine (src.automationLine (lane), sec.start, sec.end, shift, *dst.findLine (lane));
            const auto srcSlots = slotsOf (src);
            const auto dstSlots = slotsOf (dst);
            for (std::size_t k = 0; k < srcSlots.size() && k < dstSlots.size(); ++k)
                for (std::size_t l = 0; l < srcSlots[k]->automation.size() && l < dstSlots[k]->automation.size(); ++l)
                    copyLine (srcSlots[k]->automation[l].points, sec.start, sec.end, shift, dstSlots[k]->automation[l].points);
        }
    }
    if (! tempo.empty())
        out.tempo.setPoints (tempo);
    for (auto& t : out.tracks)
    {
        std::sort (t.audio.begin(), t.audio.end(), [] (const AudioClipPtr& a, const AudioClipPtr& b) { return a->start < b->start; });
        std::stable_sort (t.marks.begin(), t.marks.end(), [] (const ScoreMark& a, const ScoreMark& b) { return a.at < b.at; });
    }

    // The transitions, from the last to the first (each one only touches the end of its part and the next downbeat).
    const auto joins = boundaries (source, form, devices);
    for (std::size_t i = form.transitions.size(); i-- > 0;)
    {
        if (i + 1 >= starts.size() - 1 || i >= joins.size())
            continue;
        applyTransition (out, form.transitions[i], starts[i + 1], starts[i], joins[i].densityBefore, joins[i].densityAfter, devices);
    }
    return out;
}

void use (Song& song, const SongForm& form, const DeviceLibrary* devices)
{
    auto built = build (song, form, devices);

    // The other forms name the copies: the first copy of each section.
    std::map<std::uint32_t, std::uint32_t> copyOf;
    for (std::size_t i = 0; i < form.parts.size(); ++i)
        copyOf.emplace (form.parts[i], built.sections[i].id);
    // Repeats get their own names, so a form can name each copy.
    std::map<std::string, int> seen;
    for (auto& sec : built.sections)
        if (const int n = ++seen[sec.name]; n > 1)
            sec.name += " (" + std::to_string (n) + ")";

    built.forms.clear();
    for (const auto& other : song.forms)
    {
        if (other.name == form.name)
            continue;
        SongForm mapped = other;
        bool ok = ! mapped.parts.empty();
        for (auto& id : mapped.parts)
        {
            const auto it = copyOf.find (id);
            if (it == copyOf.end())
            {
                ok = false;
                break;
            }
            id = it->second;
        }
        if (ok)
            built.forms.push_back (std::move (mapped));
    }
    song = std::move (built);
}

} // namespace arranger::song_form

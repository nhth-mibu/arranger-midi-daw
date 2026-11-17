#include "arranger/core/HardwareFit.h"

#include "ArrangingDetail.h"

#include "arranger/core/GeneralMidi.h"
#include "arranger/core/SongEdit.h"

#include <algorithm>
#include <map>
#include <set>

namespace arranger::hardware
{

using namespace arranging;
using namespace arranging::detail;

DeviceCapabilities soundFontCapabilities()
{
    return { 256, 16, 0, 127, {}, "built-in" };
}

namespace
{
    struct Voice
    {
        Tick on = 0, off = 0;
        int pitch = 60;
        TrackId track = 0;
        bool removed = false;
    };

    /** Where the sustain pedal is down on a track (song times). */
    std::vector<std::pair<Tick, Tick>> pedalSpans (const Track& t)
    {
        std::vector<std::pair<Tick, int>> moves;
        for (const auto& ptr : t.clips)
        {
            if (ptr == nullptr)
                continue;
            const auto played = edit::playedClip (*ptr);
            for (const auto& c : played.controls)
                if (c.kind == ControlEvent::Kind::controller && c.number == 64)
                    moves.push_back ({ played.start + c.at, c.value });
            // The clip end lifts the pedal.
            if (! played.controls.empty())
                moves.push_back ({ played.end(), 0 });
        }
        std::stable_sort (moves.begin(), moves.end(), [] (auto& a, auto& b) { return a.first < b.first; });
        std::vector<std::pair<Tick, Tick>> spans;
        Tick down = -1;
        for (const auto& [at, value] : moves)
        {
            if (value >= 64 && down < 0)
                down = at;
            else if (value < 64 && down >= 0)
            {
                spans.push_back ({ down, at });
                down = -1;
            }
        }
        return spans;
    }

    std::vector<Voice> voicesOf (const Track& t)
    {
        std::vector<Voice> out;
        const auto pedal = pedalSpans (t);
        for (const auto& n : playedNotes (t))
        {
            Voice v { n.on, n.off, n.pitch, t.id, false };
            for (const auto& [down, up] : pedal)
                if (v.off >= down && v.off < up)
                    v.off = up;
            out.push_back (v);
        }
        return out;
    }

    struct Sweep
    {
        int peak = 0;
        Tick peakAt = 0;
        std::vector<std::pair<Tick, Tick>> over;
    };

    Sweep sweep (const std::vector<Voice>& voices, int limit)
    {
        std::vector<std::pair<Tick, int>> events;
        for (const auto& v : voices)
            if (! v.removed)
            {
                events.push_back ({ v.on, 1 });
                events.push_back ({ v.off, -1 });
            }
        std::sort (events.begin(), events.end()); // at the same time, an end (-1) comes first
        Sweep s;
        int now = 0;
        Tick overFrom = -1;
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            now += events[i].second;
            // Only count after all the events at this time.
            if (i + 1 < events.size() && events[i + 1].first == events[i].first)
                continue;
            if (now > s.peak)
            {
                s.peak = now;
                s.peakAt = events[i].first;
            }
            if (limit > 0)
            {
                if (now > limit && overFrom < 0)
                    overFrom = events[i].first;
                else if (now <= limit && overFrom >= 0)
                {
                    s.over.push_back ({ overFrom, events[i].first });
                    overFrom = -1;
                }
            }
        }
        return s;
    }

    int soundingAt (const std::vector<Voice>& voices, Tick t)
    {
        int n = 0;
        for (const auto& v : voices)
            if (! v.removed && v.on <= t && t < v.off)
                ++n;
        return n;
    }

    int priority (Role r)
    {
        switch (r)
        {
            case Role::melody: return 0;
            case Role::bass: return 1;
            case Role::drums: return 2;
            case Role::chords: return 3;
            case Role::other: break;
        }
        return 4;
    }

    bool isProtected (Role r) { return r == Role::melody || r == Role::bass || r == Role::drums; }

    int gmProgramFor (const Track& t, Role role)
    {
        const auto name = t.name + " " + t.sound.name;
        struct Hint { std::initializer_list<const char*> words; int program; };
        const Hint hints[] = {
            { { "e.piano", "epiano", "e-piano", "rhodes", "tine" }, 4 }, { { "piano" }, 0 }, { { "organ" }, 16 }, { { "guitar" }, 24 },
            { { "bass" }, 38 },      { { "string" }, 48 }, { { "choir", "voice", "vox" }, 52 }, { { "brass", "horn" }, 61 },
            { { "sax" }, 65 },       { { "flute" }, 73 }, { { "lead" }, 81 },  { { "pad", "sweep", "atmos" }, 89 }, { { "bell", "harp" }, 46 },
        };
        for (const auto& h : hints)
            if (nameHas (name, h.words))
                return h.program;
        switch (role)
        {
            case Role::bass: return 38;
            case Role::melody: return 81;
            case Role::chords: return 89;
            case Role::drums: return 0;
            case Role::other: break;
        }
        return 89;
    }

    /** A SoundFont channel no SoundFont track uses (10 for drums), or 0. */
    int freeSoundFontChannel (const std::set<int>& used, bool drums)
    {
        if (drums)
            return used.count (10) == 0 ? 10 : 0;
        for (int ch = 1; ch <= 16; ++ch)
            if (ch != 10 && used.count (ch) == 0)
                return ch;
        return 0;
    }

    std::set<int> soundFontChannels (const Song& s)
    {
        std::set<int> used;
        for (const auto& t : s.tracks)
            if (t.output.kind == TrackOutput::Kind::soundFont && ! t.clips.empty())
                used.insert (t.output.channel);
        return used;
    }

    /** The track moved to the built-in SoundFont player on a free channel. False when no channel is free. */
    bool moveToSoundFont (Track& t, Role role, std::set<int>& used, std::string& what)
    {
        const bool drums = role == Role::drums;
        const int ch = freeSoundFontChannel (used, drums);
        if (ch == 0)
            return false;
        used.insert (ch);
        t.output = { TrackOutput::Kind::soundFont, {}, ch };
        if (drums)
            t.sound = { "GM: Standard Kit", -1, -1, 0 };
        else
        {
            const int program = gmProgramFor (t, role);
            t.sound = { std::string ("GM: ") + kGeneralMidiNames[program], -1, -1, program };
        }
        what = t.name + " plays on the built-in SoundFont (channel " + std::to_string (ch) + ", " + t.sound.name.substr (4) + ")";
        return true;
    }

    std::string joinNames (const Song& s, const std::vector<TrackId>& ids)
    {
        std::string out;
        for (std::size_t i = 0; i < ids.size(); ++i)
            out += (i == 0 ? "" : (i + 1 == ids.size() ? " and " : ", ")) + trackName (s, ids[i]);
        return out;
    }

    std::string joinNumbers (const std::vector<int>& v)
    {
        std::string out;
        for (std::size_t i = 0; i < v.size(); ++i)
            out += (i == 0 ? "" : (i + 1 == v.size() ? " and " : ", ")) + std::to_string (v[i]);
        return out;
    }

    struct Checker
    {
        const Song& song;
        const DeviceLibrary& library;
        std::map<TrackId, Role> roles;
        Report report;

        Role role (TrackId id) const
        {
            const auto it = roles.find (id);
            return it != roles.end() ? it->second : Role::other;
        }

        std::vector<const Track*> tracksOf (const DeviceUse& d, const std::vector<Track>& replaced = {}) const
        {
            std::vector<const Track*> out;
            for (auto id : d.tracks)
            {
                const auto it = std::find_if (replaced.begin(), replaced.end(), [&] (const Track& t) { return t.id == id; });
                if (it != replaced.end())
                {
                    if (it->output.kind == TrackOutput::Kind::midiPort && it->output.port == d.key)
                        out.push_back (&*it);
                    else if (it->output.kind == TrackOutput::Kind::soundFont && d.key == "soundfont")
                        out.push_back (&*it);
                }
                else
                    out.push_back (edit::findTrack (song, id));
            }
            return out;
        }

        int peakWith (const DeviceUse& d, const std::vector<Track>& replaced) const { return peakVoices (tracksOf (d, replaced)).first; }

        void group()
        {
            std::map<std::string, DeviceUse> byKey;
            std::vector<std::string> order;
            for (const auto& t : song.tracks)
            {
                if (playedNotes (t).empty())
                    continue;
                std::string key;
                if (t.output.kind == TrackOutput::Kind::soundFont)
                    key = "soundfont";
                else if (t.output.kind == TrackOutput::Kind::midiPort)
                    key = t.output.port;
                else
                    continue; // plugins, samplers and audio have no fixed voice count
                auto [it, isNew] = byKey.try_emplace (key);
                auto& d = it->second;
                if (isNew)
                {
                    order.push_back (key);
                    d.key = key;
                    if (key == "soundfont")
                    {
                        d.name = "Built-in SoundFont";
                        d.profileId = "soundfont";
                        d.capabilities = soundFontCapabilities();
                    }
                    else
                    {
                        const auto& p = library.forPort (key);
                        d.profileId = p.id;
                        d.capabilities = p.capabilities;
                        d.name = key.empty() ? p.name + " (no port chosen)" : (detail::lower (key).find (detail::lower (p.name)) != std::string::npos ? key : p.name + " (" + key + ")");
                    }
                }
                d.tracks.push_back (t.id);
                if (std::find (d.channels.begin(), d.channels.end(), t.output.channel) == d.channels.end())
                    d.channels.push_back (t.output.channel);
            }
            for (const auto& k : order)
            {
                auto d = byKey[k];
                std::sort (d.channels.begin(), d.channels.end());
                report.devices.push_back (std::move (d));
            }
        }

        // ---- polyphony ----
        void polyphony (DeviceUse& d)
        {
            const auto tracks = tracksOf (d);
            std::vector<Voice> voices;
            for (const auto* t : tracks)
            {
                auto v = voicesOf (*t);
                voices.insert (voices.end(), v.begin(), v.end());
            }
            const int limit = d.capabilities.polyphony;
            const auto sw = sweep (voices, limit);
            d.peakVoices = sw.peak;
            d.peakAt = sw.peakAt;
            d.overloaded = sw.over;
            if (limit <= 0)
            {
                d.problems.push_back ("The app doesn't know how many notes " + d.name + " plays at once. Up to " + std::to_string (sw.peak)
                                      + " sound together. Enter its polyphony in Fit to My Synths.");
                return;
            }
            if (sw.over.empty())
                return;

            // Bars with too many notes, joined.
            const Tick bar = barLength (song);
            std::vector<std::pair<Tick, Tick>> bars;
            for (const auto& [a, b] : sw.over)
            {
                const Tick from = a / bar * bar, to = std::max (from + bar, (b + bar - 1) / bar * bar);
                if (! bars.empty() && from <= bars.back().second)
                    bars.back().second = std::max (bars.back().second, to);
                else
                    bars.push_back ({ from, to });
            }
            std::string where;
            for (std::size_t i = 0; i < bars.size() && i < 4; ++i)
                where += (i == 0 ? "" : ", ") + barsText (song, bars[i].first, bars[i].second);
            if (bars.size() > 4)
                where += " and " + std::to_string (bars.size() - 4) + " more places";
            const std::string problem = "Up to " + std::to_string (sw.peak) + " notes sound at once on " + d.name + " (in " + where + "), but it plays "
                                        + std::to_string (limit) + " (" + d.capabilities.source + "). Notes will cut off.";
            d.problems.push_back (problem);

            // Which tracks can give way: not bass, melody or drums.
            std::vector<TrackId> lighter;
            for (auto id : d.tracks)
                if (! isProtected (role (id)))
                    lighter.push_back (id);
            if (lighter.empty())
            {
                d.problems.push_back ("Only the bass, melody or drums play on " + d.name + " there. Choose a simpler patch, or move a track to another synth.");
                return;
            }
            const Tick from = bars.front().first, to = bars.back().second;
            auto p = makeProposal (song, "polyphony", "polyphony:" + d.key, "Fit the song in the " + std::to_string (limit) + " voices of " + d.name, problem
                                   + " Bass, melody and drums stay as they are.", lighter, from, to);

            // A: remove chord notes that another note already doubles, where there are too many.
            {
                auto work = voices;
                const auto overAt = [&] (Tick t) {
                    return std::any_of (sw.over.begin(), sw.over.end(), [&] (auto& s) { return t >= s.first && t < s.second; });
                };
                for (auto& c : work)
                {
                    if (std::find (lighter.begin(), lighter.end(), c.track) == lighter.end() || ! overAt (c.on))
                        continue;
                    if (soundingAt (work, c.on) <= limit)
                        continue;
                    const bool doubled = std::any_of (work.begin(), work.end(), [&] (const Voice& v) {
                        return &v != &c && ! v.removed && v.on <= c.on && c.on < v.off && v.pitch % 12 == c.pitch % 12;
                    });
                    if (doubled)
                        c.removed = true;
                }
                int removed = 0;
                for (const auto& v : work)
                    removed += v.removed ? 1 : 0;
                // Still too many: also the inner notes of chords (the top and bottom notes stay).
                std::string how = "Removes chord notes that another note already plays (the same note name)";
                if (sweep (work, limit).peak > limit)
                {
                    for (auto& c : work)
                    {
                        if (c.removed || std::find (lighter.begin(), lighter.end(), c.track) == lighter.end() || ! overAt (c.on) || soundingAt (work, c.on) <= limit)
                            continue;
                        int lo = 128, hi = -1, together = 0;
                        for (const auto& v : work)
                            if (! v.removed && v.track == c.track && v.on == c.on)
                            {
                                lo = std::min (lo, v.pitch);
                                hi = std::max (hi, v.pitch);
                                ++together;
                            }
                        if (together >= 3 && c.pitch != lo && c.pitch != hi)
                        {
                            c.removed = true;
                            ++removed;
                        }
                    }
                    how += ", then inner chord notes";
                }
                std::vector<Track> changed;
                for (auto id : lighter)
                {
                    std::set<std::pair<Tick, int>> gone;
                    for (const auto& v : work)
                        if (v.removed && v.track == id)
                            gone.insert ({ v.on, v.pitch });
                    if (gone.empty())
                        continue;
                    Track t = *edit::findTrack (song, id);
                    editNotes (t, 0, std::numeric_limits<Tick>::max(), [&] (Note& n) { return gone.count ({ n.start, n.pitch }) == 0; });
                    changed.push_back (std::move (t));
                }
                const int after = sweep (work, limit).peak;
                addOption (p, "Remove doubled chord notes", how + ": " + count (removed, "note", "notes") + " fewer. The most at once goes from " + std::to_string (sw.peak)
                           + " to " + std::to_string (after) + (after > limit ? ", still more than " + std::to_string (limit) + "." : "."), std::move (changed), removed);
            }

            // B: the busiest of the lighter tracks plays on the built-in SoundFont.
            if (d.key != "soundfont")
            {
                TrackId busiest = 0;
                int most = 0;
                for (auto id : lighter)
                {
                    int n = 0;
                    for (const auto& v : voices)
                        if (v.track == id)
                            for (const auto& [a, b] : sw.over)
                                if (v.on < b && v.off > a)
                                {
                                    ++n;
                                    break;
                                }
                    if (n > most)
                    {
                        most = n;
                        busiest = id;
                    }
                }
                if (busiest != 0)
                {
                    Track t = *edit::findTrack (song, busiest);
                    auto used = soundFontChannels (song);
                    std::string what;
                    if (moveToSoundFont (t, role (busiest), used, what))
                    {
                        const int after = peakWith (d, { t });
                        std::vector<Track> moved { std::move (t) };
                        addOption (p, "Move " + trackName (song, busiest) + " to the built-in synth", what + ". The most at once on " + d.name + " goes from "
                                   + std::to_string (sw.peak) + " to " + std::to_string (after) + ".", std::move (moved), most);
                    }
                }
            }
            p.weight = 2.0 * sw.peak / std::max (1, limit);
            if (! p.options.empty())
                report.proposals.push_back (std::move (p));
        }

        // ---- parts and channels ----
        void parts (DeviceUse& d)
        {
            const int partsLimit = d.capabilities.parts;
            // Tracks on one channel with different sounds: one sound wins.
            std::map<int, std::vector<TrackId>> byChannel;
            for (auto id : d.tracks)
                byChannel[edit::findTrack (song, id)->output.channel].push_back (id);
            for (const auto& [ch, ids] : byChannel)
            {
                for (std::size_t i = 1; i < ids.size(); ++i)
                {
                    const auto& a = *edit::findTrack (song, ids.front());
                    const auto& b = *edit::findTrack (song, ids[i]);
                    if (a.sound.program < 0 || b.sound.program < 0 || a.sound == b.sound)
                        continue;
                    d.problems.push_back (a.name + " and " + b.name + " share channel " + std::to_string (ch) + " on " + d.name + " but ask for different sounds. Only one sound plays.");
                    // Another channel on the same device when it has a free part, else the built-in synth.
                    auto p = makeProposal (song, "channel", "channel:" + d.key + ":" + std::to_string (b.id), "Give " + b.name + " its own channel", d.problems.back(), { b.id }, 0, songEnd (song));
                    Track t = b;
                    const int usedParts = static_cast<int> (byChannel.size());
                    if (d.key != "soundfont" && (partsLimit == 0 || usedParts < partsLimit))
                    {
                        for (int c = 1; c <= 16; ++c)
                            if (byChannel.count (c) == 0 && c != 10)
                            {
                                t.output.channel = c;
                                addOption (p, "Use channel " + std::to_string (c), b.name + " plays on channel " + std::to_string (c) + " of " + d.name + ".", { t }, 1);
                                break;
                            }
                    }
                    else
                    {
                        auto used = soundFontChannels (song);
                        std::string what;
                        if (d.key == "soundfont")
                            used.insert (ch);
                        if (moveToSoundFont (t, role (b.id), used, what))
                            addOption (p, "Move it to the built-in synth", what + ".", { t }, 1);
                    }
                    p.weight = 2.0;
                    if (! p.options.empty())
                        report.proposals.push_back (std::move (p));
                }
            }

            if (partsLimit <= 0 || static_cast<int> (byChannel.size()) <= partsLimit)
                return;
            // Keep the most important parts on the device; the rest move.
            std::vector<std::pair<int, TrackId>> ranked; // priority, track of each channel
            for (const auto& [ch, ids] : byChannel)
            {
                TrackId best = ids.front();
                for (auto id : ids)
                    if (priority (role (id)) < priority (role (best)))
                        best = id;
                ranked.push_back ({ priority (role (best)), best });
            }
            std::stable_sort (ranked.begin(), ranked.end());
            std::vector<TrackId> moving;
            for (std::size_t i = static_cast<std::size_t> (partsLimit); i < ranked.size(); ++i)
                for (auto id : byChannel[edit::findTrack (song, ranked[i].second)->output.channel])
                    moving.push_back (id);
            const auto problem = d.name + " plays " + count (partsLimit, "part", "parts") + " (one sound at a time" + (partsLimit == 1 ? "" : " on each channel")
                                 + "), but the song sends it " + std::to_string (byChannel.size()) + " channels (" + joinNumbers (d.channels) + ").";
            d.problems.push_back (problem);
            auto p = makeProposal (song, "parts", "parts:" + d.key, "Fit the parts on " + d.name, problem + " " + trackName (song, ranked.front().second) + " stays on it.",
                                   moving, 0, songEnd (song));
            {
                auto used = soundFontChannels (song);
                std::vector<Track> moved;
                std::string all;
                for (auto id : moving)
                {
                    Track t = *edit::findTrack (song, id);
                    std::string what;
                    if (moveToSoundFont (t, role (id), used, what))
                    {
                        all += (all.empty() ? "" : "; ") + what;
                        moved.push_back (std::move (t));
                    }
                }
                const int n = static_cast<int> (moved.size());
                addOption (p, "Move " + joinNames (song, moving) + " to the built-in synth", all + ".", std::move (moved), n);
            }
            // B: another synth of the song with free parts.
            for (const auto& other : report.devices)
            {
                if (other.key == d.key || other.key == "soundfont" || other.capabilities.parts <= 0)
                    continue;
                const auto& otherProfile = library.forPort (other.key);
                std::set<int> taken (other.channels.begin(), other.channels.end());
                if (static_cast<int> (taken.size() + moving.size()) > other.capabilities.parts)
                    continue;
                std::vector<Track> moved;
                std::string all;
                for (auto id : moving)
                {
                    Track t = *edit::findTrack (song, id);
                    const bool drums = role (id) == Role::drums;
                    int ch = 0;
                    for (int c = 1; c <= 16 && ch == 0; ++c)
                    {
                        const bool drumChannel = std::find (otherProfile.drumChannels.begin(), otherProfile.drumChannels.end(), c) != otherProfile.drumChannels.end();
                        if (taken.count (c) == 0 && drumChannel == drums)
                            ch = c;
                    }
                    if (ch == 0)
                        break;
                    taken.insert (ch);
                    t.output = { TrackOutput::Kind::midiPort, other.key, ch };
                    if (! drums && otherProfile.dialect != "none")
                    {
                        const int program = gmProgramFor (t, role (id));
                        t.sound = { std::string ("GM: ") + kGeneralMidiNames[program], -1, -1, program };
                    }
                    all += (all.empty() ? "" : "; ") + t.name + " plays on " + other.name + " channel " + std::to_string (ch);
                    moved.push_back (std::move (t));
                }
                if (moved.size() == moving.size())
                {
                    const int n = static_cast<int> (moved.size());
                    addOption (p, "Move them to " + other.name, all + ". Check the sounds there.", std::move (moved), n);
                    break;
                }
            }
            p.weight = 2.5;
            if (! p.options.empty())
                report.proposals.push_back (std::move (p));
        }

        // ---- drums on a synth without a drum kit ----
        void drums (DeviceUse& d)
        {
            if (d.key == "soundfont")
                return;
            const auto& profile = library.forPort (d.key);
            for (auto id : d.tracks)
            {
                const auto& t = *edit::findTrack (song, id);
                if (role (id) != Role::drums)
                    continue;
                if (std::find (profile.drumChannels.begin(), profile.drumChannels.end(), t.output.channel) != profile.drumChannels.end())
                    continue;
                const auto problem = t.name + " is a drum part, but " + d.name + " has no drum kit on channel " + std::to_string (t.output.channel) + ".";
                d.problems.push_back (problem);
                auto p = makeProposal (song, "drums", "drums:" + std::to_string (id), "Play " + t.name + " on a drum kit", problem, { id }, 0, songEnd (song));
                Track moved = t;
                auto used = soundFontChannels (song);
                std::string what;
                if (moveToSoundFont (moved, Role::drums, used, what))
                    addOption (p, "Move it to the built-in drum kit", what + ".", { moved }, 1);
                p.weight = 2.2;
                if (! p.options.empty())
                    report.proposals.push_back (std::move (p));
            }
        }

        // ---- note range ----
        void range (DeviceUse& d)
        {
            const int lo = d.capabilities.lowestNote, hi = d.capabilities.highestNote;
            if (lo <= 0 && hi >= 127)
                return;
            for (auto id : d.tracks)
            {
                if (role (id) == Role::drums)
                    continue;
                const auto& t = *edit::findTrack (song, id);
                int outside = 0, low = 128, high = -1;
                for (const auto& n : playedNotes (t))
                    if (n.pitch < lo || n.pitch > hi)
                    {
                        ++outside;
                        low = std::min (low, n.pitch);
                        high = std::max (high, n.pitch);
                    }
                if (outside == 0)
                    continue;
                const auto problem = t.name + " has " + count (outside, "note", "notes") + " (" + pitchText (low) + (low == high ? "" : "-" + pitchText (high)) + ") outside the range of "
                                     + d.name + " (" + pitchText (lo) + "-" + pitchText (hi) + "). They will not sound.";
                d.problems.push_back (problem);
                auto p = makeProposal (song, "range", "range:" + std::to_string (id), "Bring " + t.name + " into the range of " + d.name, problem, { id }, 0, songEnd (song));
                {
                    Track moved = t;
                    const int n = editNotes (moved, 0, std::numeric_limits<Tick>::max(), [&] (Note& note) {
                        int pitch = note.pitch;
                        while (pitch < lo && pitch + 12 <= 127)
                            pitch += 12;
                        while (pitch > hi && pitch - 12 >= 0)
                            pitch -= 12;
                        note.pitch = static_cast<std::uint8_t> (pitch);
                        return true;
                    });
                    addOption (p, "Move the notes by octaves into range", "Moves " + count (n, "note", "notes") + " of " + t.name + " up or down by octaves.", { std::move (moved) }, n);
                }
                {
                    Track moved = t;
                    auto used = soundFontChannels (song);
                    std::string what;
                    if (moveToSoundFont (moved, role (id), used, what))
                        addOption (p, "Move it to the built-in synth", what + ".", { std::move (moved) }, outside);
                }
                p.weight = 1.0 + outside / 20.0;
                if (! p.options.empty())
                    report.proposals.push_back (std::move (p));
            }
        }

        // ---- controllers ----
        void controllers (DeviceUse& d)
        {
            const auto& known = d.capabilities.controllers;
            if (known.empty())
                return;
            std::set<int> unknown;
            for (auto id : d.tracks)
                for (const auto& c : edit::findTrack (song, id)->clips)
                    if (c != nullptr)
                        for (const auto& e : c->controls)
                            if (e.kind == ControlEvent::Kind::controller && std::find (known.begin(), known.end(), e.number) == known.end())
                                unknown.insert (e.number);
            if (unknown.empty())
                return;
            d.problems.push_back (d.name + " does not react to controller " + joinNumbers (std::vector<int> (unknown.begin(), unknown.end())) + ", which the song sends.");
        }
    };
}

std::pair<int, Tick> peakVoices (const std::vector<const Track*>& tracks)
{
    std::vector<Voice> voices;
    for (const auto* t : tracks)
        if (t != nullptr)
        {
            auto v = voicesOf (*t);
            voices.insert (voices.end(), v.begin(), v.end());
        }
    const auto s = sweep (voices, 0);
    return { s.peak, s.peakAt };
}

Report check (const Song& song, const DeviceLibrary& library, const Preferences* prefs, TrackId melody)
{
    Checker c { song, library, guessRoles (song, &library, melody), {} };
    c.group();
    for (auto& d : c.report.devices)
    {
        c.polyphony (d);
        c.parts (d);
        c.drums (d);
        c.range (d);
        c.controllers (d);
    }
    rank (c.report.proposals, prefs);
    c.report.roles = c.roles;
    return c.report;
}

} // namespace arranger::hardware

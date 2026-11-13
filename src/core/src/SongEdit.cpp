#include "arranger/core/SongEdit.h"

#include <algorithm>
#include <array>

namespace arranger::edit
{

namespace
{
    void sortClips (Track& t)
    {
        std::stable_sort (t.clips.begin(), t.clips.end(), [] (const ClipPtr& a, const ClipPtr& b) { return a->start < b->start; });
    }
}

std::uint32_t takeId (Song& song)
{
    return song.nextId++;
}

Track* findTrack (Song& song, TrackId id)
{
    for (auto& t : song.tracks)
        if (t.id == id)
            return &t;
    return nullptr;
}

const Track* findTrack (const Song& song, TrackId id)
{
    for (const auto& t : song.tracks)
        if (t.id == id)
            return &t;
    return nullptr;
}

int trackIndex (const Song& song, TrackId id)
{
    for (std::size_t i = 0; i < song.tracks.size(); ++i)
        if (song.tracks[i].id == id)
            return static_cast<int> (i);
    return -1;
}

ClipRef findClip (Song& song, ClipId id)
{
    for (auto& t : song.tracks)
        for (std::size_t i = 0; i < t.clips.size(); ++i)
            if (t.clips[i]->id == id)
                return { &t, i, false };
    for (auto& t : song.tracks)
        for (std::size_t i = 0; i < t.slots.size(); ++i)
            if (t.slots[i] != nullptr && t.slots[i]->id == id)
                return { &t, i, true };
    return {};
}

ClipPtr findClipPtr (const Song& song, ClipId id)
{
    for (const auto& t : song.tracks)
        for (const auto& c : t.clips)
            if (c->id == id)
                return c;
    for (const auto& t : song.tracks)
        for (const auto& c : t.slots)
            if (c != nullptr && c->id == id)
                return c;
    return nullptr;
}

TrackId trackOfClip (const Song& song, ClipId id)
{
    for (const auto& t : song.tracks)
        for (const auto& c : t.clips)
            if (c->id == id)
                return t.id;
    for (const auto& t : song.tracks)
        for (const auto& c : t.slots)
            if (c != nullptr && c->id == id)
                return t.id;
    return 0;
}

Track& addTrack (Song& song, const std::string& name, int insertAfter)
{
    Track t;
    t.id = takeId (song);
    t.name = name;

    // Least-used colour, and the lowest MIDI channel not used yet (skipping 10, the drum channel).
    std::array<int, std::size (kTrackColours)> colourUse {};
    std::array<bool, 17> channelUsed {};
    for (const auto& other : song.tracks)
    {
        for (std::size_t c = 0; c < colourUse.size(); ++c)
            if (other.colour == kTrackColours[c])
                ++colourUse[c];
        channelUsed[static_cast<std::size_t> (std::clamp (other.output.channel, 1, 16))] = true;
    }
    const auto colour = std::min_element (colourUse.begin(), colourUse.end()) - colourUse.begin();
    t.colour = kTrackColours[colour];
    t.output.channel = 1;
    for (int ch = 1; ch <= 16; ++ch)
        if (ch != 10 && ! channelUsed[static_cast<std::size_t> (ch)])
        {
            t.output.channel = ch;
            break;
        }

    const auto pos = insertAfter < 0 || insertAfter >= static_cast<int> (song.tracks.size())
                       ? song.tracks.end()
                       : song.tracks.begin() + insertAfter + 1;
    return *song.tracks.insert (pos, std::move (t));
}

void removeTrack (Song& song, TrackId id)
{
    std::erase_if (song.tracks, [id] (const Track& t) { return t.id == id; });
}

void moveTrack (Song& song, TrackId id, int newIndex)
{
    const int from = trackIndex (song, id);
    if (from < 0)
        return;
    newIndex = std::clamp (newIndex, 0, static_cast<int> (song.tracks.size()) - 1);
    auto t = std::move (song.tracks[static_cast<std::size_t> (from)]);
    song.tracks.erase (song.tracks.begin() + from);
    song.tracks.insert (song.tracks.begin() + newIndex, std::move (t));
}

ClipId addClip (Song& song, TrackId trackId, Clip clip)
{
    auto* t = findTrack (song, trackId);
    if (t == nullptr)
        return 0;
    if (clip.id == 0)
        clip.id = takeId (song);
    if (clip.name.empty())
        clip.name = t->name + " " + std::to_string (t->clips.size() + 1);
    normalise (clip);
    const auto id = clip.id;
    t->clips.push_back (std::make_shared<const Clip> (std::move (clip)));
    sortClips (*t);
    return id;
}

void removeClip (Song& song, ClipId id)
{
    for (auto& t : song.tracks)
    {
        std::erase_if (t.clips, [id] (const ClipPtr& c) { return c->id == id; });
        for (auto& slot : t.slots)
            if (slot != nullptr && slot->id == id)
                slot = nullptr;
    }
}

ClipId setSlot (Song& song, TrackId trackId, int scene, Clip clip)
{
    auto* t = findTrack (song, trackId);
    if (t == nullptr || scene < 0 || scene > 511)
        return 0;
    if (clip.id == 0)
        clip.id = takeId (song);
    clip.start = 0;
    normalise (clip);
    if (t->slots.size() <= static_cast<std::size_t> (scene))
        t->slots.resize (static_cast<std::size_t> (scene) + 1);
    const auto id = clip.id;
    t->slots[static_cast<std::size_t> (scene)] = std::make_shared<const Clip> (std::move (clip));
    return id;
}

void clearSlot (Song& song, TrackId trackId, int scene)
{
    auto* t = findTrack (song, trackId);
    if (t != nullptr && scene >= 0 && static_cast<std::size_t> (scene) < t->slots.size())
        t->slots[static_cast<std::size_t> (scene)] = nullptr;
}

int sceneOfClip (const Song& song, ClipId id)
{
    for (const auto& t : song.tracks)
        for (std::size_t i = 0; i < t.slots.size(); ++i)
            if (t.slots[i] != nullptr && t.slots[i]->id == id)
                return static_cast<int> (i);
    return -1;
}

void modifyClip (Song& song, ClipId id, const std::function<void (Clip&)>& fn)
{
    auto ref = findClip (song, id);
    if (ref.track == nullptr)
        return;
    auto copy = std::make_shared<Clip> (ref.clip());
    fn (*copy);
    normalise (*copy);
    if (ref.inSlot)
    {
        copy->start = 0;
        ref.track->slots[ref.index] = std::move (copy);
        return;
    }
    ref.track->clips[ref.index] = std::move (copy);
    sortClips (*ref.track);
}

void moveClip (Song& song, ClipId id, Tick newStart, TrackId newTrack)
{
    auto ref = findClip (song, id);
    auto* dest = findTrack (song, newTrack);
    if (ref.track == nullptr || dest == nullptr || ref.inSlot)
        return;

    auto copy = std::make_shared<Clip> (ref.clip());
    copy->start = std::max<Tick> (0, newStart);
    ref.track->clips.erase (ref.track->clips.begin() + static_cast<std::ptrdiff_t> (ref.index));
    dest->clips.push_back (std::move (copy));
    sortClips (*dest);
}

void clearRange (Song& song, TrackId track, Tick from, Tick to)
{
    std::vector<ClipId> ids;
    if (const auto* t = findTrack (song, track))
        for (const auto& c : t->clips)
            if (c->end() > from && c->start < to)
                ids.push_back (c->id);
    for (auto id : ids)
    {
        if (const auto right = splitClip (song, id, from); right != 0)
            id = right;
        splitClip (song, id, to);
        removeClip (song, id);
    }
}

ClipId splitClip (Song& song, ClipId id, Tick at)
{
    auto ref = findClip (song, id);
    if (ref.track == nullptr || ref.inSlot)
        return 0;
    const auto& original = ref.clip();
    if (at <= original.start || at >= original.end())
        return 0;

    const Tick cut = at - original.start;
    Clip left = original, right = original;
    left.length = cut;
    right.id = takeId (song);
    right.start = at;
    right.length = original.length - cut;
    left.notes.clear();
    right.notes.clear();
    left.controls.clear();
    right.controls.clear();

    for (auto n : original.notes)
    {
        if (n.start < cut)
        {
            n.length = std::min (n.length, cut - n.start);
            left.notes.push_back (n);
        }
        else
        {
            n.start -= cut;
            right.notes.push_back (n);
        }
    }
    for (auto c : original.controls)
    {
        if (c.at < cut)
            left.controls.push_back (c);
        else
        {
            c.at -= cut;
            right.controls.push_back (c);
        }
    }

    auto* track = ref.track;
    track->clips[ref.index] = std::make_shared<const Clip> (std::move (left));
    const auto rightId = right.id;
    track->clips.push_back (std::make_shared<const Clip> (std::move (right)));
    sortClips (*track);
    return rightId;
}

void joinClips (Song& song, const std::vector<ClipId>& ids)
{
    if (ids.size() < 2)
        return;
    const auto trackId = trackOfClip (song, ids.front());
    std::vector<ClipPtr> parts;
    for (auto id : ids)
        if (trackOfClip (song, id) == trackId)
            if (auto c = findClipPtr (song, id))
                parts.push_back (c);
    if (parts.size() < 2)
        return;

    std::sort (parts.begin(), parts.end(), [] (auto& a, auto& b) { return a->start < b->start; });
    Clip joined = *parts.front();
    Tick end = joined.end();
    for (std::size_t i = 1; i < parts.size(); ++i)
    {
        const auto& p = *parts[i];
        const Tick offset = p.start - joined.start;
        for (auto n : p.notes)
        {
            n.start += offset;
            joined.notes.push_back (n);
        }
        for (auto c : p.controls)
        {
            c.at += offset;
            joined.controls.push_back (c);
        }
        end = std::max (end, p.end());
        removeClip (song, p.id);
    }
    joined.length = end - joined.start;
    removeClip (song, joined.id);
    addClip (song, trackId, std::move (joined));
}

void normalise (Clip& clip)
{
    clip.length = std::max<Tick> (1, clip.length);
    std::erase_if (clip.notes, [] (const Note& n) { return n.length <= 0; });
    for (auto& n : clip.notes)
    {
        n.pitch = std::min<std::uint8_t> (n.pitch, 127);
        n.velocity = std::clamp<std::uint8_t> (n.velocity, 1, 127);
    }
    std::stable_sort (clip.notes.begin(), clip.notes.end(),
                      [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
    std::stable_sort (clip.controls.begin(), clip.controls.end(), [] (auto& a, auto& b) { return a.at < b.at; });
}

void removeOverlaps (Clip& clip)
{
    normalise (clip);
    std::array<int, 128> lastIndex;
    lastIndex.fill (-1);
    std::vector<Note> out;
    for (const auto& n : clip.notes)
    {
        auto& last = lastIndex[n.pitch];
        if (last >= 0 && out[static_cast<std::size_t> (last)].end() > n.start)
        {
            auto& prev = out[static_cast<std::size_t> (last)];
            if (prev.start == n.start)
            {
                prev.length = std::max (prev.length, n.length); // exact duplicate: keep one
                continue;
            }
            prev.length = n.start - prev.start; // cut the earlier note where the next one starts
        }
        last = static_cast<int> (out.size());
        out.push_back (n);
    }
    clip.notes = std::move (out);
}

NoteKey addTimelineNote (Song& song, TrackId trackId, Tick at, Tick length, int pitch, int velocity)
{
    auto* track = findTrack (song, trackId);
    if (track == nullptr || length <= 0)
        return {};
    at = std::max<Tick> (0, at);
    Note note;
    note.pitch = static_cast<std::uint8_t> (std::clamp (pitch, 0, 127));
    note.velocity = static_cast<std::uint8_t> (std::clamp (velocity, 1, 127));

    ClipId target = 0;
    Tick nextStart = -1;
    for (const auto& c : track->clips)
    {
        const Tick round = c->loopLength > 0 && c->loopLength < c->length ? c->loopLength : c->length;
        if (at >= c->start && at < c->start + round)
            target = c->id;
        if (c->start > at && (nextStart < 0 || c->start < nextStart))
            nextStart = c->start;
    }
    if (target == 0)
    {
        // No clip here (or only the repeats of a loop): make one from the bar line.
        const Tick bar = song.timeSignature.ticksPerBar();
        Tick start = (at / bar) * bar;
        for (const auto& c : track->clips)
            if (c->end() > start && c->end() <= at)
                start = c->end();
        for (const auto& c : track->clips)
            if (c->start <= at && c->end() > at)
                return {}; // inside the repeats of a looped clip: there is no room for a new clip
        Tick end = ((at + length + bar - 1) / bar) * bar;
        if (nextStart >= 0)
            end = std::min (end, nextStart);
        Clip clip;
        clip.name = track->name;
        clip.start = start;
        clip.length = std::max<Tick> (1, end - start);
        target = addClip (song, trackId, std::move (clip));
    }
    modifyClip (song, target, [&] (Clip& clip) {
        const Tick round = clip.loopLength > 0 && clip.loopLength < clip.length ? clip.loopLength : clip.length;
        note.start = at - clip.start;
        note.length = std::max<Tick> (1, std::min (length, round - note.start));
        clip.notes.push_back (note);
        normalise (clip);
    });
    return { target, note };
}

std::vector<NoteKey> changeNotes (Song& song, const std::vector<NoteKey>& keys, const std::function<void (Note&)>& fn)
{
    std::vector<NoteKey> out;
    std::vector<ClipId> clips;
    for (const auto& k : keys)
        if (std::find (clips.begin(), clips.end(), k.clip) == clips.end())
            clips.push_back (k.clip);
    for (const ClipId id : clips)
    {
        if (findClipPtr (song, id) == nullptr)
            continue;
        modifyClip (song, id, [&] (Clip& clip) {
            std::vector<bool> used (clip.notes.size(), false);
            for (const auto& k : keys)
            {
                if (k.clip != id)
                    continue;
                for (std::size_t i = 0; i < clip.notes.size(); ++i)
                {
                    if (used[i] || ! (clip.notes[i] == k.note))
                        continue;
                    used[i] = true;
                    auto& n = clip.notes[i];
                    fn (n);
                    n.start = std::clamp<Tick> (n.start, 0, std::max<Tick> (0, clip.length - 1));
                    n.length = std::max<Tick> (1, n.length);
                    n.pitch = std::min<std::uint8_t> (n.pitch, 127);
                    n.velocity = std::clamp<std::uint8_t> (n.velocity, 1, 127);
                    out.push_back ({ id, n });
                    break;
                }
            }
            normalise (clip);
        });
    }
    return out;
}

void removeNotes (Song& song, const std::vector<NoteKey>& keys)
{
    std::vector<ClipId> clips;
    for (const auto& k : keys)
        if (std::find (clips.begin(), clips.end(), k.clip) == clips.end())
            clips.push_back (k.clip);
    for (const ClipId id : clips)
    {
        if (findClipPtr (song, id) == nullptr)
            continue;
        modifyClip (song, id, [&] (Clip& clip) {
            for (const auto& k : keys)
                if (k.clip == id)
                    if (const auto it = std::find (clip.notes.begin(), clip.notes.end(), k.note); it != clip.notes.end())
                        clip.notes.erase (it);
        });
    }
}

void setMark (Song& song, TrackId trackId, ScoreMark mark)
{
    auto* track = findTrack (song, trackId);
    if (track == nullptr)
        return;
    mark.at = std::max<Tick> (0, mark.at);
    auto& marks = track->marks;
    if (mark.kind == ScoreMark::Kind::articulation)
    {
        const auto same = std::find (marks.begin(), marks.end(), mark);
        if (same != marks.end())
        {
            marks.erase (same);
            return;
        }
    }
    else
        std::erase_if (marks, [&] (const ScoreMark& m) { return m.kind == mark.kind && m.at == mark.at; });
    if (mark.text.empty())
        return;
    marks.push_back (std::move (mark));
    std::stable_sort (marks.begin(), marks.end(), [] (const ScoreMark& a, const ScoreMark& b) { return a.at < b.at; });
}

void clearMarks (Song& song, TrackId trackId, Tick from, Tick to)
{
    if (auto* track = findTrack (song, trackId))
        std::erase_if (track->marks, [&] (const ScoreMark& m) { return m.at >= from && m.at < to; });
}

int setLyrics (Song& song, TrackId trackId, Tick from, const std::string& text, const std::vector<Tick>& noteStarts)
{
    // Words; a hyphen ends a part of a word and stays with it ("Twin-" "kle").
    std::vector<std::string> words;
    std::string word;
    const auto flush = [&] {
        if (! word.empty())
            words.push_back (word);
        word.clear();
    };
    for (const char c : text)
    {
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r')
            flush();
        else
        {
            word += c;
            if (c == '-' && word.size() > 1)
                flush();
        }
    }
    flush();

    std::vector<Tick> starts;
    for (const Tick t : noteStarts)
        if (t >= from && (starts.empty() || starts.back() != t))
            starts.push_back (t);
    std::sort (starts.begin(), starts.end());
    starts.erase (std::unique (starts.begin(), starts.end()), starts.end());

    int placed = 0;
    for (std::size_t i = 0; i < words.size() && i < starts.size(); ++i)
    {
        setMark (song, trackId, { ScoreMark::Kind::lyric, starts[i], words[i] });
        ++placed;
    }
    return placed;
}

void forEachClip (Song& song, const std::function<void (Clip&)>& fn)
{
    for (auto& t : song.tracks)
        for (auto& c : t.clips)
        {
            auto copy = std::make_shared<Clip> (*c);
            fn (*copy);
            normalise (*copy);
            c = std::move (copy);
        }
}

} // namespace arranger::edit

#include "arranger/core/Clipboard.h"

#include <algorithm>
#include <limits>

namespace arranger::clipboard
{

namespace
{
    bool isAudioTrack (const Track& t) noexcept { return t.output.kind == TrackOutput::Kind::audio; }

    /** The clip of the track whose own notes (not the repeats of a loop) are at `at`, or 0. */
    ClipId clipAt (const Track& track, Tick at)
    {
        for (const auto& c : track.clips)
            if (at >= c->start && at < c->end())
                return c->id;
        return 0;
    }

    Tick nextClipStart (const Track& track, Tick after)
    {
        Tick next = std::numeric_limits<Tick>::max();
        for (const auto& c : track.clips)
            if (c->start > after)
                next = std::min (next, c->start);
        return next;
    }
}

Content copyClips (const Song& song, const std::vector<ClipId>& ids)
{
    Content out;
    struct Found { int track; ClipPtr midi; AudioClipPtr audio; Tick start, end; };
    std::vector<Found> found;
    for (int ti = 0; ti < static_cast<int> (song.tracks.size()); ++ti)
    {
        const auto& t = song.tracks[static_cast<std::size_t> (ti)];
        for (const auto& c : t.clips)
            if (std::find (ids.begin(), ids.end(), c->id) != ids.end())
                found.push_back ({ ti, c, nullptr, c->start, c->end() });
        for (const auto& a : t.audio)
            if (std::find (ids.begin(), ids.end(), a->id) != ids.end())
                found.push_back ({ ti, nullptr, a, a->start, a->end() });
    }
    if (found.empty())
        return out;

    int firstTrack = found.front().track;
    Tick first = found.front().start, last = found.front().end;
    for (const auto& f : found)
    {
        firstTrack = std::min (firstTrack, f.track);
        first = std::min (first, f.start);
        last = std::max (last, f.end);
    }
    out.kind = Content::Kind::clips;
    out.span = last - first;
    for (const auto& f : found)
    {
        Content::ClipItem item;
        item.trackOffset = f.track - firstTrack;
        if (f.midi != nullptr)
        {
            auto c = std::make_shared<Clip> (*f.midi);
            c->id = 0;
            c->start -= first;
            item.midi = std::move (c);
        }
        else
        {
            auto a = std::make_shared<AudioClip> (*f.audio);
            a->id = 0;
            a->start -= first;
            item.audio = std::move (a);
        }
        out.clips.push_back (std::move (item));
    }
    return out;
}

Content copySlot (const Song& song, TrackId trackId, int scene)
{
    Content out;
    const auto* t = edit::findTrack (song, trackId);
    if (t == nullptr || scene < 0)
        return out;
    const auto i = static_cast<std::size_t> (scene);
    Content::ClipItem item;
    if (i < t->slots.size() && t->slots[i] != nullptr)
    {
        auto c = std::make_shared<Clip> (*t->slots[i]);
        c->id = 0;
        c->start = 0;
        out.span = c->length;
        item.midi = std::move (c);
    }
    else if (i < t->audioSlots.size() && t->audioSlots[i] != nullptr)
    {
        auto a = std::make_shared<AudioClip> (*t->audioSlots[i]);
        a->id = 0;
        a->start = 0;
        out.span = a->length;
        item.audio = std::move (a);
    }
    else
        return out;
    out.kind = Content::Kind::clips;
    out.clips.push_back (std::move (item));
    return out;
}

Content copyNotes (const Song& song, const std::vector<edit::NoteKey>& keys)
{
    Content out;
    for (const auto& k : keys)
    {
        auto clip = edit::findClipPtr (song, k.clip);
        if (clip == nullptr)
            continue;
        auto n = k.note;
        n.start += clip->start;
        out.notes.push_back (n);
    }
    if (out.notes.empty())
        return out;
    Tick first = out.notes.front().start, last = 0;
    for (const auto& n : out.notes)
        first = std::min (first, n.start);
    for (auto& n : out.notes)
    {
        n.start -= first;
        last = std::max (last, n.end());
    }
    std::sort (out.notes.begin(), out.notes.end(), [] (const Note& a, const Note& b) {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
    });
    out.kind = Content::Kind::notes;
    out.span = last;
    return out;
}

Content copyPoints (const AutomationLine& line, const std::vector<int>& indexes)
{
    Content out;
    for (int i : indexes)
        if (i >= 0 && i < static_cast<int> (line.size()))
            out.points.push_back (line[static_cast<std::size_t> (i)]);
    if (out.points.empty())
        return out;
    std::sort (out.points.begin(), out.points.end(), [] (const AutomationPoint& a, const AutomationPoint& b) { return a.at < b.at; });
    const Tick first = out.points.front().at;
    for (auto& p : out.points)
        p.at -= first;
    out.kind = Content::Kind::points;
    out.span = out.points.back().at;
    return out;
}

std::vector<ClipId> pasteClips (Song& song, const Content& content, TrackId trackId, Tick at)
{
    std::vector<ClipId> ids;
    const int base = edit::trackIndex (song, trackId);
    if (content.kind != Content::Kind::clips || base < 0)
        return ids;
    at = std::max<Tick> (0, at);
    for (const auto& item : content.clips)
    {
        const auto ti = static_cast<std::size_t> (base + item.trackOffset);
        if (ti >= song.tracks.size())
            continue;
        auto& t = song.tracks[ti];
        if (item.midi != nullptr && ! isAudioTrack (t))
        {
            auto c = *item.midi;
            c.start += at;
            ids.push_back (edit::addClip (song, t.id, std::move (c)));
        }
        else if (item.audio != nullptr && isAudioTrack (t))
        {
            auto a = *item.audio;
            a.start += at;
            a.layer = edit::layerAbove (t, a.start, a.end());
            ids.push_back (edit::addAudioClip (song, t.id, std::move (a)));
        }
    }
    return ids;
}

std::vector<edit::NoteKey> pasteNotesInClip (Song& song, const Content& content, ClipId clipId, Tick at)
{
    std::vector<edit::NoteKey> added;
    auto clip = edit::findClipPtr (song, clipId);
    if (content.kind != Content::Kind::notes || clip == nullptr)
        return added;
    at = std::max<Tick> (0, at);
    std::vector<Note> notes;
    for (auto n : content.notes)
    {
        n.start += at;
        if (n.start >= clip->length)
            continue;
        n.length = std::min (n.length, clip->length - n.start);
        notes.push_back (n);
    }
    if (notes.empty())
        return added;
    edit::modifyClip (song, clipId, [&] (Clip& c) {
        for (const auto& n : notes)
        {
            // A note of the same pitch at the same time is replaced, not doubled.
            std::erase_if (c.notes, [&] (const Note& o) { return o.start == n.start && o.pitch == n.pitch; });
            c.notes.push_back (n);
        }
        edit::normalise (c);
    });
    for (const auto& n : notes)
        added.push_back ({ clipId, n });
    return added;
}

std::vector<edit::NoteKey> pasteNotes (Song& song, const Content& content, TrackId trackId, Tick at)
{
    auto* track = edit::findTrack (song, trackId);
    if (content.kind != Content::Kind::notes || track == nullptr || isAudioTrack (*track))
        return {};
    at = std::max<Tick> (0, at);
    const Tick bar = song.timeSignature.ticksPerBar();
    const Tick wantEnd = ((at + std::max<Tick> (1, content.span) + bar - 1) / bar) * bar;

    ClipId target = clipAt (*track, at);
    if (target == 0)
    {
        Tick start = (at / bar) * bar;
        for (const auto& c : track->clips)
            if (c->end() > start && c->end() <= at)
                start = c->end();
        Clip clip;
        clip.name = track->name;
        clip.start = start;
        clip.length = std::max<Tick> (1, std::min (wantEnd, nextClipStart (*track, at)) - start);
        target = edit::addClip (song, trackId, std::move (clip));
    }
    else
    {
        // Grow the clip to hold the notes, up to the next clip. A looped clip keeps its length.
        auto clip = edit::findClipPtr (song, target);
        if (clip->loopLength == 0 && clip->end() < at + content.span)
        {
            const Tick end = std::min (wantEnd, nextClipStart (*track, clip->start));
            if (end > clip->end())
                edit::modifyClip (song, target, [&] (Clip& c) { c.length = end - c.start; });
        }
    }
    auto clip = edit::findClipPtr (song, target);
    return pasteNotesInClip (song, content, target, at - clip->start);
}

ClipId pasteSlot (Song& song, const Content& content, TrackId trackId, int scene)
{
    const auto* t = edit::findTrack (song, trackId);
    if (t == nullptr || scene < 0)
        return 0;
    const bool audio = isAudioTrack (*t);
    if (content.kind == Content::Kind::clips)
    {
        for (const auto& item : content.clips)
        {
            if (audio && item.audio != nullptr)
            {
                auto a = *item.audio;
                a.start = 0;
                a.layer = 0;
                return edit::setAudioSlot (song, trackId, scene, std::move (a));
            }
            if (! audio && item.midi != nullptr)
            {
                auto c = *item.midi;
                c.start = 0;
                return edit::setSlot (song, trackId, scene, std::move (c));
            }
        }
        return 0;
    }
    if (content.kind == Content::Kind::notes && ! audio)
    {
        const Tick bar = song.timeSignature.ticksPerBar();
        Clip c;
        c.name = t->name;
        c.notes = content.notes;
        c.length = std::max<Tick> (1, (content.span + bar - 1) / bar) * bar;
        edit::normalise (c);
        return edit::setSlot (song, trackId, scene, std::move (c));
    }
    return 0;
}

std::vector<Tick> pastePoints (Song& song, const Content& content, TrackId trackId, int lane, Tick at)
{
    std::vector<Tick> times;
    if (content.kind != Content::Kind::points)
        return times;
    at = std::max<Tick> (0, at);
    for (auto p : content.points)
    {
        p.at += at;
        if (edit::setAutomationPoint (song, trackId, lane, p) >= 0)
            times.push_back (p.at);
    }
    return times;
}

} // namespace arranger::clipboard

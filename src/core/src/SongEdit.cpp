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

/** Materialises what a clip plays, including partial final repetitions. */
Clip playedClip (const Clip& source)
{
    Clip out = source;
    out.loopLength = 0;
    out.notes.clear();
    out.controls.clear();
    const Tick cycle = source.loopLength > 0 && source.loopLength < source.length ? source.loopLength : source.length;
    for (Tick base = 0; cycle > 0 && base < source.length; base += cycle)
    {
        const Tick end = std::min (base + cycle, source.length);
        for (auto n : source.notes)
            if (n.start < cycle && base + n.start < end)
            {
                n.start += base;
                n.length = std::min (n.length, end - n.start);
                out.notes.push_back (n);
            }
        for (auto c : source.controls)
            if (c.at < cycle && base + c.at < end)
            {
                c.at += base;
                out.controls.push_back (c);
            }
    }
    return out;
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

void setSolo (Song& song, TrackId id, bool on, bool exclusive)
{
    if (findTrack (song, id) == nullptr)
        return;
    for (auto& t : song.tracks)
        if (t.id == id)
            t.mixer.solo = on;
        else if (on && exclusive)
            t.mixer.solo = false;
}

void toggleSolo (Song& song, TrackId id, bool exclusive)
{
    if (const auto* t = findTrack (song, id))
        setSolo (song, id, ! t->mixer.solo, exclusive);
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

namespace
{
    void sortAudio (Track& t)
    {
        std::stable_sort (t.audio.begin(), t.audio.end(), [] (const AudioClipPtr& a, const AudioClipPtr& b) { return a->start < b->start; });
    }
}

double audioSpeed (const Song& song, const AudioClip& clip)
{
    if (clip.bpm <= 0.0)
        return 1.0;
    // One speed for the whole clip: the song tempo where the clip starts.
    const double speed = song.tempo.tempoAt (static_cast<double> (clip.start)) / clip.bpm;
    return std::abs (speed - 1.0) < 0.002 ? 1.0 : std::clamp (speed, 0.25, 4.0);
}

AudioClipPtr findAudioClip (const Song& song, ClipId id)
{
    for (const auto& t : song.tracks)
        for (const auto& c : t.audio)
            if (c->id == id)
                return c;
    for (const auto& t : song.tracks)
        for (const auto& c : t.audioSlots)
            if (c != nullptr && c->id == id)
                return c;
    return nullptr;
}

namespace
{
    /** Bottom first: the lower layer, then the earlier start. */
    bool below (const AudioClip& a, const AudioClip& b) noexcept
    {
        return a.layer != b.layer ? a.layer < b.layer : (a.start != b.start ? a.start < b.start : a.id < b.id);
    }
}

std::vector<AudioClipPtr> audioDrawOrder (const Track& t)
{
    auto order = t.audio;
    std::stable_sort (order.begin(), order.end(), [] (const AudioClipPtr& a, const AudioClipPtr& b) { return below (*a, *b); });
    return order;
}

std::vector<AudioPiece> audioPieces (const Track& t)
{
    const auto order = audioDrawOrder (t);
    std::vector<AudioPiece> out;
    for (std::size_t i = 0; i < order.size(); ++i)
    {
        const auto& c = *order[i];
        if (c.length <= 0)
            continue;
        // The clip's own fades; at an edge that lies over a lower clip, at least a short crossfade.
        Tick fadeIn = std::clamp<Tick> (c.fadeIn, 0, c.length), fadeOut = std::clamp<Tick> (c.fadeOut, 0, c.length);
        for (std::size_t j = 0; j < i; ++j)
        {
            const auto& lower = *order[j];
            if (lower.start < c.start && lower.end() > c.start)
                fadeIn = std::max (fadeIn, std::min (kAudioCrossfade, c.length));
            if (lower.start < c.end() && lower.end() > c.end())
                fadeOut = std::max (fadeOut, std::min (kAudioCrossfade, c.length));
        }
        std::vector<AudioPiece> pieces { { order[i], c.start, c.end(), fadeIn, fadeOut } };
        // Each clip above hides this one, except during its own fades: there the two cross.
        for (std::size_t j = i + 1; j < order.size(); ++j)
        {
            const auto& up = *order[j];
            if (up.start >= c.end() || up.end() <= c.start || up.length <= 0)
                continue;
            const bool overStart = c.start < up.start, overEnd = c.end() > up.end();
            const Tick in = overStart ? std::max (std::clamp<Tick> (up.fadeIn, 0, up.length), std::min (kAudioCrossfade, up.length)) : 0;
            const Tick outFade = overEnd ? std::max (std::clamp<Tick> (up.fadeOut, 0, up.length), std::min (kAudioCrossfade, up.length)) : 0;
            const Tick hideStart = up.start + in, hideEnd = up.end() - outFade;
            if (hideStart >= hideEnd)
                continue; // a very short clip on top: both play
            std::vector<AudioPiece> next;
            for (const auto& p : pieces)
            {
                if (hideEnd <= p.start || hideStart >= p.end)
                {
                    next.push_back (p);
                    continue;
                }
                if (hideStart > p.start)
                {
                    auto left = p;
                    left.end = hideStart;
                    left.fadeOut = in;
                    left.fadeIn = std::min (left.fadeIn, left.end - left.start);
                    left.fadeOut = std::min (left.fadeOut, left.end - left.start);
                    next.push_back (left);
                }
                if (hideEnd < p.end)
                {
                    auto right = p;
                    right.start = hideEnd;
                    right.fadeIn = outFade;
                    right.fadeIn = std::min (right.fadeIn, right.end - right.start);
                    right.fadeOut = std::min (right.fadeOut, right.end - right.start);
                    next.push_back (right);
                }
            }
            pieces = std::move (next);
        }
        for (auto& p : pieces)
            if (p.end > p.start)
                out.push_back (std::move (p));
    }
    std::stable_sort (out.begin(), out.end(), [] (const AudioPiece& a, const AudioPiece& b) { return a.start < b.start; });
    return out;
}

int layerAbove (const Track& t, Tick start, Tick end) noexcept
{
    int top = -1;
    for (const auto& c : t.audio)
        if (c->start < end && c->end() > start)
            top = std::max (top, c->layer);
    return top + 1;
}

void bringToFront (Song& song, ClipId id)
{
    const auto clip = findAudioClip (song, id);
    const auto* t = findTrack (song, trackOfClip (song, id));
    if (clip == nullptr || t == nullptr)
        return;
    int top = clip->layer;
    for (const auto& c : t->audio)
        if (c->id != id && c->start < clip->end() && c->end() > clip->start && ! below (*c, *clip))
            top = std::max (top, c->layer + 1);
    if (top != clip->layer)
        modifyAudioClip (song, id, [top] (AudioClip& c) { c.layer = top; });
}

std::vector<AudioClipPtr> audioClipsAt (const Track& t, Tick at)
{
    std::vector<AudioClipPtr> out;
    for (const auto& c : audioDrawOrder (t))
        if (c->start <= at && c->end() > at)
            out.insert (out.begin(), c);
    return out;
}

ClipId addAudioClip (Song& song, TrackId trackId, AudioClip clip)
{
    auto* t = findTrack (song, trackId);
    if (t == nullptr)
        return 0;
    if (clip.id == 0)
        clip.id = takeId (song);
    clip.start = std::max<Tick> (0, clip.start);
    clip.length = std::max<Tick> (1, clip.length);
    const auto id = clip.id;
    t->audio.push_back (std::make_shared<const AudioClip> (std::move (clip)));
    sortAudio (*t);
    return id;
}

namespace
{
    /** Keeps the values of an audio clip in their ranges. */
    void normaliseAudio (AudioClip& c)
    {
        c.start = std::max<Tick> (0, c.start);
        c.length = std::max<Tick> (1, c.length);
        c.offset = std::max (0.0, c.offset);
        c.fadeIn = std::clamp<Tick> (c.fadeIn, 0, c.length);
        c.fadeOut = std::clamp<Tick> (c.fadeOut, 0, c.length - c.fadeIn);
        c.gainDb = std::clamp (c.gainDb, -60.0f, 24.0f);
        if (! (c.bpm >= 20.0 && c.bpm <= 400.0))
            c.bpm = 0.0;
    }
}

void modifyAudioClip (Song& song, ClipId id, const std::function<void (AudioClip&)>& fn)
{
    for (auto& t : song.tracks)
    {
        for (auto& c : t.audio)
            if (c->id == id)
            {
                auto copy = std::make_shared<AudioClip> (*c);
                fn (*copy);
                copy->id = id;
                normaliseAudio (*copy);
                c = std::move (copy);
                sortAudio (t);
                return;
            }
        for (auto& c : t.audioSlots)
            if (c != nullptr && c->id == id)
            {
                auto copy = std::make_shared<AudioClip> (*c);
                fn (*copy);
                copy->id = id;
                copy->start = 0; // a slot clip has no place on the Timeline
                normaliseAudio (*copy);
                c = std::move (copy);
                return;
            }
    }
}

ClipId setAudioSlot (Song& song, TrackId trackId, int scene, AudioClip clip)
{
    auto* t = findTrack (song, trackId);
    if (t == nullptr || t->output.kind != TrackOutput::Kind::audio || scene < 0 || scene > 511)
        return 0;
    if (clip.id == 0)
        clip.id = takeId (song);
    clip.start = 0;
    normaliseAudio (clip);
    if (t->audioSlots.size() <= static_cast<std::size_t> (scene))
        t->audioSlots.resize (static_cast<std::size_t> (scene) + 1);
    const auto id = clip.id;
    t->audioSlots[static_cast<std::size_t> (scene)] = std::make_shared<const AudioClip> (std::move (clip));
    return id;
}

Track& addAudioTrack (Song& song, const std::string& name, int insertAfter)
{
    auto& t = addTrack (song, name, insertAfter);
    t.output.kind = TrackOutput::Kind::audio;
    t.sound = {};
    return t;
}

TrackId trackOfClip (const Song& song, ClipId id)
{
    for (const auto& t : song.tracks)
        for (const auto& c : t.audio)
            if (c->id == id)
                return t.id;
    for (const auto& t : song.tracks)
        for (const auto& c : t.clips)
            if (c->id == id)
                return t.id;
    for (const auto& t : song.tracks)
        for (const auto& c : t.slots)
            if (c != nullptr && c->id == id)
                return t.id;
    for (const auto& t : song.tracks)
        for (const auto& c : t.audioSlots)
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

bool makesSound (const Track& t) noexcept
{
    using K = TrackOutput::Kind;
    return t.output.kind == K::soundFont || t.output.kind == K::sampler || t.output.kind == K::plugin || t.output.kind == K::audio;
}

Bus& addBus (Song& song, const std::string& name)
{
    static constexpr const char* kColours[] = { "blue", "violet", "green", "amber", "teal", "pink", "coral" };
    Bus b;
    b.id = takeId (song);
    b.name = name;
    b.colour = kColours[song.buses.size() % 7];
    song.buses.push_back (std::move (b));
    return song.buses.back();
}

void removeBus (Song& song, std::uint32_t id)
{
    std::erase_if (song.buses, [id] (const Bus& b) { return b.id == id; });
    for (auto& t : song.tracks)
    {
        if (t.bus == id)
            t.bus = 0;
        std::erase_if (t.sends, [id] (const BusSend& s) { return s.bus == id; });
    }
}

Bus* findBus (Song& song, std::uint32_t id) noexcept
{
    for (auto& b : song.buses)
        if (b.id == id)
            return &b;
    return nullptr;
}

const Bus* findBus (const Song& song, std::uint32_t id) noexcept
{
    for (const auto& b : song.buses)
        if (b.id == id)
            return &b;
    return nullptr;
}

void setTrackBus (Song& song, TrackId id, std::uint32_t bus)
{
    auto* t = findTrack (song, id);
    if (t == nullptr || ! makesSound (*t) || (bus != 0 && findBus (song, bus) == nullptr))
        return;
    t->bus = bus;
}

int sendLevel (const Track& t, std::uint32_t bus) noexcept
{
    for (const auto& s : t.sends)
        if (s.bus == bus)
            return s.level;
    return 0;
}

void setSend (Song& song, TrackId id, std::uint32_t bus, int level)
{
    auto* t = findTrack (song, id);
    if (t == nullptr || ! makesSound (*t) || findBus (song, bus) == nullptr)
        return;
    level = std::clamp (level, 0, 127);
    for (auto it = t->sends.begin(); it != t->sends.end(); ++it)
        if (it->bus == bus)
        {
            if (level == 0)
                t->sends.erase (it);
            else
                it->level = level;
            return;
        }
    if (level > 0)
        t->sends.push_back ({ bus, level });
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
        std::erase_if (t.audio, [id] (const AudioClipPtr& c) { return c->id == id; });
        for (auto& slot : t.slots)
            if (slot != nullptr && slot->id == id)
                slot = nullptr;
        for (auto& slot : t.audioSlots)
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

int slotsFromTimeline (Song& song, Tick sceneLength)
{
    if (sceneLength <= 0)
        return 0;
    const Tick end = song.contentEnd();
    const int scenes = static_cast<int> (std::min<Tick> (128, (end + sceneLength - 1) / sceneLength));
    if (scenes <= 0)
        return 0;

    const Tick bar = std::max<Tick> (1, song.timeSignature.ticksPerBar());
    if (song.scenes.size() < static_cast<std::size_t> (scenes))
        song.scenes.resize (static_cast<std::size_t> (scenes));
    for (int k = 0; k < scenes; ++k)
    {
        const Tick from = k * sceneLength, to = from + sceneLength;
        song.scenes[static_cast<std::size_t> (k)] = "Bars " + std::to_string (from / bar + 1) + "-" + std::to_string ((to - 1) / bar + 1);
    }

    for (auto& track : song.tracks)
    {
        if (track.output.kind == TrackOutput::Kind::audio)
            continue;
        std::vector<Clip> parts (static_cast<std::size_t> (scenes));
        for (const auto& clip : track.clips)
        {
            if (clip == nullptr)
                continue;
            // A looping clip repeats its first `loopLength` ticks to its end.
            const bool looping = clip->loopLength > 0 && clip->loopLength < clip->length;
            const Tick cycle = looping ? clip->loopLength : clip->length;
            for (Tick base = 0; cycle > 0 && base < clip->length; base += cycle)
            {
                const Tick cycleEnd = std::min (base + cycle, clip->length);
                for (const auto& n : clip->notes)
                {
                    if (n.start >= cycle || base + n.start >= clip->length)
                        continue;
                    const Tick on = clip->start + base + n.start;
                    const Tick off = clip->start + std::min (base + n.end(), cycleEnd);
                    const auto k = on / sceneLength;
                    if (on < 0 || k >= scenes)
                        continue;
                    const Tick sceneStart = k * sceneLength;
                    Note note = n;
                    note.start = on - sceneStart;
                    note.length = std::max<Tick> (1, std::min (off, sceneStart + sceneLength) - on);
                    parts[static_cast<std::size_t> (k)].notes.push_back (note);
                }
                for (const auto& c : clip->controls)
                {
                    if (c.at >= cycle || base + c.at >= clip->length)
                        continue;
                    const Tick at = clip->start + base + c.at;
                    const auto k = at / sceneLength;
                    if (at < 0 || k >= scenes)
                        continue;
                    ControlEvent e = c;
                    e.at = at - k * sceneLength;
                    parts[static_cast<std::size_t> (k)].controls.push_back (e);
                }
            }
        }
        for (int k = 0; k < scenes; ++k)
        {
            auto& part = parts[static_cast<std::size_t> (k)];
            if (part.notes.empty())
                continue;
            part.name = track.name;
            part.length = sceneLength;
            setSlot (song, track.id, k, std::move (part));
        }
    }
    return scenes;
}

void clearSlot (Song& song, TrackId trackId, int scene)
{
    auto* t = findTrack (song, trackId);
    if (t != nullptr && scene >= 0 && static_cast<std::size_t> (scene) < t->slots.size())
        t->slots[static_cast<std::size_t> (scene)] = nullptr;
    if (t != nullptr && scene >= 0 && static_cast<std::size_t> (scene) < t->audioSlots.size())
        t->audioSlots[static_cast<std::size_t> (scene)] = nullptr;
}

int sceneOfClip (const Song& song, ClipId id)
{
    for (const auto& t : song.tracks)
        for (std::size_t i = 0; i < t.slots.size(); ++i)
            if (t.slots[i] != nullptr && t.slots[i]->id == id)
                return static_cast<int> (i);
    for (const auto& t : song.tracks)
        for (std::size_t i = 0; i < t.audioSlots.size(); ++i)
            if (t.audioSlots[i] != nullptr && t.audioSlots[i]->id == id)
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
    if (const auto audio = findAudioClip (song, id))
    {
        // An audio clip stays on audio tracks: a move to another kind of track changes only its time.
        auto* from = findTrack (song, trackOfClip (song, id));
        auto* to = findTrack (song, newTrack);
        if (from == nullptr)
            return;
        if (to == nullptr || to->output.kind != TrackOutput::Kind::audio)
            to = from;
        auto copy = std::make_shared<AudioClip> (*audio);
        copy->start = std::max<Tick> (0, newStart);
        std::erase_if (from->audio, [id] (const AudioClipPtr& c) { return c->id == id; });
        to->audio.push_back (std::move (copy));
        sortAudio (*to);
        return;
    }
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
    if (const auto audio = findAudioClip (song, id))
    {
        if (at <= audio->start || at >= audio->end())
            return 0;
        const TrackId track = trackOfClip (song, id);
        AudioClip right = *audio;
        right.id = 0;
        right.start = at;
        right.length = audio->end() - at;
        // The right part starts later in the file, by the time between the two starts.
        right.offset = audio->offset + (song.tempo.secondsAt (static_cast<double> (at)) - song.tempo.secondsAt (static_cast<double> (audio->start))) * audioSpeed (song, *audio);
        right.fadeIn = 0;
        modifyAudioClip (song, id, [&] (AudioClip& c) {
            c.length = at - c.start;
            c.fadeOut = 0;
        });
        return addAudioClip (song, track, std::move (right));
    }
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

    if (original.loopLength > 0 && original.loopLength < original.length)
    {
        // A pattern that repeats: the left part keeps it; the right part keeps it when the cut is on a
        // repeat, else it gets the notes written out from the cut on.
        const Tick loop = original.loopLength;
        if (cut < loop)
            left.loopLength = 0;
        for (auto n : original.notes)
            if (n.start < std::min (cut, loop))
            {
                if (cut < loop)
                    n.length = std::min (n.length, cut - n.start);
                left.notes.push_back (n);
            }
        if (cut % loop == 0)
        {
            right.notes = original.notes;
        }
        else
        {
            right.loopLength = 0;
            for (Tick base = (cut / loop) * loop; base < original.length; base += loop)
                for (auto n : original.notes)
                {
                    const Tick when = base + n.start;
                    if (n.start >= loop || when < cut || when >= original.length)
                        continue;
                    n.start = when - cut;
                    n.length = std::min (n.length, original.length - when);
                    right.notes.push_back (n);
                }
            std::sort (right.notes.begin(), right.notes.end(), [] (const Note& x, const Note& y) { return x.start != y.start ? x.start < y.start : x.pitch < y.pitch; });
        }
        if (right.loopLength >= right.length)
            right.loopLength = 0;
        if (left.loopLength >= left.length)
            left.loopLength = 0;
    }
    else
    {
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
    }
    const bool looping = original.loopLength > 0 && original.loopLength < original.length;
    if (looping)
    {
        for (const auto& c : original.controls)
            if (c.at < std::min (cut, original.loopLength))
                left.controls.push_back (c);
        if (cut % original.loopLength == 0)
            right.controls = original.controls;
        else
        {
            for (auto c : playedClip (original).controls)
                if (c.at >= cut)
                {
                    c.at -= cut;
                    right.controls.push_back (c);
                }
        }
    }
    else
        for (auto c : original.controls)
            if (c.at < cut)
                left.controls.push_back (c);
            else
            {
                c.at -= cut;
                right.controls.push_back (c);
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
    Clip joined = playedClip (*parts.front());
    Tick end = joined.end();
    for (std::size_t i = 1; i < parts.size(); ++i)
    {
        const auto p = playedClip (*parts[i]);
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

int setAutomationPoint (Song& song, TrackId id, int lane, AutomationPoint point)
{
    auto* t = findTrack (song, id);
    auto* found = t != nullptr ? t->findLine (lane) : nullptr;
    if (found == nullptr)
        return -1;
    auto& line = *found;
    point.at = std::max<Tick> (0, point.at);
    point.value = std::clamp (point.value, 0.0f, 127.0f);
    auto it = std::lower_bound (line.begin(), line.end(), point.at, [] (const AutomationPoint& p, Tick at) { return p.at < at; });
    if (it != line.end() && it->at == point.at)
        *it = point;
    else
        it = line.insert (it, point);
    return static_cast<int> (it - line.begin());
}

void removeAutomationPoint (Song& song, TrackId id, int lane, int index)
{
    auto* t = findTrack (song, id);
    if (auto* line = t != nullptr ? t->findLine (lane) : nullptr)
        if (index >= 0 && index < static_cast<int> (line->size()))
            line->erase (line->begin() + index);
}

void clearAutomation (Song& song, TrackId id, int lane)
{
    auto* t = findTrack (song, id);
    if (t == nullptr)
        return;
    int slot = 0;
    if (const auto* control = t->controlLine (lane, &slot))
    {
        auto& lines = slot < 0 ? t->instrument.automation : t->effects[static_cast<std::size_t> (slot)].automation;
        lines.erase (lines.begin() + (control - lines.data()));
    }
    else if (auto* line = t->findLine (lane))
        line->clear();
}

int addControlLine (Song& song, TrackId id, int slot, int control, const std::string& name)
{
    auto* t = findTrack (song, id);
    if (t == nullptr || control < 0 || slot < -1 || slot >= static_cast<int> (t->effects.size()))
        return -1;
    auto& plugin = slot < 0 ? t->instrument : t->effects[static_cast<std::size_t> (slot)];
    if (plugin.empty())
        return -1;
    int lane = Track::kFirstControlLane + (slot < 0 ? 0 : static_cast<int> (t->instrument.automation.size()));
    for (int i = 0; i < slot; ++i)
        lane += static_cast<int> (t->effects[static_cast<std::size_t> (i)].automation.size());
    for (std::size_t i = 0; i < plugin.automation.size(); ++i)
        if (plugin.automation[i].control == control)
            return lane + static_cast<int> (i);
    plugin.automation.push_back ({ control, name, {} });
    return lane + static_cast<int> (plugin.automation.size()) - 1;
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

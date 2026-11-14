#include "arranger/core/Style.h"

#include "arranger/core/SongEdit.h"
#include "arranger/core/StyleChord.h"

#include <algorithm>
#include <set>
#include <span>

namespace arranger::style_convert
{

namespace
{
    bool isVoiceOrMixer (const StyleEvent& e, int& channel)
    {
        if (e.bytes.size() < 2 || e.bytes[0] >= 0xf0)
            return false;
        const int kind = e.bytes[0] & 0xf0;
        channel = (e.bytes[0] & 0x0f) + 1;
        if (kind == 0xc0)
            return true;
        return kind == 0xb0 && (e.bytes[1] == 0 || e.bytes[1] == 32 || e.bytes[1] == 7 || e.bytes[1] == 10 || e.bytes[1] == 91 || e.bytes[1] == 93);
    }

    const StyleChannelRule* fileRule (const std::vector<StyleRuleSet>& sets, const std::string& section, int channel)
    {
        for (const auto& set : sets)
            if (section.empty() || std::find (set.sections.begin(), set.sections.end(), section) != set.sections.end())
                for (const auto& r : set.rules)
                    if (r.sourceChannel == channel)
                        return &r;
        return nullptr;
    }

    /** What the keyboard does for a channel with no rule: rhythm as recorded, the rest follow from CMaj7. */
    StyleChannelRule defaultRule (int channel, int destChannel)
    {
        StyleChannelRule r;
        r.sourceChannel = channel;
        r.destChannel = destChannel;
        r.name = kStylePartNames[std::clamp (destChannel, 9, 16) - 9];
        r.sourceRoot = 0;
        r.sourceChordType = static_cast<int> (ChordType::major7);
        if (destChannel == 9 || destChannel == 10)
        {
            r.transpose.ntr = StyleTranspose::Ntr::rootFixed;
            r.transpose.ntt = StyleTranspose::Ntt::bypass;
            r.transpose.rtr = StyleTranspose::Rtr::stop;
        }
        else if (destChannel == 11)
        {
            r.transpose.ntt = StyleTranspose::Ntt::melody;
            r.transpose.bass = true;
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

    std::string sceneName (const Song& song, int scene)
    {
        return scene >= 0 && static_cast<std::size_t> (scene) < song.scenes.size() ? song.scenes[static_cast<std::size_t> (scene)] : std::string();
    }
}

Song toSong (const Style& style)
{
    Song song;
    song.title = style.name;
    song.tempo = TempoMap (style.tempo);
    song.timeSignature = style.timeSignature;
    song.launchQuantise = style.timeSignature.ticksPerBar();
    song.metronome.enabled = false;

    StyleData data;
    data.format = style.format;
    data.ruleSets = style.ruleSets;
    data.extra = style.extra;
    for (const auto& s : style.sections)
        if (! s.events.empty())
            data.sectionEvents[s.name] = s.events;

    std::set<int> channels;
    for (const auto& s : style.sections)
    {
        song.scenes.push_back (s.name);
        for (const auto& [channel, clip] : s.parts)
            channels.insert (channel);
    }

    int colour = 0;
    for (const int channel : channels)
    {
        Track t;
        t.id = song.nextId++;
        t.styleChannel = channel;
        const auto* rule = fileRule (style.ruleSets, {}, channel);
        const int dest = rule != nullptr ? rule->destChannel : channel;
        t.name = rule != nullptr && ! rule->name.empty() ? rule->name : std::string (dest >= 9 ? kStylePartNames[dest - 9] : "Part");
        t.colour = kTrackColours[static_cast<std::size_t> (colour++) % std::size (kTrackColours)];
        t.output.channel = dest;

        // Voice and mixer values from the setup bar become the track's own.
        for (const auto& e : style.setup)
        {
            int ch = 0;
            if (! isVoiceOrMixer (e, ch) || ch != dest)
                continue;
            if ((e.bytes[0] & 0xf0) == 0xc0)
                t.sound.program = e.bytes[1];
            else if (e.bytes.size() >= 3)
                switch (e.bytes[1])
                {
                    case 0: t.sound.bankMsb = e.bytes[2]; break;
                    case 32: t.sound.bankLsb = e.bytes[2]; break;
                    case 7: t.mixer.volume = e.bytes[2]; break;
                    case 10: t.mixer.pan = e.bytes[2]; break;
                    case 91: t.mixer.reverbSend = e.bytes[2]; break;
                    case 93: t.mixer.chorusSend = e.bytes[2]; break;
                    default: break;
                }
        }

        for (const auto& s : style.sections)
        {
            const auto it = s.parts.find (channel);
            if (it == s.parts.end())
            {
                t.slots.push_back (nullptr);
                continue;
            }
            Clip c = it->second;
            c.id = song.nextId++;
            c.start = 0;
            c.length = s.length;
            c.name = s.name;
            t.slots.push_back (std::make_shared<const Clip> (std::move (c)));
        }
        song.tracks.push_back (std::move (t));
    }

    // The rest of the setup bar (SysEx, other controllers) stays as it is.
    std::set<int> dests;
    for (const auto& t : song.tracks)
        dests.insert (t.output.channel);
    for (const auto& e : style.setup)
    {
        int ch = 0;
        if (! (isVoiceOrMixer (e, ch) && dests.count (ch) != 0))
            data.setup.push_back (e);
    }
    song.style = std::move (data);
    return song;
}

Style fromSong (const Song& song)
{
    Style style;
    style.name = song.title;
    style.tempo = song.baseTempo();
    style.timeSignature = song.timeSignature;
    if (song.style)
    {
        style.format = song.style->format;
        style.setup = song.style->setup;
        style.ruleSets = song.style->ruleSets;
        style.extra = song.style->extra;
    }

    // Voices and mixer values, once for each destination channel.
    const Tick at = kTicksPerQuarter / 4;
    std::set<int> done;
    for (const auto& t : song.tracks)
    {
        if (t.styleChannel <= 0 || ! done.insert (t.output.channel).second)
            continue;
        const auto status = [&] (int kind) { return static_cast<std::uint8_t> (kind | (std::clamp (t.output.channel, 1, 16) - 1)); };
        const auto cc = [&] (int number, int value) {
            if (value >= 0)
                style.setup.push_back ({ at, { status (0xb0), static_cast<std::uint8_t> (number), static_cast<std::uint8_t> (std::clamp (value, 0, 127)) } });
        };
        cc (0, t.sound.bankMsb);
        cc (32, t.sound.bankLsb);
        if (t.sound.program >= 0)
            style.setup.push_back ({ at, { status (0xc0), static_cast<std::uint8_t> (std::clamp (t.sound.program, 0, 127)) } });
        cc (7, t.mixer.volume);
        cc (10, t.mixer.pan);
        cc (91, t.mixer.reverbSend);
        cc (93, t.mixer.chorusSend);
    }
    std::stable_sort (style.setup.begin(), style.setup.end(), [] (const StyleEvent& a, const StyleEvent& b) { return a.at < b.at; });

    for (int scene = 0; scene < song.sceneCount(); ++scene)
    {
        StyleSection section;
        section.name = sceneName (song, scene);
        section.length = 0;
        const auto slotOf = [scene] (const Track& t) -> ClipPtr {
            return t.styleChannel > 0 && static_cast<std::size_t> (scene) < t.slots.size() ? t.slots[static_cast<std::size_t> (scene)] : nullptr;
        };
        for (const auto& t : song.tracks)
            if (const auto c = slotOf (t))
                section.length = std::max (section.length, c->length);
        if (section.name.empty() || section.length <= 0)
            continue;
        if (song.style)
            if (const auto it = song.style->sectionEvents.find (section.name); it != song.style->sectionEvents.end())
                section.events = it->second;

        for (const auto& t : song.tracks)
        {
            const auto c = slotOf (t);
            if (c == nullptr || c->length <= 0)
                continue;
            // A shorter slot loops in the app, so it repeats in the file.
            Clip out;
            out.name = section.name;
            out.length = section.length;
            for (Tick base = 0; base < section.length; base += c->length)
            {
                for (auto n : c->notes)
                {
                    if (n.start >= c->length || base + n.start >= section.length)
                        continue;
                    n.length = std::min ({ n.length, c->length - n.start, section.length - base - n.start });
                    n.start += base;
                    out.notes.push_back (n);
                }
                for (auto e : c->controls)
                {
                    if (e.at >= c->length || base + e.at >= section.length)
                        continue;
                    e.at += base;
                    out.controls.push_back (e);
                }
            }
            if (! out.notes.empty() || ! out.controls.empty())
                section.parts[t.styleChannel] = std::move (out);
        }
        style.sections.push_back (std::move (section));
    }
    return style;
}

void makeStyle (Song& song)
{
    if (! song.style)
        song.style = StyleData {};
    std::set<int> used;
    for (const auto& t : song.tracks)
        if (t.styleChannel > 0)
            used.insert (t.styleChannel);

    for (auto& t : song.tracks)
    {
        const bool hasSlots = std::any_of (t.slots.begin(), t.slots.end(), [] (const ClipPtr& c) { return c != nullptr; });
        if (t.styleChannel > 0 || ! hasSlots)
            continue;
        // Drums go to the rhythm parts, the others take the next free part.
        const bool drums = t.output.channel == 10 || t.output.kind == TrackOutput::Kind::sampler;
        int channel = 0;
        static constexpr int kRhythm[] = { 10, 9 }, kOthers[] = { 11, 12, 13, 14, 15, 16 };
        for (const int candidate : drums ? std::span<const int> (kRhythm) : std::span<const int> (kOthers))
            if (used.count (candidate) == 0)
            {
                channel = candidate;
                break;
            }
        if (channel == 0)
            continue; // all eight parts are taken
        used.insert (channel);
        t.styleChannel = channel;
        if (t.output.kind != TrackOutput::Kind::sampler)
            t.output.channel = channel;
    }

    // The keyboard only knows its own section names: name the scenes that have none.
    const int count = song.sceneCount();
    std::size_t next = 4; // Main A
    for (int scene = 0; scene < count; ++scene)
    {
        const bool any = std::any_of (song.tracks.begin(), song.tracks.end(), [scene] (const Track& t) {
            return t.styleChannel > 0 && static_cast<std::size_t> (scene) < t.slots.size() && t.slots[static_cast<std::size_t> (scene)] != nullptr;
        });
        if (! any)
            continue;
        if (song.scenes.size() <= static_cast<std::size_t> (scene))
            song.scenes.resize (static_cast<std::size_t> (scene) + 1);
        auto& name = song.scenes[static_cast<std::size_t> (scene)];
        const bool known = std::any_of (std::begin (kStyleSectionNames), std::end (kStyleSectionNames), [&] (const char* n) { return name == n; });
        if (known)
            continue;
        while (next < std::size (kStyleSectionNames)
               && std::find (song.scenes.begin(), song.scenes.end(), kStyleSectionNames[next]) != song.scenes.end())
            ++next;
        if (next < std::size (kStyleSectionNames))
            name = kStyleSectionNames[next++];
    }
}

StyleChannelRule ruleFor (const Song& song, const Track& track, int scene)
{
    if (song.style)
        if (const auto* r = fileRule (song.style->ruleSets, sceneName (song, scene), track.styleChannel))
            return *r;
    return defaultRule (track.styleChannel, track.output.channel);
}

void setRule (Song& song, const Track& track, int scene, StyleChannelRule rule)
{
    if (track.styleChannel <= 0)
        return;
    if (! song.style)
        song.style = StyleData {};
    auto& sets = song.style->ruleSets;
    rule.sourceChannel = track.styleChannel;
    rule.destChannel = track.output.kind == TrackOutput::Kind::sampler ? std::clamp (track.styleChannel, 9, 10) : std::clamp (track.output.channel, 9, 16);

    const auto put = [&rule] (StyleRuleSet& set) {
        for (auto& r : set.rules)
            if (r.sourceChannel == rule.sourceChannel)
            {
                r = rule;
                return;
            }
        set.rules.push_back (rule);
    };
    const auto setWith = [&sets] (const std::string& name) {
        return std::find_if (sets.begin(), sets.end(), [&name] (const StyleRuleSet& s) {
            return std::find (s.sections.begin(), s.sections.end(), name) != s.sections.end();
        });
    };

    if (scene < 0)
    {
        for (auto& set : sets)
            put (set);
        // Sections that no rule set names get one of their own.
        StyleRuleSet rest;
        for (const auto& name : song.scenes)
            if (! name.empty() && setWith (name) == sets.end() && std::find (rest.sections.begin(), rest.sections.end(), name) == rest.sections.end())
                rest.sections.push_back (name);
        if (! rest.sections.empty())
        {
            put (rest);
            sets.push_back (std::move (rest));
        }
        return;
    }

    const auto name = sceneName (song, scene);
    if (name.empty())
        return;
    auto it = setWith (name);
    if (it == sets.end())
    {
        StyleRuleSet own;
        own.sections = { name };
        put (own);
        sets.push_back (std::move (own));
    }
    else if (it->sections.size() == 1)
        put (*it);
    else
    {
        StyleRuleSet own;
        own.sections = { name };
        own.rules = it->rules;
        std::erase (it->sections, name);
        put (own);
        sets.push_back (std::move (own));
    }
}

int sectionFromTimeline (Song& song, Tick from, Tick to, const std::string& section)
{
    if (to <= from || section.empty())
        return -1;

    // 1. The notes of each track in the range, as one clip that starts at 0.
    std::vector<std::pair<TrackId, Clip>> parts;
    for (const auto& t : song.tracks)
    {
        Clip out;
        out.name = section;
        out.length = to - from;
        for (const auto& c : t.clips)
        {
            if (c->end() <= from || c->start >= to)
                continue;
            const Tick round = c->loopLength > 0 && c->loopLength < c->length ? c->loopLength : c->length;
            for (Tick base = c->start; base < c->end() && base < to; base += round)
            {
                const Tick roundEnd = std::min (base + round, c->end());
                for (auto n : c->notes)
                {
                    const Tick at = base + n.start;
                    if (n.start >= round || at >= roundEnd || at < from || at >= to)
                        continue;
                    n.length = std::max<Tick> (1, std::min (n.length, std::min (roundEnd, to) - at));
                    n.start = at - from;
                    out.notes.push_back (n);
                }
                for (auto e : c->controls)
                {
                    const Tick at = base + e.at;
                    if (e.at >= round || at >= roundEnd || at < from || at >= to)
                        continue;
                    e.at = at - from;
                    out.controls.push_back (e);
                }
            }
        }
        if (! out.notes.empty())
        {
            edit::normalise (out);
            parts.emplace_back (t.id, std::move (out));
        }
    }
    if (parts.empty())
        return -1;

    // 2. The scene: the one with this name, else the first row that nothing uses.
    int scene = static_cast<int> (std::find (song.scenes.begin(), song.scenes.end(), section) - song.scenes.begin());
    if (scene == static_cast<int> (song.scenes.size()))
    {
        scene = 0;
        const auto used = [&song] (int row) {
            const auto r = static_cast<std::size_t> (row);
            if (r < song.scenes.size() && ! song.scenes[r].empty())
                return true;
            return std::any_of (song.tracks.begin(), song.tracks.end(), [r] (const Track& t) { return r < t.slots.size() && t.slots[r] != nullptr; });
        };
        while (used (scene))
            ++scene;
        if (song.scenes.size() <= static_cast<std::size_t> (scene))
            song.scenes.resize (static_cast<std::size_t> (scene) + 1);
        song.scenes[static_cast<std::size_t> (scene)] = section;
    }
    for (auto& t : song.tracks)
        if (t.styleChannel > 0)
            edit::clearSlot (song, t.id, scene);
    for (auto& [id, clip] : parts)
        edit::setSlot (song, id, scene, std::move (clip));

    const bool wasStyle = song.style.has_value();
    makeStyle (song);
    if (! wasStyle)
        song.launchQuantise = song.timeSignature.ticksPerBar();

    // 3. The chord of the passage, from the parts that follow chords.
    double weight[12] = {}, bassWeight[12] = {};
    for (const auto& t : song.tracks)
    {
        if (t.styleChannel <= 0 || t.output.channel == 9 || t.output.channel == 10 || t.output.kind == TrackOutput::Kind::sampler)
            continue;
        const auto s = static_cast<std::size_t> (scene);
        if (s >= t.slots.size() || t.slots[s] == nullptr)
            continue;
        for (const auto& n : t.slots[s]->notes)
        {
            weight[n.pitch % 12] += static_cast<double> (n.length);
            if (t.output.channel == 11)
                bassWeight[n.pitch % 12] += static_cast<double> (n.length) * (n.start == 0 ? 2.0 : 1.0); // the first note is most often the root
        }
    }
    const auto strongest = std::max_element (std::begin (bassWeight), std::end (bassWeight));
    const int bass = *strongest > 0.0 ? static_cast<int> (strongest - std::begin (bassWeight)) : -1;
    if (const auto chord = style_chord::guess (std::span<const double, 12> (weight), bass))
    {
        const std::vector<Track> tracks = song.tracks;
        for (const auto& t : tracks)
        {
            const auto s = static_cast<std::size_t> (scene);
            if (t.styleChannel <= 0 || t.output.channel == 9 || t.output.channel == 10 || t.output.kind == TrackOutput::Kind::sampler
                || s >= t.slots.size() || t.slots[s] == nullptr)
                continue;
            auto rule = ruleFor (song, t, scene);
            rule.sourceRoot = chord->root;
            rule.sourceChordType = static_cast<int> (chord->type);
            setRule (song, t, scene, rule);
        }
    }
    return scene;
}

Song applyChord (const Song& song, const Chord& chord)
{
    Song out = song;
    if (! song.style)
        return out;
    for (auto& t : out.tracks)
    {
        if (t.styleChannel <= 0)
            continue;
        for (std::size_t scene = 0; scene < t.slots.size(); ++scene)
        {
            if (t.slots[scene] == nullptr)
                continue;
            const auto rule = ruleFor (song, t, static_cast<int> (scene));
            Clip moved = *t.slots[scene];
            if (! style_chord::plays (rule, chord))
                moved.notes.clear();
            else
            {
                std::vector<Note> notes;
                for (auto n : moved.notes)
                    if (const auto p = style_chord::transpose (n.pitch, rule, chord))
                    {
                        n.pitch = static_cast<std::uint8_t> (*p);
                        notes.push_back (n);
                    }
                std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
                moved.notes = std::move (notes);
            }
            t.slots[scene] = std::make_shared<const Clip> (std::move (moved));
        }
    }
    return out;
}

std::vector<TrackId> fillTimeline (Song& song, const Style& style, const Fill& fill)
{
    const Song source = toSong (style);
    const auto sceneIt = std::find (source.scenes.begin(), source.scenes.end(), fill.section);
    if (sceneIt == source.scenes.end() || fill.bars <= 0)
        return {};
    const auto scene = static_cast<std::size_t> (sceneIt - source.scenes.begin());
    const Tick bar = song.timeSignature.ticksPerBar();
    const Tick total = bar * fill.bars;
    const Tick at = std::max<Tick> (0, fill.at);

    // The style moved to each chord that is used, made once.
    const std::vector<Chord> chords = fill.chords.empty() ? std::vector<Chord> { Chord {} } : fill.chords;
    std::vector<std::pair<Chord, Song>> moved;
    const auto songFor = [&] (const Chord& c) -> const Song& {
        for (const auto& [chord, s] : moved)
            if (chord == c)
                return s;
        moved.emplace_back (c, applyChord (source, c));
        return moved.back().second;
    };
    for (const auto& c : chords)
        songFor (c);

    // SoundFont channels that have a sound already.
    std::set<int> taken;
    for (const auto& t : song.tracks)
        if (t.output.kind == TrackOutput::Kind::soundFont)
            taken.insert (t.output.channel);

    std::vector<TrackId> made;
    for (std::size_t ti = 0; ti < source.tracks.size(); ++ti)
    {
        const auto& part = source.tracks[ti];
        if (scene >= part.slots.size() || part.slots[scene] == nullptr || part.slots[scene]->length <= 0)
            continue;

        Clip clip;
        clip.name = style.name + " - " + fill.section;
        clip.start = at;
        clip.length = total;
        for (int b = 0; b < fill.bars; ++b)
        {
            const auto& chord = chords[static_cast<std::size_t> (b) % chords.size()];
            const auto& slot = *songFor (chord).tracks[ti].slots[scene];
            const Tick from = bar * b, to = from + bar, length = slot.length;
            // The section repeats from the start of the fill. Notes stop at the end of the bar, where the chord can change.
            for (Tick base = (from / length) * length; base < to; base += length)
            {
                for (const auto& n : slot.notes)
                {
                    const Tick start = base + n.start;
                    if (start < from || start >= to)
                        continue;
                    Note copy = n;
                    copy.start = start;
                    copy.length = std::max<Tick> (1, std::min (n.length, to - start));
                    clip.notes.push_back (copy);
                }
                for (const auto& c : slot.controls)
                {
                    const Tick time = base + c.at;
                    if (time < from || time >= to)
                        continue;
                    ControlEvent copy = c;
                    copy.at = time;
                    clip.controls.push_back (copy);
                }
            }
        }
        if (clip.notes.empty())
            continue;
        std::stable_sort (clip.notes.begin(), clip.notes.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
        std::stable_sort (clip.controls.begin(), clip.controls.end(), [] (const ControlEvent& a, const ControlEvent& b) { return a.at < b.at; });

        Track t = part;
        t.id = song.nextId++;
        t.styleChannel = 0; // an ordinary track from here on
        t.slots.clear();
        t.armed = false;
        t.colour = kTrackColours[song.tracks.size() % std::size (kTrackColours)];
        // Drum kits play on channel 10 of a General MIDI SoundFont. Other parts get a channel of their own.
        const bool drums = t.sound.bankMsb >= 126 || t.output.channel == 10;
        if (drums)
            t.output.channel = 10;
        else if (taken.count (t.output.channel) != 0)
            for (int ch = 1; ch <= 16; ++ch)
                if (ch != 10 && taken.count (ch) == 0)
                {
                    t.output.channel = ch;
                    break;
                }
        taken.insert (t.output.channel);
        clip.id = song.nextId++;
        t.clips.push_back (std::make_shared<const Clip> (std::move (clip)));
        made.push_back (t.id);
        song.tracks.push_back (std::move (t));
    }
    return made;
}

} // namespace arranger::style_convert

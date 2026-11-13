#include "arranger/core/Style.h"

#include "arranger/core/StyleChord.h"

#include <algorithm>
#include <set>

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
        for (const int candidate : drums ? std::initializer_list<int> { 10, 9 } : std::initializer_list<int> { 11, 12, 13, 14, 15, 16 })
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

} // namespace arranger::style_convert

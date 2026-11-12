#include "arranger/engine/Playback.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace arranger
{

namespace
{
    std::uint64_t mix (std::uint64_t h, std::uint64_t v) noexcept
    {
        return (h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2)));
    }

    SequenceEvent message (Tick at, std::uint8_t order, std::uint8_t b0, int b1, int b2 = 0)
    {
        SequenceEvent e;
        e.at = at;
        e.order = order;
        e.bytes = { b0, static_cast<std::uint8_t> (std::clamp (b1, 0, 127)), static_cast<std::uint8_t> (std::clamp (b2, 0, 127)) };
        const int kind = b0 & 0xf0;
        e.size = (kind == 0xc0 || kind == 0xd0) ? 2 : 3;
        return e;
    }
}

std::vector<SequenceEvent> trackSetupMessages (const Track& t)
{
    const auto ch = static_cast<std::uint8_t> (std::clamp (t.output.channel, 1, 16) - 1);
    std::vector<SequenceEvent> out;
    const auto cc = static_cast<std::uint8_t> (0xb0 | ch);
    if (t.sound.bankMsb >= 0) out.push_back (message (0, 1, cc, 0, t.sound.bankMsb));
    if (t.sound.bankLsb >= 0) out.push_back (message (0, 1, cc, 32, t.sound.bankLsb));
    if (t.sound.program >= 0) out.push_back (message (0, 1, static_cast<std::uint8_t> (0xc0 | ch), t.sound.program));
    out.push_back (message (0, 1, cc, 7, t.mixer.volume));
    out.push_back (message (0, 1, cc, 10, t.mixer.pan));
    out.push_back (message (0, 1, cc, 91, t.mixer.reverbSend));
    out.push_back (message (0, 1, cc, 93, t.mixer.chorusSend));
    return out;
}

PlaybackSnapshot buildSnapshot (const Song& song)
{
    PlaybackSnapshot s;
    s.tempo = song.tempo;
    s.timeSignature = song.timeSignature;
    s.loop = song.loop;
    s.metronome = song.metronome;
    s.masterGain = static_cast<float> (std::pow (10.0, song.master.gainDb / 20.0));

    const bool anySolo = std::any_of (song.tracks.begin(), song.tracks.end(), [] (const Track& t) { return t.mixer.solo; });

    for (const auto& t : song.tracks)
    {
        if (t.output.kind == TrackOutput::Kind::none)
            continue;

        PlaybackTrack p;
        p.id = t.id;
        p.channel = static_cast<std::uint8_t> (std::clamp (t.output.channel, 1, 16) - 1);
        p.audible = ! t.mixer.mute && (! anySolo || t.mixer.solo);

        if (t.output.kind == TrackOutput::Kind::midiPort)
        {
            auto it = std::find (s.ports.begin(), s.ports.end(), t.output.port);
            if (it == s.ports.end())
            {
                s.ports.push_back (t.output.port);
                it = s.ports.end() - 1;
            }
            p.port = static_cast<std::uint16_t> ((it - s.ports.begin()) + 1);
        }

        p.setup = trackSetupMessages (t);

        const std::uint8_t ch = p.channel;
        std::uint64_t key = mix (mix (mix (t.id, p.port), p.channel), p.audible ? 1 : 0);
        for (const auto& clipPtr : t.clips)
        {
            const auto& clip = *clipPtr;
            key = mix (key, reinterpret_cast<std::uintptr_t> (clipPtr.get()));
            for (const auto& n : clip.notes)
            {
                if (n.start >= clip.length)
                    continue; // hidden past the clip end
                const Tick on = clip.start + n.start;
                const Tick off = clip.start + std::min (n.end(), clip.length);
                p.events.push_back (message (on, 2, static_cast<std::uint8_t> (0x90 | ch), n.pitch, n.velocity));
                p.events.push_back (message (off, 0, static_cast<std::uint8_t> (0x80 | ch), n.pitch, 0));
            }
            for (const auto& c : clip.controls)
            {
                if (c.at >= clip.length)
                    continue;
                const Tick at = clip.start + c.at;
                switch (c.kind)
                {
                    case ControlEvent::Kind::controller: p.events.push_back (message (at, 1, static_cast<std::uint8_t> (0xb0 | ch), c.number, c.value)); break;
                    case ControlEvent::Kind::program: p.events.push_back (message (at, 1, static_cast<std::uint8_t> (0xc0 | ch), c.value)); break;
                    case ControlEvent::Kind::channelPressure: p.events.push_back (message (at, 1, static_cast<std::uint8_t> (0xd0 | ch), c.value)); break;
                    case ControlEvent::Kind::pitchBend:
                    {
                        const int v = std::clamp (c.value + 8192, 0, 16383);
                        p.events.push_back (message (at, 1, static_cast<std::uint8_t> (0xe0 | ch), v & 0x7f, v >> 7));
                        break;
                    }
                }
            }
        }
        std::stable_sort (p.events.begin(), p.events.end(),
                          [] (const SequenceEvent& a, const SequenceEvent& b) { return a.at != b.at ? a.at < b.at : a.order < b.order; });
        p.contentKey = key;
        s.tracks.push_back (std::move (p));
    }
    return s;
}

} // namespace arranger

#include <optional>
#include "arranger/engine/Playback.h"

#include "arranger/core/SongEdit.h"

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

std::vector<SequenceEvent> trackSetupMessages (const Track& t, const MixerControls& controls)
{
    const auto ch = static_cast<std::uint8_t> (std::clamp (t.output.channel, 1, 16) - 1);
    std::vector<SequenceEvent> out;
    const auto cc = static_cast<std::uint8_t> (0xb0 | ch);
    if (t.sound.bankMsb >= 0) out.push_back (message (0, 1, cc, 0, t.sound.bankMsb));
    if (t.sound.bankLsb >= 0) out.push_back (message (0, 1, cc, 32, t.sound.bankLsb));
    if (t.sound.program >= 0) out.push_back (message (0, 1, static_cast<std::uint8_t> (0xc0 | ch), t.sound.program));
    const auto add = [&] (int number, int value) {
        if (number >= 0 && number <= 127)
            out.push_back (message (0, 1, cc, static_cast<std::uint8_t> (number), value));
    };
    add (controls.volume, t.mixer.volume);
    add (controls.pan, t.mixer.pan);
    add (controls.reverb, t.mixer.reverbSend);
    add (controls.chorus, t.mixer.chorusSend);
    return out;
}

std::pair<float, float> stripGains (double volume, double pan) noexcept
{
    const double quarter = 1.5707963267948966;
    const double level = std::pow (std::clamp (volume, 0.0, 127.0) / 100.0, 2.0);
    const double angle = std::clamp (pan, 0.0, 127.0) / 127.0 * quarter;
    const double centre = 64.0 / 127.0 * quarter;
    return { static_cast<float> (level * std::cos (angle) / std::cos (centre)), static_cast<float> (level * std::sin (angle) / std::sin (centre)) };
}

std::pair<float, float> PlaybackSnapshot::GainLane::at (double tick) const noexcept
{
    return stripGains (automation::valueAt (volume, tick, staticVolume), automation::valueAt (pan, tick, staticPan));
}

const PlaybackSnapshot::GainLane* PlaybackSnapshot::laneFor (TrackId id) const noexcept
{
    for (const auto& l : gainLanes)
        if (l.track == id)
            return &l;
    return nullptr;
}

std::uint16_t PlaybackSnapshot::samplerPort (TrackId id) const noexcept
{
    for (std::size_t i = 0; i < samplerTracks.size(); ++i)
        if (samplerTracks[i] == id)
            return static_cast<std::uint16_t> (MidiEvent::kSamplerPortBase + i);
    return MidiEvent::kNoPort;
}

PlaybackSnapshot buildSnapshot (const Song& song, const std::function<MixerControls (const std::string&)>& mixerForPort, const SampleSource& samples, const StretchSource& stretched)
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
        if (t.output.kind == TrackOutput::Kind::audio)
        {
            // An audio track has no MIDI. In the track list it only says which Clip grid slots it has, so that they launch.
            PlaybackTrack p;
            p.id = t.id;
            p.port = MidiEvent::kNoPort;
            p.audible = ! t.mixer.mute && (! anySolo || t.mixer.solo);
            for (const auto& slot : t.audioSlots)
            {
                SlotSequence seq;
                if (slot != nullptr)
                {
                    seq.clip = slot->id;
                    seq.length = slot->length;
                }
                p.slots.push_back (std::move (seq));
            }
            s.tracks.push_back (std::move (p));

            // Its clips become regions with the track's volume and pan in them.
            if (t.mixer.mute || (anySolo && ! t.mixer.solo) || ! samples)
                continue;
            // With automation the engine sets the track's level as the song moves; the clips then carry only their own gain.
            const bool automated = ! t.volumeAutomation.empty() || ! t.panAutomation.empty();
            if (automated)
                s.gainLanes.push_back ({ t.id, t.volumeAutomation, t.panAutomation, static_cast<float> (t.mixer.volume), static_cast<float> (t.mixer.pan) });
            const auto strip = automated ? std::pair<float, float> { 1.0f, 1.0f } : stripGains (t.mixer.volume, t.mixer.pan);
            const auto regionOf = [&] (const AudioClip& c) -> std::optional<AudioRegion> {
                // A clip that follows the tempo plays a stretched copy of its file.
                double speed = stretched ? edit::audioSpeed (song, c) : 1.0;
                auto sample = speed != 1.0 ? stretched (c.file, speed) : nullptr;
                if (sample == nullptr)
                {
                    speed = 1.0;
                    sample = samples (c.file);
                }
                if (sample == nullptr || sample->frames() == 0 || c.length <= 0)
                    return {};
                AudioRegion r;
                r.track = t.id;
                r.clip = c.id;
                r.sample = std::move (sample);
                r.start = c.start;
                r.end = c.end();
                r.startSeconds = song.tempo.secondsAt (static_cast<double> (r.start));
                r.endSeconds = song.tempo.secondsAt (static_cast<double> (r.end));
                r.offset = std::max (0.0, c.offset) / speed;
                r.fadeIn = song.tempo.secondsAt (static_cast<double> (r.start + std::clamp<Tick> (c.fadeIn, 0, c.length))) - r.startSeconds;
                r.fadeOut = r.endSeconds - song.tempo.secondsAt (static_cast<double> (r.end - std::clamp<Tick> (c.fadeOut, 0, c.length)));
                const auto gain = static_cast<float> (std::pow (10.0, static_cast<double> (c.gainDb) / 20.0));
                r.gainLeft = gain * strip.first;
                r.gainRight = gain * strip.second;
                return r;
            };
            for (const auto& c : t.audio)
                if (auto r = regionOf (*c))
                    s.audio.push_back (std::move (*r));
            for (std::size_t scene = 0; scene < t.audioSlots.size(); ++scene)
                if (t.audioSlots[scene] != nullptr)
                    if (auto r = regionOf (*t.audioSlots[scene]))
                    {
                        r->scene = static_cast<int> (scene);
                        s.slotAudio.push_back (std::move (*r));
                    }
            continue;
        }

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

        else if (t.output.kind == TrackOutput::Kind::sampler || t.output.kind == TrackOutput::Kind::plugin)
        {
            p.port = static_cast<std::uint16_t> (MidiEvent::kSamplerPortBase + s.samplerTracks.size());
            s.samplerTracks.push_back (t.id);
        }
        if (t.output.kind == TrackOutput::Kind::plugin)
        {
            // The same volume and pan law as audio tracks.
            const auto strip = p.audible ? stripGains (t.mixer.volume, t.mixer.pan) : std::pair<float, float> { 0.0f, 0.0f };
            s.busGains.push_back ({ t.id, strip.first, strip.second });
            if (p.audible && (! t.volumeAutomation.empty() || ! t.panAutomation.empty()))
                s.gainLanes.push_back ({ t.id, t.volumeAutomation, t.panAutomation, static_cast<float> (t.mixer.volume), static_cast<float> (t.mixer.pan) });
        }

        const bool hardware = t.output.kind == TrackOutput::Kind::midiPort;
        p.setup = trackSetupMessages (t, hardware && mixerForPort ? mixerForPort (t.output.port) : MixerControls {});

        const std::uint8_t ch = p.channel;
        std::uint64_t key = mix (mix (mix (t.id, p.port), p.channel), p.audible ? 1 : 0);
        const auto byTime = [] (const SequenceEvent& x, const SequenceEvent& y) { return x.at != y.at ? x.at < y.at : x.order < y.order; };
        const auto flatten = [ch] (const Clip& clip, Tick origin, std::vector<SequenceEvent>& out) {
            // A looping clip repeats its first `loopLength` ticks until the clip ends.
            const bool looping = clip.loopLength > 0 && clip.loopLength < clip.length;
            const Tick cycle = looping ? clip.loopLength : clip.length;
            for (Tick base = 0; base < clip.length; base += cycle)
            {
            const Tick cycleEnd = std::min (base + cycle, clip.length);
            for (const auto& n : clip.notes)
            {
                if (n.start >= cycle || base + n.start >= clip.length)
                    continue; // hidden past the clip (or loop) end
                const Tick on = origin + base + n.start;
                const Tick off = origin + std::min (base + n.end(), cycleEnd);
                out.push_back (message (on, 2, static_cast<std::uint8_t> (0x90 | ch), n.pitch, n.velocity));
                out.push_back (message (off, 0, static_cast<std::uint8_t> (0x80 | ch), n.pitch, 0));
            }
            for (const auto& c : clip.controls)
            {
                if (c.at >= cycle || base + c.at >= clip.length)
                    continue;
                const Tick at = origin + base + c.at;
                switch (c.kind)
                {
                    case ControlEvent::Kind::controller: out.push_back (message (at, 1, static_cast<std::uint8_t> (0xb0 | ch), c.number, c.value)); break;
                    case ControlEvent::Kind::program: out.push_back (message (at, 1, static_cast<std::uint8_t> (0xc0 | ch), c.value)); break;
                    case ControlEvent::Kind::channelPressure: out.push_back (message (at, 1, static_cast<std::uint8_t> (0xd0 | ch), c.value)); break;
                    case ControlEvent::Kind::pitchBend:
                    {
                        const int v = std::clamp (c.value + 8192, 0, 16383);
                        out.push_back (message (at, 1, static_cast<std::uint8_t> (0xe0 | ch), v & 0x7f, v >> 7));
                        break;
                    }
                }
            }
            }
        };

        for (const auto& clipPtr : t.clips)
        {
            key = mix (key, reinterpret_cast<std::uintptr_t> (clipPtr.get()));
            flatten (*clipPtr, clipPtr->start, p.events);
        }

        for (const auto& slot : t.slots)
        {
            SlotSequence seq;
            if (slot != nullptr)
            {
                key = mix (key, reinterpret_cast<std::uintptr_t> (slot.get()));
                seq.clip = slot->id;
                seq.length = slot->length;
                flatten (*slot, 0, seq.events);
                // Note-offs that fall on the end are dropped: the engine ends held notes when the slot wraps.
                std::erase_if (seq.events, [&] (const SequenceEvent& e) { return e.at >= seq.length; });
                std::stable_sort (seq.events.begin(), seq.events.end(), byTime);
            }
            p.slots.push_back (std::move (seq));
        }
        // Automation of a track that takes MIDI: controller messages along the line.
        if (t.output.kind != TrackOutput::Kind::plugin)
        {
            const auto controls = hardware && mixerForPort ? mixerForPort (t.output.port) : MixerControls {};
            const auto write = [&] (const AutomationLine& line, int number) {
                if (line.empty() || number < 0 || number > 127)
                    return;
                const Tick end = std::max (song.contentEnd(), line.back().at) + kTicksPerQuarter * 4;
                constexpr Tick kStep = kTicksPerQuarter / 8;
                int last = -1;
                for (Tick at = 0; at <= end; at += kStep)
                {
                    const int value = static_cast<int> (std::lround (automation::valueAt (line, static_cast<double> (at), 0.0f)));
                    // Each change, and once per beat anyway so a start in the middle of the song soon has the right value.
                    if (value != last || at % kTicksPerQuarter == 0)
                        p.events.push_back (message (at, 1, static_cast<std::uint8_t> (0xb0 | ch), number, value));
                    last = value;
                    key = mix (key, static_cast<std::uint64_t> (value));
                }
            };
            write (t.volumeAutomation, controls.volume);
            write (t.panAutomation, controls.pan);
        }
        std::stable_sort (p.events.begin(), p.events.end(), byTime);
        p.contentKey = key;
        s.tracks.push_back (std::move (p));
    }
    std::stable_sort (s.audio.begin(), s.audio.end(), [] (const AudioRegion& a, const AudioRegion& b) { return a.start < b.start; });
    return s;
}

} // namespace arranger

#include "arranger/core/MidiFile.h"

#include "arranger/core/SongEdit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <fstream>
#include <map>
#include <string>

namespace arranger::midi_file
{

namespace
{
    // ---- reading -------------------------------------------------------------
    class Reader
    {
    public:
        explicit Reader (std::span<const std::uint8_t> b) : bytes (b) {}

        bool atEnd() const noexcept { return pos >= bytes.size(); }
        std::size_t position() const noexcept { return pos; }

        std::uint8_t u8()
        {
            if (pos >= bytes.size())
                throw Error ("The MIDI file ends too early");
            return bytes[pos++];
        }
        std::uint8_t peek() const
        {
            if (pos >= bytes.size())
                throw Error ("The MIDI file ends too early");
            return bytes[pos];
        }
        std::uint32_t u16() { const std::uint32_t a = u8(); return (a << 8) | u8(); }
        std::uint32_t u32() { const std::uint32_t a = u16(); return (a << 16) | u16(); }
        std::uint32_t varLen()
        {
            std::uint32_t v = 0;
            for (int i = 0; i < 4; ++i)
            {
                const auto b = u8();
                v = (v << 7) | (b & 0x7f);
                if ((b & 0x80) == 0)
                    return v;
            }
            throw Error ("Bad variable-length number in the MIDI file");
        }
        std::span<const std::uint8_t> take (std::size_t n)
        {
            if (n > bytes.size() - pos)
                throw Error ("The MIDI file ends too early");
            auto s = bytes.subspan (pos, n);
            pos += n;
            return s;
        }
        void skip (std::size_t n) { take (n); }

    private:
        std::span<const std::uint8_t> bytes;
        std::size_t pos = 0;
    };

    /** Notes and controls collected for one (file track, channel) pair. */
    struct Part
    {
        std::string name;
        int channel = 1;
        std::vector<Note> notes; // absolute ticks
        std::vector<ControlEvent> controls;
        int bankMsb = -1, bankLsb = -1, program = -1;
        int volume = -1, pan = -1, reverb = -1, chorus = -1;
        std::array<std::deque<std::pair<Tick, std::uint8_t>>, 128> open; // note-ons waiting for their note-off
    };

    constexpr Tick kStartWindow = kTicksPerQuarter / 8; // events this close to 0 count as "setup"
}

Song read (std::span<const std::uint8_t> bytes)
{
    Reader r (bytes);

    // Some files have a RIFF wrapper (.rmi).
    if (bytes.size() > 20 && std::equal (bytes.begin(), bytes.begin() + 4, "RIFF"))
    {
        static constexpr std::array<std::uint8_t, 4> header { 'M', 'T', 'h', 'd' };
        const auto it = std::search (bytes.begin(), bytes.end(), header.begin(), header.end());
        if (it == bytes.end())
            throw Error ("No MIDI data inside this RIFF file");
        r.skip (static_cast<std::size_t> (it - bytes.begin()));
    }

    const auto header = r.take (4);
    if (! std::equal (header.begin(), header.end(), "MThd"))
        throw Error ("Not a MIDI file");
    const auto headerLen = r.u32();
    if (headerLen < 6)
        throw Error ("Bad MIDI file header");
    const auto format = r.u16();
    const auto numTracks = r.u16();
    const auto division = r.u16();
    r.skip (headerLen - 6);

    if (format > 1)
        throw Error ("MIDI file format 2 is not supported");
    if ((division & 0x8000) != 0 || division == 0)
        throw Error ("SMPTE-timed MIDI files are not supported yet");

    auto scale = [division] (std::uint64_t t) {
        return static_cast<Tick> ((t * static_cast<std::uint64_t> (kTicksPerQuarter) + division / 2) / division);
    };

    Song song;
    song.title.clear();
    std::vector<TempoPoint> tempos;
    bool haveTimeSig = false, haveKey = false;
    std::map<std::pair<int, int>, Part> parts; // (file track, channel)
    Tick songEnd = 0;

    for (std::uint32_t trackIndex = 0; trackIndex < numTracks && ! r.atEnd(); ++trackIndex)
    {
        // Skip unknown chunks.
        auto id = r.take (4);
        auto len = r.u32();
        while (! std::equal (id.begin(), id.end(), "MTrk"))
        {
            r.skip (len);
            if (r.atEnd())
                break;
            id = r.take (4);
            len = r.u32();
        }
        const auto chunkEnd = r.position() + len;

        std::uint64_t time = 0;
        std::uint8_t running = 0;
        std::string trackName;
        std::vector<int> channelsInTrack;

        auto part = [&] (int channel) -> Part& {
            auto& p = parts[{ static_cast<int> (trackIndex), channel }];
            p.channel = channel;
            return p;
        };

        while (r.position() < chunkEnd)
        {
            time += r.varLen();
            const Tick t = scale (time);
            songEnd = std::max (songEnd, t);

            std::uint8_t status = r.peek();
            if (status & 0x80)
                r.u8();
            else if (running == 0)
                throw Error ("Bad MIDI event (no status byte)");
            else
                status = running;

            if (status == 0xff)
            {
                const auto type = r.u8();
                const auto data = r.take (r.varLen());
                if (type == 0x2f)
                    break; // end of track
                if (type == 0x03)
                    trackName.assign (data.begin(), data.end());
                else if (type == 0x51 && data.size() == 3)
                {
                    const auto usPerQuarter = (static_cast<std::uint32_t> (data[0]) << 16) | (data[1] << 8) | data[2];
                    if (usPerQuarter > 0)
                        tempos.push_back ({ t, 60'000'000.0 / usPerQuarter });
                }
                else if (type == 0x58 && data.size() >= 2 && ! haveTimeSig)
                {
                    TimeSignature sig { data[0], 1 << std::min<int> (data[1], 5) };
                    if (sig.isValid())
                        song.timeSignature = sig;
                    haveTimeSig = true;
                }
                else if (type == 0x59 && data.size() == 2 && ! haveKey)
                {
                    static const char* majors[] = { "Cb", "Gb", "Db", "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "C#" };
                    static const char* minors[] = { "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "C#", "G#", "D#", "A#" };
                    const int sf = static_cast<std::int8_t> (data[0]);
                    if (sf >= -7 && sf <= 7)
                        song.key = std::string (data[1] ? minors[sf + 7] : majors[sf + 7]) + (data[1] ? " minor" : " major");
                    haveKey = true;
                }
                continue;
            }
            if (status == 0xf0 || status == 0xf7)
            {
                r.skip (r.varLen()); // SysEx: handled by device profiles in a later step
                continue;
            }
            if (status >= 0xf0)
                throw Error ("Unexpected system message in the MIDI file");

            running = status;
            const int kind = status & 0xf0;
            const int channel = (status & 0x0f) + 1;
            const std::uint8_t d1 = r.u8() & 0x7f;
            const std::uint8_t d2 = (kind == 0xc0 || kind == 0xd0) ? 0 : (r.u8() & 0x7f);

            if (std::find (channelsInTrack.begin(), channelsInTrack.end(), channel) == channelsInTrack.end())
                channelsInTrack.push_back (channel);
            auto& p = part (channel);
            const bool atStart = t <= kStartWindow && p.notes.empty();

            switch (kind)
            {
                case 0x90:
                    if (d2 > 0)
                    {
                        p.open[d1].push_back ({ t, d2 });
                        break;
                    }
                    [[fallthrough]];
                case 0x80:
                    if (! p.open[d1].empty())
                    {
                        const auto [start, vel] = p.open[d1].front();
                        p.open[d1].pop_front();
                        p.notes.push_back ({ start, std::max<Tick> (1, t - start), d1, vel });
                    }
                    break;
                case 0xb0:
                    if (atStart && d1 == 0) p.bankMsb = d2;
                    else if (atStart && d1 == 32) p.bankLsb = d2;
                    else if (atStart && d1 == 7) p.volume = d2;
                    else if (atStart && d1 == 10) p.pan = d2;
                    else if (atStart && d1 == 91) p.reverb = d2;
                    else if (atStart && d1 == 93) p.chorus = d2;
                    else p.controls.push_back ({ t, ControlEvent::Kind::controller, d1, d2 });
                    break;
                case 0xc0:
                    if (atStart) p.program = d1;
                    else p.controls.push_back ({ t, ControlEvent::Kind::program, 0, d1 });
                    break;
                case 0xd0: p.controls.push_back ({ t, ControlEvent::Kind::channelPressure, 0, d1 }); break;
                case 0xe0: p.controls.push_back ({ t, ControlEvent::Kind::pitchBend, 0, ((d2 << 7) | d1) - 8192 }); break;
                default: break; // poly pressure: ignored for now
            }
        }

        // Notes still held at the end of the track end there.
        for (const int ch : channelsInTrack)
        {
            auto& p = part (ch);
            for (int pitch = 0; pitch < 128; ++pitch)
                for (const auto& [start, vel] : p.open[static_cast<std::size_t> (pitch)])
                    p.notes.push_back ({ start, std::max<Tick> (1, songEnd - start), static_cast<std::uint8_t> (pitch), vel });
            p.name = trackName;
        }
        if (trackIndex == 0 && song.title.empty() && ! trackName.empty() && (channelsInTrack.empty() || format == 0))
            song.title = trackName;

        r.skip (chunkEnd > r.position() ? chunkEnd - r.position() : 0);
    }

    if (song.title.empty())
        song.title = "Imported MIDI";
    song.tempo.setPoints (tempos.empty() ? std::vector<TempoPoint> { { 0, 120.0 } } : tempos);

    const Tick bar = song.timeSignature.ticksPerBar();
    const Tick end = ((songEnd + bar - 1) / bar) * bar;

    for (auto& [key, p] : parts)
    {
        if (p.notes.empty() && p.controls.empty())
            continue;

        const bool drums = p.channel == 10;
        auto& track = edit::addTrack (song, p.name.empty() ? (drums ? std::string ("Drums") : "Channel " + std::to_string (p.channel)) : p.name);
        track.output.channel = p.channel;
        track.sound.bankMsb = p.bankMsb;
        track.sound.bankLsb = p.bankLsb;
        track.sound.program = p.program;
        if (drums)
            track.colour = "coral";
        if (p.volume >= 0) track.mixer.volume = p.volume;
        if (p.pan >= 0) track.mixer.pan = p.pan;
        if (p.reverb >= 0) track.mixer.reverbSend = p.reverb;
        if (p.chorus >= 0) track.mixer.chorusSend = p.chorus;

        Clip clip;
        clip.name = track.name;
        clip.start = 0;
        clip.length = std::max (end, bar);
        clip.notes = std::move (p.notes);
        clip.controls = std::move (p.controls);
        edit::addClip (song, track.id, std::move (clip));
    }

    return song;
}

Song load (const std::filesystem::path& path)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        throw Error ("Can't open " + path.string());
    std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    auto song = read (bytes);
    if (song.title == "Imported MIDI")
        song.title = path.stem().string();
    return song;
}

// ---- writing -------------------------------------------------------------------
namespace
{
    struct Writer
    {
        std::vector<std::uint8_t> out;

        void u8 (int v) { out.push_back (static_cast<std::uint8_t> (v)); }
        void u16 (std::uint32_t v) { u8 (static_cast<int> ((v >> 8) & 0xff)); u8 (static_cast<int> (v & 0xff)); }
        void u32 (std::uint32_t v) { u16 (v >> 16); u16 (v & 0xffff); }
        void varLen (std::uint32_t v)
        {
            std::uint8_t buf[5];
            int n = 0;
            buf[n++] = v & 0x7f;
            while ((v >>= 7) != 0)
                buf[n++] = static_cast<std::uint8_t> ((v & 0x7f) | 0x80);
            while (n > 0)
                u8 (buf[--n]);
        }
        void text (const std::string& s) { out.insert (out.end(), s.begin(), s.end()); }
    };

    struct TimedEvent
    {
        Tick at;
        int order; // at equal time: setup, note-offs, then others, then note-ons
        std::vector<std::uint8_t> bytes;
    };

    void writeTrack (Writer& file, std::vector<TimedEvent> events)
    {
        std::stable_sort (events.begin(), events.end(), [] (auto& a, auto& b) { return a.at != b.at ? a.at < b.at : a.order < b.order; });
        Writer track;
        Tick last = 0;
        for (const auto& e : events)
        {
            track.varLen (static_cast<std::uint32_t> (std::max<Tick> (0, e.at - last)));
            last = std::max (last, e.at);
            track.out.insert (track.out.end(), e.bytes.begin(), e.bytes.end());
        }
        track.varLen (0);
        track.u8 (0xff);
        track.u8 (0x2f);
        track.u8 (0);

        file.text ("MTrk");
        file.u32 (static_cast<std::uint32_t> (track.out.size()));
        file.out.insert (file.out.end(), track.out.begin(), track.out.end());
    }

    std::vector<std::uint8_t> meta (int type, const std::string& data)
    {
        Writer w;
        w.u8 (0xff);
        w.u8 (type);
        w.varLen (static_cast<std::uint32_t> (data.size()));
        w.text (data);
        return w.out;
    }
}

std::vector<std::uint8_t> write (const Song& song)
{
    Writer file;
    file.text ("MThd");
    file.u32 (6);
    file.u16 (1);
    file.u16 (static_cast<std::uint32_t> (song.tracks.size() + 1));
    file.u16 (static_cast<std::uint32_t> (kTicksPerQuarter));

    // Tempo track
    std::vector<TimedEvent> conductor;
    conductor.push_back ({ 0, 0, meta (0x03, song.title) });
    {
        int dd = 0;
        for (int d = song.timeSignature.denominator; d > 1; d >>= 1)
            ++dd;
        conductor.push_back ({ 0, 0, { 0xff, 0x58, 4, static_cast<std::uint8_t> (song.timeSignature.numerator), static_cast<std::uint8_t> (dd), 24, 8 } });
    }
    for (const auto& p : song.tempo.getPoints())
    {
        const auto us = static_cast<std::uint32_t> (std::llround (60'000'000.0 / p.bpm));
        conductor.push_back ({ p.at, 0, { 0xff, 0x51, 3, static_cast<std::uint8_t> (us >> 16), static_cast<std::uint8_t> ((us >> 8) & 0xff), static_cast<std::uint8_t> (us & 0xff) } });
    }
    writeTrack (file, std::move (conductor));

    for (const auto& t : song.tracks)
    {
        const auto ch = static_cast<std::uint8_t> (std::clamp (t.output.channel, 1, 16) - 1);
        std::vector<TimedEvent> ev;
        ev.push_back ({ 0, 0, meta (0x03, t.name) });
        auto cc = [&] (Tick at, int number, int value, int order) {
            ev.push_back ({ at, order, { static_cast<std::uint8_t> (0xb0 | ch), static_cast<std::uint8_t> (number), static_cast<std::uint8_t> (std::clamp (value, 0, 127)) } });
        };
        if (t.sound.bankMsb >= 0) cc (0, 0, t.sound.bankMsb, 0);
        if (t.sound.bankLsb >= 0) cc (0, 32, t.sound.bankLsb, 0);
        if (t.sound.program >= 0)
            ev.push_back ({ 0, 0, { static_cast<std::uint8_t> (0xc0 | ch), static_cast<std::uint8_t> (t.sound.program & 0x7f) } });
        cc (0, 7, t.mixer.volume, 0);
        cc (0, 10, t.mixer.pan, 0);
        cc (0, 91, t.mixer.reverbSend, 0);
        cc (0, 93, t.mixer.chorusSend, 0);

        for (const auto& clip : t.clips)
        {
            for (const auto& n : clip->notes)
            {
                if (n.start >= clip->length)
                    continue;
                const Tick on = clip->start + n.start;
                const Tick off = clip->start + std::min (n.end(), clip->length);
                ev.push_back ({ on, 3, { static_cast<std::uint8_t> (0x90 | ch), n.pitch, n.velocity } });
                ev.push_back ({ off, 1, { static_cast<std::uint8_t> (0x80 | ch), n.pitch, 0 } });
            }
            for (const auto& c : clip->controls)
            {
                if (c.at >= clip->length)
                    continue;
                const Tick at = clip->start + c.at;
                switch (c.kind)
                {
                    case ControlEvent::Kind::controller: cc (at, c.number, c.value, 2); break;
                    case ControlEvent::Kind::program:
                        ev.push_back ({ at, 2, { static_cast<std::uint8_t> (0xc0 | ch), static_cast<std::uint8_t> (c.value & 0x7f) } });
                        break;
                    case ControlEvent::Kind::channelPressure:
                        ev.push_back ({ at, 2, { static_cast<std::uint8_t> (0xd0 | ch), static_cast<std::uint8_t> (c.value & 0x7f) } });
                        break;
                    case ControlEvent::Kind::pitchBend:
                    {
                        const int v = std::clamp (c.value + 8192, 0, 16383);
                        ev.push_back ({ at, 2, { static_cast<std::uint8_t> (0xe0 | ch), static_cast<std::uint8_t> (v & 0x7f), static_cast<std::uint8_t> (v >> 7) } });
                        break;
                    }
                }
            }
        }
        writeTrack (file, std::move (ev));
    }
    return file.out;
}

void save (const Song& song, const std::filesystem::path& path)
{
    const auto bytes = write (song);
    std::ofstream out (path, std::ios::binary | std::ios::trunc);
    if (! out)
        throw Error ("Can't write " + path.string());
    out.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size()));
    if (! out)
        throw Error ("Write failed for " + path.string());
}

} // namespace arranger::midi_file

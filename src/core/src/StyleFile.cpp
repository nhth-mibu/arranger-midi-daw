#include "arranger/core/Style.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <deque>
#include <fstream>
#include <optional>
#include <utility>

namespace arranger
{

const StyleSection* Style::section (const std::string& n) const
{
    for (const auto& s : sections)
        if (s.name == n)
            return &s;
    return nullptr;
}

StyleSection* Style::section (const std::string& n)
{
    return const_cast<StyleSection*> (std::as_const (*this).section (n));
}

const StyleChannelRule* Style::rule (const std::string& sectionName, int sourceChannel) const
{
    for (const auto& set : ruleSets)
        if (std::find (set.sections.begin(), set.sections.end(), sectionName) != set.sections.end())
            for (const auto& r : set.rules)
                if (r.sourceChannel == sourceChannel)
                    return &r;
    return nullptr;
}

namespace style_file
{

namespace
{
    constexpr int kWriteDivision = 1920; // what Yamaha's own styles use

    class Reader
    {
    public:
        explicit Reader (std::span<const std::uint8_t> b) : bytes (b) {}

        bool atEnd() const noexcept { return pos >= bytes.size(); }
        std::size_t left() const noexcept { return bytes.size() - pos; }
        std::uint8_t u8()
        {
            if (pos >= bytes.size())
                throw Error ("The style file ends too early");
            return bytes[pos++];
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
            throw Error ("Bad variable-length number in the style file");
        }
        std::span<const std::uint8_t> take (std::size_t n)
        {
            if (n > left())
                throw Error ("The style file ends too early");
            auto s = bytes.subspan (pos, n);
            pos += n;
            return s;
        }
        std::string tag()
        {
            const auto t = take (4);
            return std::string (t.begin(), t.end());
        }
        std::span<const std::uint8_t> rest() { return take (left()); }

    private:
        std::span<const std::uint8_t> bytes;
        std::size_t pos = 0;
    };

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
        void bytes (std::span<const std::uint8_t> b) { out.insert (out.end(), b.begin(), b.end()); }
        void chunk (const char* tag, const std::vector<std::uint8_t>& data)
        {
            text (tag);
            u32 (static_cast<std::uint32_t> (data.size()));
            bytes (data);
        }
    };

    std::string trimmed (std::string s)
    {
        while (! s.empty() && (s.back() == ' ' || s.back() == '\0'))
            s.pop_back();
        return s;
    }

    // ---- chord rules (CASM) ------------------------------------------------------------
    using Ntt = StyleTranspose::Ntt;

    StyleTranspose readTranspose (Reader& r, bool sff2)
    {
        StyleTranspose t;
        t.ntr = static_cast<StyleTranspose::Ntr> (std::min<int> (r.u8(), 2));
        const int ntt = r.u8();
        if (sff2)
        {
            t.bass = (ntt & 0x80) != 0;
            t.ntt = static_cast<Ntt> (std::min (ntt & 0x7f, 10));
        }
        else
        {
            // SFF1: 0 bypass, 1 melody, 2 chord, 3 bass, 4 melodic minor, 5 harmonic minor.
            switch (ntt)
            {
                case 0: t.ntt = Ntt::bypass; break;
                case 1: t.ntt = Ntt::melody; break;
                case 3: t.ntt = Ntt::melody; t.bass = true; break;
                case 4: t.ntt = Ntt::melodicMinor; break;
                case 5: t.ntt = Ntt::harmonicMinor; break;
                default: t.ntt = Ntt::chord; break;
            }
        }
        t.highKey = r.u8();
        t.lowLimit = r.u8();
        t.highLimit = r.u8();
        t.rtr = static_cast<StyleTranspose::Rtr> (std::min<int> (r.u8(), 5));
        return t;
    }

    void writeTranspose (Writer& w, const StyleTranspose& t, bool sff2)
    {
        w.u8 (static_cast<int> (sff2 ? t.ntr : std::min (t.ntr, StyleTranspose::Ntr::rootFixed)));
        if (sff2)
            w.u8 (static_cast<int> (t.ntt) | (t.bass ? 0x80 : 0));
        else
        {
            int ntt = 2;
            switch (t.ntt)
            {
                case Ntt::bypass: ntt = 0; break;
                case Ntt::melody: ntt = t.bass ? 3 : 1; break;
                case Ntt::chord: ntt = 2; break;
                case Ntt::melodicMinor: case Ntt::melodicMinor5th: ntt = 4; break;
                default: ntt = 5; break;
            }
            w.u8 (ntt);
        }
        w.u8 (t.highKey);
        w.u8 (t.lowLimit);
        w.u8 (t.highLimit);
        w.u8 (static_cast<int> (t.rtr));
    }

    StyleChannelRule readRule (std::span<const std::uint8_t> data, bool sff2)
    {
        Reader r (data);
        StyleChannelRule c;
        c.sourceChannel = (r.u8() & 0x0f) + 1;
        const auto name = r.take (8);
        c.name = trimmed (std::string (name.begin(), name.end()));
        c.destChannel = (r.u8() & 0x0f) + 1;
        c.editable = r.u8() == 0;
        c.noteMute = static_cast<std::uint16_t> (r.u16() & 0x0fff);
        c.chordMute = 0;
        for (int i = 0; i < 5; ++i)
            c.chordMute = (c.chordMute << 8) | r.u8();
        c.sourceRoot = r.u8() % 12;
        c.sourceChordType = r.u8();
        if (sff2)
        {
            c.middleLow = r.u8();
            c.middleHigh = r.u8();
            c.lowRange = readTranspose (r, true);
            c.transpose = readTranspose (r, true);
            c.highRange = readTranspose (r, true);
        }
        else
            c.transpose = readTranspose (r, false);
        const auto tail = r.rest();
        c.special.assign (tail.begin(), tail.end());
        return c;
    }

    std::vector<std::uint8_t> writeRule (const StyleChannelRule& c, bool sff2)
    {
        Writer w;
        w.u8 (std::clamp (c.sourceChannel, 1, 16) - 1);
        auto name = c.name.substr (0, 8);
        name.resize (8, ' ');
        w.text (name);
        w.u8 (std::clamp (c.destChannel, 1, 16) - 1);
        w.u8 (c.editable ? 0 : 1);
        w.u16 (c.noteMute & 0x0fff);
        for (int i = 4; i >= 0; --i)
            w.u8 (static_cast<int> ((c.chordMute >> (8 * i)) & 0xff));
        w.u8 (c.sourceRoot);
        w.u8 (c.sourceChordType);
        if (sff2)
        {
            w.u8 (c.middleLow);
            w.u8 (c.middleHigh);
            writeTranspose (w, c.lowRange, true);
            writeTranspose (w, c.transpose, true);
            writeTranspose (w, c.highRange, true);
        }
        else
            writeTranspose (w, c.transpose, false);
        if (c.special.empty())
            w.u8 (0); // no special features
        else
            w.bytes (c.special);
        return w.out;
    }

    std::vector<StyleRuleSet> readCasm (std::span<const std::uint8_t> data)
    {
        std::vector<StyleRuleSet> sets;
        Reader r (data);
        while (r.left() >= 8)
        {
            const auto tag = r.tag();
            const auto body = r.take (std::min<std::size_t> (r.u32(), r.left()));
            if (tag != "CSEG")
                continue;
            StyleRuleSet set;
            Reader seg (body);
            while (seg.left() >= 8)
            {
                const auto inner = seg.tag();
                const auto part = seg.take (std::min<std::size_t> (seg.u32(), seg.left()));
                if (inner == "Sdec")
                {
                    std::string name;
                    for (const auto b : part)
                    {
                        if (b == ',')
                        {
                            set.sections.push_back (trimmed (name));
                            name.clear();
                        }
                        else
                            name.push_back (static_cast<char> (b));
                    }
                    if (! trimmed (name).empty())
                        set.sections.push_back (trimmed (name));
                }
                else if ((inner == "Ctab" || inner == "Ctb2") && part.size() >= 26)
                    set.rules.push_back (readRule (part, inner == "Ctb2"));
                else
                {
                    Writer w;
                    w.chunk (std::string (inner).c_str(), { part.begin(), part.end() });
                    set.extra.insert (set.extra.end(), w.out.begin(), w.out.end());
                }
            }
            sets.push_back (std::move (set));
        }
        return sets;
    }

    std::vector<std::uint8_t> writeCasm (const Style& style)
    {
        Writer casm;
        for (const auto& set : style.ruleSets)
        {
            Writer seg;
            std::string names;
            for (const auto& n : set.sections)
                names += (names.empty() ? "" : ",") + n;
            seg.chunk ("Sdec", { names.begin(), names.end() });
            for (const auto& rule : set.rules)
                seg.chunk (style.format >= 2 ? "Ctb2" : "Ctab", writeRule (rule, style.format >= 2));
            seg.bytes (set.extra);
            casm.chunk ("CSEG", seg.out);
        }
        return casm.out;
    }

    // ---- the MIDI part -----------------------------------------------------------------
    struct TimedEvent
    {
        Tick at;
        int order;
        std::vector<std::uint8_t> bytes;
    };

    std::vector<std::uint8_t> meta (int type, std::span<const std::uint8_t> data)
    {
        Writer w;
        w.u8 (0xff);
        w.u8 (type);
        w.varLen (static_cast<std::uint32_t> (data.size()));
        w.bytes (data);
        return w.out;
    }

    std::vector<std::uint8_t> meta (int type, const std::string& text)
    {
        return meta (type, std::span<const std::uint8_t> (reinterpret_cast<const std::uint8_t*> (text.data()), text.size()));
    }
}

Style read (std::span<const std::uint8_t> bytes)
{
    Reader r (bytes);
    if (r.left() < 14 || r.tag() != "MThd")
        throw Error ("This is not a style file (no MIDI header)");
    const auto headerLen = r.u32();
    if (headerLen < 6)
        throw Error ("The style file header is damaged");
    r.u16(); // format: a style is format 0
    r.u16(); // number of tracks
    const auto division = r.u16();
    if ((division & 0x8000) != 0 || division == 0)
        throw Error ("This style file uses a time format the app can't read");
    r.take (headerLen - 6);

    if (r.left() < 8 || r.tag() != "MTrk")
        throw Error ("The style file has no MIDI track");
    Reader track (r.take (std::min<std::size_t> (r.u32(), r.left())));

    const auto toTicks = [division] (std::uint64_t t) {
        return static_cast<Tick> (std::llround (static_cast<double> (t) * static_cast<double> (kTicksPerQuarter) / static_cast<double> (division)));
    };

    Style style;
    style.format = 0;
    style.name.clear();
    bool haveTempo = false, haveMeter = false;

    struct OpenSection
    {
        std::string name;
        Tick start = 0;
        std::map<int, Clip> parts;
        std::vector<StyleEvent> events;
        std::map<int, std::array<std::deque<std::pair<Tick, std::uint8_t>>, 128>> open;
    };
    std::optional<OpenSection> current; // empty while in the setup bar
    bool inSetup = true;

    const auto closeSection = [&] (Tick at) {
        if (! current)
            return;
        StyleSection s;
        s.name = current->name;
        s.length = std::max<Tick> (0, at - current->start);
        for (auto& [channel, clip] : current->parts)
        {
            // Notes still held at the end of the section stop there.
            for (int pitch = 0; pitch < 128; ++pitch)
                for (const auto& [start, velocity] : current->open[channel][static_cast<std::size_t> (pitch)])
                    clip.notes.push_back ({ start, std::max<Tick> (1, s.length - start), static_cast<std::uint8_t> (pitch), velocity });
            std::sort (clip.notes.begin(), clip.notes.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
            clip.length = s.length;
            clip.name = s.name;
            if (! clip.notes.empty() || ! clip.controls.empty())
                s.parts[channel] = std::move (clip);
        }
        s.events = std::move (current->events);
        if (s.length > 0)
            style.sections.push_back (std::move (s));
        current.reset();
    };

    std::uint64_t fileTick = 0;
    std::uint8_t running = 0;
    Tick now = 0;
    while (! track.atEnd())
    {
        fileTick += track.varLen();
        now = toTicks (fileTick);
        std::uint8_t status = track.u8();

        if (status == 0xff)
        {
            const int type = track.u8();
            const auto data = track.take (track.varLen());
            const std::string text (data.begin(), data.end());
            if (type == 0x2f)
                break;
            if (type == 0x06) // marker
            {
                if (text == "SFF1" || text == "SFF2")
                    style.format = text == "SFF2" ? 2 : 1;
                else if (text == "SInt")
                    inSetup = true;
                else
                {
                    closeSection (now);
                    inSetup = false;
                    current.emplace();
                    current->name = trimmed (text);
                    current->start = now;
                }
            }
            else if (type == 0x03 && style.name.empty())
                style.name = trimmed (text);
            else if (type == 0x51 && data.size() == 3 && ! haveTempo)
            {
                const double micros = static_cast<double> ((data[0] << 16) | (data[1] << 8) | data[2]);
                if (micros > 0.0)
                    style.tempo = std::round (60000000.0 / micros * 1000.0) / 1000.0;
                haveTempo = true;
            }
            else if (type == 0x58 && data.size() >= 2 && ! haveMeter)
            {
                style.timeSignature = { data[0], 1 << std::min<int> (data[1], 5) };
                haveMeter = true;
            }
            else if (current && ! inSetup)
                current->events.push_back ({ now - current->start, meta (type, data) }); // a tempo change, a text
            else if (type != 0x03 && type != 0x51 && type != 0x58)
                style.setup.push_back ({ now, meta (type, data) });
            continue;
        }
        if (status == 0xf0 || status == 0xf7)
        {
            const auto data = track.take (track.varLen());
            StyleEvent e { now, { status } };
            e.bytes.insert (e.bytes.end(), data.begin(), data.end());
            if (inSetup || ! current)
                style.setup.push_back (std::move (e));
            else
            {
                e.at -= current->start;
                current->events.push_back (std::move (e));
            }
            continue;
        }

        std::uint8_t d1 = 0;
        if (status & 0x80)
        {
            running = status;
            d1 = track.u8();
        }
        else
        {
            if (running == 0)
                throw Error ("The style file has data without a status byte");
            d1 = status;
            status = running;
        }
        const int kind = status & 0xf0;
        const int channel = (status & 0x0f) + 1;
        const std::uint8_t d2 = (kind == 0xc0 || kind == 0xd0) ? std::uint8_t (0) : track.u8();

        if (! current)
        {
            // The setup bar: keep the messages as they are.
            StyleEvent e { now, { status, d1 } };
            if (kind != 0xc0 && kind != 0xd0)
                e.bytes.push_back (d2);
            style.setup.push_back (std::move (e));
            continue;
        }

        auto& clip = current->parts[channel];
        const Tick at = now - current->start;
        if (kind == 0x90 && d2 > 0)
            current->open[channel][d1].push_back ({ at, d2 });
        else if (kind == 0x80 || kind == 0x90)
        {
            auto& waiting = current->open[channel][d1];
            if (! waiting.empty())
            {
                const auto [start, velocity] = waiting.front();
                waiting.pop_front();
                clip.notes.push_back ({ start, std::max<Tick> (1, at - start), d1, velocity });
            }
        }
        else if (kind == 0xb0)
            clip.controls.push_back ({ at, ControlEvent::Kind::controller, d1, d2 });
        else if (kind == 0xc0)
            clip.controls.push_back ({ at, ControlEvent::Kind::program, 0, d1 });
        else if (kind == 0xd0)
            clip.controls.push_back ({ at, ControlEvent::Kind::channelPressure, 0, d1 });
        else if (kind == 0xe0)
            clip.controls.push_back ({ at, ControlEvent::Kind::pitchBend, 0, ((d2 << 7) | d1) - 8192 });
    }
    closeSection (now);

    if (style.format == 0)
        throw Error ("This MIDI file is not a Yamaha style (no SFF1 or SFF2 marker)");
    if (style.name.empty())
        style.name = "Style";
    if (! style.timeSignature.isValid())
        style.timeSignature = {};

    // After the track: CASM (chord rules), then other chunks we keep as they are.
    while (r.left() >= 8)
    {
        const auto tag = r.tag();
        const auto length = r.u32();
        const auto body = r.take (std::min<std::size_t> (length, r.left()));
        if (tag == "CASM")
            style.ruleSets = readCasm (body);
        else
        {
            Writer w;
            w.text (tag);
            w.u32 (length);
            w.bytes (body);
            style.extra.insert (style.extra.end(), w.out.begin(), w.out.end());
        }
    }
    return style;
}

Style load (const std::filesystem::path& path)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        throw Error ("Can't open the style file");
    const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    auto style = read (bytes);
    if (style.name == "Style")
        style.name = path.stem().string();
    return style;
}

bool hasStyleExtension (const std::string& fileName)
{
    const auto dot = fileName.rfind ('.');
    if (dot == std::string::npos)
        return false;
    std::string ext = fileName.substr (dot);
    for (auto& c : ext)
        c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return std::any_of (std::begin (kExtensions), std::end (kExtensions), [&ext] (const char* e) { return ext == e; });
}

std::vector<std::uint8_t> write (const Style& style)
{
    // A SysEx goes in the file with its length after the F0; other messages go as they are.
    const auto sysExOrPlain = [] (const StyleEvent& e) {
        if (e.bytes.size() <= 1 || (e.bytes[0] != 0xf0 && e.bytes[0] != 0xf7))
            return e.bytes;
        Writer w;
        w.u8 (e.bytes[0]);
        w.varLen (static_cast<std::uint32_t> (e.bytes.size() - 1));
        w.bytes (std::span (e.bytes).subspan (1));
        return w.out;
    };
    const auto scale = [] (Tick t) { return t * kWriteDivision / kTicksPerQuarter; };
    std::vector<TimedEvent> events;
    const auto add = [&] (Tick at, int order, std::vector<std::uint8_t> bytes) { events.push_back ({ scale (at), order, std::move (bytes) }); };

    // Order at one time: 0 header, 1 markers, 2 setup, 3 note-offs, 4 controls, 5 note-ons.
    const int log2 = static_cast<int> (std::lround (std::log2 (std::max (1, style.timeSignature.denominator))));
    add (0, 0, meta (0x58, std::vector<std::uint8_t> { static_cast<std::uint8_t> (style.timeSignature.numerator), static_cast<std::uint8_t> (log2), 24, 8 }));
    const auto micros = static_cast<std::uint32_t> (std::lround (60000000.0 / std::clamp (style.tempo, 5.0, 500.0)));
    add (0, 0, meta (0x51, std::vector<std::uint8_t> { static_cast<std::uint8_t> (micros >> 16), static_cast<std::uint8_t> (micros >> 8), static_cast<std::uint8_t> (micros) }));
    add (0, 0, meta (0x03, style.name));
    add (0, 1, meta (0x06, std::string (style.format >= 2 ? "SFF2" : "SFF1")));
    add (0, 1, meta (0x06, std::string ("SInt")));

    // The setup bar is one bar long, or longer when its messages need more room.
    const Tick bar = style.timeSignature.ticksPerBar();
    Tick setupEnd = bar;
    for (const auto& e : style.setup)
    {
        add (e.at, 2, sysExOrPlain (e));
        while (e.at >= setupEnd)
            setupEnd += bar;
    }

    Tick start = setupEnd;
    for (const auto& section : style.sections)
    {
        add (start, 1, meta (0x06, section.name));
        for (const auto& e : section.events)
            if (e.at < section.length && ! e.bytes.empty())
                add (start + e.at, 2, sysExOrPlain (e));
        for (const auto& [channel, clip] : section.parts)
        {
            const auto status = [channel] (int kind) { return static_cast<std::uint8_t> (kind | (std::clamp (channel, 1, 16) - 1)); };
            for (const auto& n : clip.notes)
            {
                if (n.start >= section.length)
                    continue;
                add (start + n.start, 5, { status (0x90), n.pitch, std::max<std::uint8_t> (1, n.velocity) });
                add (start + std::min (n.end(), section.length), 3, { status (0x80), n.pitch, 0 });
            }
            for (const auto& c : clip.controls)
            {
                if (c.at >= section.length)
                    continue;
                const auto v = static_cast<std::uint8_t> (std::clamp (c.value, 0, 127));
                switch (c.kind)
                {
                    case ControlEvent::Kind::controller: add (start + c.at, 4, { status (0xb0), c.number, v }); break;
                    case ControlEvent::Kind::program: add (start + c.at, 4, { status (0xc0), v }); break;
                    case ControlEvent::Kind::channelPressure: add (start + c.at, 4, { status (0xd0), v }); break;
                    case ControlEvent::Kind::pitchBend:
                    {
                        const int bend = std::clamp (c.value + 8192, 0, 16383);
                        add (start + c.at, 4, { status (0xe0), static_cast<std::uint8_t> (bend & 0x7f), static_cast<std::uint8_t> (bend >> 7) });
                        break;
                    }
                }
            }
        }
        start += section.length;
    }

    std::stable_sort (events.begin(), events.end(), [] (const TimedEvent& a, const TimedEvent& b) { return a.at != b.at ? a.at < b.at : a.order < b.order; });
    Writer track;
    Tick last = 0;
    for (const auto& e : events)
    {
        track.varLen (static_cast<std::uint32_t> (std::max<Tick> (0, e.at - last)));
        last = std::max (last, e.at);
        track.bytes (e.bytes);
    }
    track.varLen (static_cast<std::uint32_t> (std::max<Tick> (0, scale (start) - last)));
    track.u8 (0xff);
    track.u8 (0x2f);
    track.u8 (0);

    Writer file;
    file.text ("MThd");
    file.u32 (6);
    file.u16 (0);
    file.u16 (1);
    file.u16 (kWriteDivision);
    file.chunk ("MTrk", track.out);
    if (! style.ruleSets.empty())
        file.chunk ("CASM", writeCasm (style));
    file.bytes (style.extra);
    return file.out;
}

void save (const Style& style, const std::filesystem::path& path)
{
    const auto bytes = write (style);
    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream out (temp, std::ios::binary | std::ios::trunc);
        if (! out)
            throw Error ("Can't write the style file");
        out.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size()));
        if (! out)
            throw Error ("Can't write the style file");
    }
    std::error_code ec;
    std::filesystem::rename (temp, path, ec);
    if (ec)
        throw Error ("Can't write the style file");
}

} // namespace style_file

} // namespace arranger

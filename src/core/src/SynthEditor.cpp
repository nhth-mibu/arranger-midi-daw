#include "arranger/core/SynthEditor.h"

#include "SynthEditorYaml.h"

#include "arranger/core/DeviceProfile.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace arranger
{

namespace
{
    std::string lower (std::string s)
    {
        std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return s;
    }

    /** A template split into tokens: "F0", "{dev}", ... */
    std::vector<std::string> tokens (const std::string& text)
    {
        std::vector<std::string> out;
        std::istringstream in (text);
        std::string t;
        while (in >> t)
            out.push_back (lower (t));
        return out;
    }

    int hexByte (const std::string& t)
    {
        auto s = t;
        if (s.size() > 2 && s[0] == '0' && s[1] == 'x')
            s = s.substr (2);
        if (! s.empty() && s.back() == 'h')
            s.pop_back();
        if (s.empty() || s.size() > 2 || ! std::all_of (s.begin(), s.end(), [] (unsigned char c) { return std::isxdigit (c); }))
            return -1;
        return std::stoi (s, nullptr, 16);
    }

    std::uint8_t deviceId (const SysExFormat& f, int channel)
    {
        return static_cast<std::uint8_t> (f.device >= 0 ? f.device : std::clamp (channel, 1, 16) - 1);
    }

    /** Builds a message from a template. `fields` gives the bytes of {addr}, {value} and {size}. */
    MidiBytes build (const std::string& pattern, const std::map<std::string, MidiBytes>& fields,
                     SysExFormat::Checksum checksum = SysExFormat::Checksum::roland)
    {
        MidiBytes out;
        std::size_t sumFrom = std::string::npos;
        for (const auto& t : tokens (pattern))
        {
            if (t == "{sum}")
            {
                int sum = 0;
                for (std::size_t i = sumFrom == std::string::npos ? 0 : sumFrom; i < out.size(); ++i)
                    sum += out[i];
                if (checksum == SysExFormat::Checksum::roland)
                    out.push_back (static_cast<std::uint8_t> ((128 - (sum & 0x7f)) & 0x7f));
                else if (checksum == SysExFormat::Checksum::sum7)
                    out.push_back (static_cast<std::uint8_t> (sum & 0x7f));
                else if (checksum == SysExFormat::Checksum::bitXor)
                {
                    int x = 0;
                    for (std::size_t i = sumFrom == std::string::npos ? 0 : sumFrom; i < out.size(); ++i)
                        x ^= out[i];
                    out.push_back (static_cast<std::uint8_t> (x & 0x7f));
                }
                else
                    out.push_back (0);
                continue;
            }
            if (t.size() > 2 && t.front() == '{')
            {
                const auto it = fields.find (t);
                if (it == fields.end())
                    throw device_profile::Error ("the SysEx pattern has an unknown field " + t);
                if (t == "{addr}")
                    sumFrom = out.size();
                out.insert (out.end(), it->second.begin(), it->second.end());
                continue;
            }
            const int b = hexByte (t);
            if (b < 0)
                throw device_profile::Error ("\"" + t + "\" in a SysEx pattern is not a hex byte or a {field}");
            out.push_back (static_cast<std::uint8_t> (b));
        }
        return out;
    }

    MidiBytes encodeValue (const SynthParameter& p, int value)
    {
        const int bytes = std::clamp (p.valueBytes, 1, 4);
        const int bits = (p.nibble ? 4 : 7) * bytes;
        unsigned int u = 0;
        if (p.signedValue)
            u = static_cast<unsigned int> (value) & ((1u << bits) - 1u);
        else
            u = static_cast<unsigned int> (std::max (0, value));
        MidiBytes out;
        for (int i = bytes - 1; i >= 0; --i)
            out.push_back (static_cast<std::uint8_t> ((u >> (i * (p.nibble ? 4 : 7))) & (p.nibble ? 0x0f : 0x7f)));
        return out;
    }

    bool decodeValue (const SynthParameter& p, const MidiBytes& data, std::size_t at, int& value)
    {
        const int bytes = std::clamp (p.valueBytes, 1, 4);
        if (at + static_cast<std::size_t> (bytes) > data.size())
            return false;
        unsigned int u = 0;
        for (int i = 0; i < bytes; ++i)
            u = (u << (p.nibble ? 4 : 7)) | (data[at + static_cast<std::size_t> (i)] & (p.nibble ? 0x0f : 0x7f));
        const int bits = (p.nibble ? 4 : 7) * bytes;
        if (p.signedValue && (u & (1u << (bits - 1))))
            value = static_cast<int> (u) - static_cast<int> (1u << bits);
        else
            value = static_cast<int> (u);
        return true;
    }

    std::string trimRight (std::string s)
    {
        while (! s.empty() && (s.back() == ' ' || s.back() == '\0'))
            s.pop_back();
        return s;
    }
}

// ---- SynthParameter ----------------------------------------------------------------------

int SynthParameter::choiceValue (std::size_t i) const
{
    if (i < values.size())
        return values[i];
    return min + static_cast<int> (i);
}

int SynthParameter::choiceIndex (int value) const
{
    for (std::size_t i = 0; i < choices.size(); ++i)
        if (choiceValue (i) == value)
            return static_cast<int> (i);
    return -1;
}

std::string SynthParameter::display (int value) const
{
    if (isChoice())
    {
        const int i = choiceIndex (value);
        return i >= 0 ? choices[static_cast<std::size_t> (i)] : std::to_string (value);
    }
    if (noteFrom >= 0)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int note = noteFrom + value - min;
        return std::string (names[((note % 12) + 12) % 12]) + std::to_string (note / 12 - 1);
    }
    const int shown = value + offset;
    std::string text = (offset < 0 && shown > 0 && min + offset < 0 ? "+" : "") + std::to_string (shown);
    if (! unit.empty())
        text += " " + unit;
    return text;
}

int SynthParameter::clamp (int value) const
{
    if (isChoice() && ! values.empty())
    {
        int best = values.front();
        for (const int v : values)
            if (std::abs (v - value) < std::abs (best - value))
                best = v;
        return best;
    }
    return std::clamp (value, min, max);
}

// ---- SynthEditor --------------------------------------------------------------------------

std::size_t SynthEditor::parameterCount() const noexcept
{
    std::size_t n = 0;
    for (const auto& p : pages)
        n += p.params.size();
    return n;
}

const SynthParameter* SynthEditor::find (const std::string& name, const SynthPage** pageOut) const
{
    const auto wanted = lower (name);
    const auto slash = wanted.find ('/');
    const auto pageName = slash == std::string::npos ? std::string() : wanted.substr (0, slash);
    const auto paramName = slash == std::string::npos ? wanted : wanted.substr (slash + 1);
    for (const auto& page : pages)
    {
        if (! pageName.empty() && lower (page.name) != pageName)
            continue;
        for (const auto& p : page.params)
            if (lower (p.name) == paramName)
            {
                if (pageOut != nullptr)
                    *pageOut = &page;
                return &p;
            }
    }
    return nullptr;
}

namespace synth_edit
{

long addressValue (const MidiBytes& a)
{
    long v = 0;
    for (const auto b : a)
        v = v * 128 + (b & 0x7f);
    return v;
}

MidiBytes addressBytes (long value, int bytes)
{
    MidiBytes out (static_cast<std::size_t> (std::max (bytes, 1)), 0);
    for (int i = bytes - 1; i >= 0; --i)
    {
        out[static_cast<std::size_t> (i)] = static_cast<std::uint8_t> (value & 0x7f);
        value >>= 7;
    }
    return out;
}

MidiBytes addAddress (const MidiBytes& a, const MidiBytes& b)
{
    const int n = static_cast<int> (std::max (a.size(), b.size()));
    return addressBytes (addressValue (a) + addressValue (b), n);
}

MidiBytes multiplyAddress (const MidiBytes& a, int n)
{
    return addressBytes (addressValue (a) * n, static_cast<int> (a.size()));
}

std::vector<MidiBytes> messages (const SynthEditor& editor, const SynthParameter& p, int value, int channel)
{
    value = p.clamp (value);
    const auto status = static_cast<std::uint8_t> (0xb0 | ((std::clamp (channel, 1, 16) - 1) & 0x0f));
    const auto v = static_cast<std::uint8_t> (std::clamp (value, 0, 127));
    switch (p.kind)
    {
        case SynthParameter::Kind::cc:
            return { { status, static_cast<std::uint8_t> (p.number & 0x7f), v } };
        case SynthParameter::Kind::nrpn:
        case SynthParameter::Kind::rpn:
        {
            const bool n = p.kind == SynthParameter::Kind::nrpn;
            return { { status, static_cast<std::uint8_t> (n ? 99 : 101), static_cast<std::uint8_t> ((p.number >> 7) & 0x7f) },
                     { status, static_cast<std::uint8_t> (n ? 98 : 100), static_cast<std::uint8_t> (p.number & 0x7f) },
                     { status, 6, v } };
        }
        case SynthParameter::Kind::sysex:
        {
            if (editor.sysex.empty())
                return {};
            return { build (editor.sysex.write, { { "{dev}", { deviceId (editor.sysex, channel) } },
                                                  { "{addr}", p.address },
                                                  { "{value}", encodeValue (p, value) } }, editor.sysex.checksum) };
        }
    }
    return {};
}

std::vector<MidiBytes> readRequests (const SynthEditor& editor, int channel)
{
    std::vector<MidiBytes> out;
    if (editor.sysex.read.empty())
        return out;
    for (const auto& page : editor.pages)
    {
        long lo = -1, hi = -1;
        for (const auto& p : page.params)
            if (p.kind == SynthParameter::Kind::sysex)
            {
                const long a = addressValue (p.address);
                lo = lo < 0 ? a : std::min (lo, a);
                hi = std::max (hi, a + static_cast<long> (p.valueBytes) - 1);
            }
        if (lo < 0)
            continue;
        out.push_back (build (editor.sysex.read, { { "{dev}", { deviceId (editor.sysex, channel) } },
                                                   { "{addr}", addressBytes (lo, editor.sysex.addressBytes) },
                                                   { "{size}", addressBytes (hi - lo + 1, editor.sysex.sizeBytes) } }, editor.sysex.checksum));
    }
    return out;
}

bool parseWrite (const SynthEditor& editor, const MidiBytes& m, int channel, MidiBytes& address, MidiBytes& data)
{
    if (editor.sysex.empty() || m.size() < 4 || m.front() != 0xf0)
        return false;
    const auto t = tokens (editor.sysex.write);
    const auto valueAt = std::find (t.begin(), t.end(), "{value}");
    if (valueAt == t.end())
        return false;
    const auto after = static_cast<std::size_t> (t.end() - valueAt - 1); // bytes after the data, e.g. {sum} F7
    std::size_t pos = 0;
    for (auto it = t.begin(); it != valueAt; ++it)
    {
        if (*it == "{addr}")
        {
            const auto n = static_cast<std::size_t> (editor.sysex.addressBytes);
            if (pos + n > m.size())
                return false;
            address.assign (m.begin() + static_cast<long> (pos), m.begin() + static_cast<long> (pos + n));
            pos += n;
            continue;
        }
        if (pos >= m.size())
            return false;
        if (*it == "{dev}")
        {
            // Any device id is fine when the device has a fixed one; else it must be our channel's.
            if (editor.sysex.device < 0 && m[pos] != deviceId (editor.sysex, channel) && m[pos] != 0x7f)
                return false;
            ++pos;
            continue;
        }
        if (it->front() == '{')
        {
            ++pos;
            continue;
        }
        if (hexByte (*it) != m[pos])
            return false;
        ++pos;
    }
    if (m.size() < pos + after + 1)
        return false;
    data.assign (m.begin() + static_cast<long> (pos), m.end() - static_cast<long> (after));
    return ! address.empty();
}

Listener::Listener (const SynthEditor& e, int ch) : editor (e), channel (ch) {}

std::vector<Change> Listener::take (const MidiBytes& m)
{
    std::vector<Change> out;
    const auto addMatches = [&] (SynthParameter::Kind kind, int number, int value) {
        for (std::size_t pg = 0; pg < editor.pages.size(); ++pg)
        {
            const auto& params = editor.pages[pg].params;
            for (std::size_t i = 0; i < params.size(); ++i)
            {
                const auto& p = params[i];
                if (p.kind != kind || p.number != number)
                    continue;
                if (! p.values.empty() && p.choiceIndex (value) < 0)
                    continue; // one NRPN, several switches: this value belongs to another
                out.push_back ({ pg, i, value });
            }
        }
    };

    if (m.empty())
        return out;
    if (m.front() == 0xf0)
    {
        MidiBytes address, data;
        if (! parseWrite (editor, m, channel, address, data))
            return out;
        const long first = addressValue (address);
        for (std::size_t pg = 0; pg < editor.pages.size(); ++pg)
        {
            const auto& params = editor.pages[pg].params;
            for (std::size_t i = 0; i < params.size(); ++i)
            {
                const auto& p = params[i];
                if (p.kind != SynthParameter::Kind::sysex)
                    continue;
                const long at = addressValue (p.address) - first;
                int value = 0;
                if (at >= 0 && decodeValue (p, data, static_cast<std::size_t> (at), value))
                    out.push_back ({ pg, i, value });
            }
        }
        return out;
    }
    if (m.size() < 3 || (m[0] & 0xf0) != 0xb0 || (m[0] & 0x0f) != ((std::clamp (channel, 1, 16) - 1) & 0x0f))
        return out;

    const int cc = m[1], v = m[2];
    switch (cc)
    {
        case 99: nrpnMsb = v; rpnActive = false; return out;
        case 98: nrpnLsb = v; rpnActive = false; return out;
        case 101: rpnMsb = v; rpnActive = true; return out;
        case 100: rpnLsb = v; rpnActive = true; return out;
        case 6:
        {
            if (rpnActive && rpnMsb >= 0 && rpnLsb >= 0 && ! (rpnMsb == 127 && rpnLsb == 127))
                addMatches (SynthParameter::Kind::rpn, rpnMsb * 128 + rpnLsb, v);
            else if (! rpnActive && nrpnLsb >= 0)
                addMatches (SynthParameter::Kind::nrpn, std::max (nrpnMsb, 0) * 128 + nrpnLsb, v);
            if (out.empty())
                addMatches (SynthParameter::Kind::cc, cc, v);
            return out;
        }
        default:
            addMatches (SynthParameter::Kind::cc, cc, v);
            return out;
    }
}

// ---- NameReader -----------------------------------------------------------------------------

NameReader::NameReader (const SynthEditor& e, int ch) : editor (e), channel (ch) {}

int NameReader::expected() const noexcept
{
    if (! editor.names)
        return 0;
    return editor.names->count * static_cast<int> (std::max<std::size_t> (1, editor.names->banks.size()));
}

std::optional<NameReader::Step> NameReader::next()
{
    if (! editor.names || editor.sysex.read.empty())
        return std::nullopt;
    const auto& n = *editor.names;
    const auto bankCount = std::max<std::size_t> (1, n.banks.size());
    if (bank >= bankCount)
        return std::nullopt;

    const auto status = static_cast<std::uint8_t> ((std::clamp (channel, 1, 16) - 1) & 0x0f);
    if (! n.banks.empty() && ! bankSelected)
    {
        // Select the bank (its first sound), one MIDI message at a time.
        const auto& b = n.banks[bank];
        Step s;
        if (bankStage == 0)
        {
            bankStage = 1;
            if (b.msb >= 0)
            {
                s.bytes = { static_cast<std::uint8_t> (0xb0 | status), 0, static_cast<std::uint8_t> (b.msb) };
                return s;
            }
        }
        if (bankStage == 1)
        {
            bankStage = 2;
            if (b.lsb >= 0)
            {
                s.bytes = { static_cast<std::uint8_t> (0xb0 | status), 32, static_cast<std::uint8_t> (b.lsb) };
                return s;
            }
        }
        s.bytes = { static_cast<std::uint8_t> (0xc0 | status), 0 };
        s.waitMs = bank == 0 ? 0 : 400; // let the last answers of the previous bank come in
        bankSelected = true;
        bankStage = 0;
        return s;
    }

    Step s;
    requestBank = bank;
    const auto address = addAddress (n.first, multiplyAddress (n.stride, index));
    s.bytes = build (editor.sysex.read, { { "{dev}", { deviceId (editor.sysex, channel) } },
                                          { "{addr}", addressBytes (addressValue (address), editor.sysex.addressBytes) },
                                          { "{size}", addressBytes (n.length, editor.sysex.sizeBytes) } }, editor.sysex.checksum);
    s.waitMs = index == 0 && ! n.banks.empty() ? 400 : 40;
    if (++index >= n.count)
    {
        index = 0;
        ++bank;
        bankSelected = false;
    }
    return s;
}

void NameReader::receive (const MidiBytes& m)
{
    if (! editor.names)
        return;
    MidiBytes address, data;
    if (! parseWrite (editor, m, channel, address, data))
        return;
    const auto& n = *editor.names;
    const long step = addressValue (n.stride);
    const long at = addressValue (address) - addressValue (n.first);
    if (step <= 0 || at < 0 || at % step != 0)
        return;
    const long i = at / step;
    if (i >= n.count)
        return;

    std::string name;
    for (std::size_t c = 0; c < data.size() && c < static_cast<std::size_t> (n.length); ++c)
    {
        const auto b = data[c];
        if (! n.charset.empty())
            name += b < n.charset.size() ? n.charset[b] : '?';
        else
            name += b >= 32 && b < 127 ? static_cast<char> (b) : ' ';
    }
    name = trimRight (name);
    while (! name.empty() && name.front() == ' ')
        name.erase (name.begin());

    // The answer belongs to the bank whose requests were sent last.
    const std::size_t b = requestBank;
    Found f;
    f.name = name.empty() ? "(no name)" : name;
    f.program = static_cast<int> (i);
    if (b < n.banks.size())
    {
        f.group = n.banks[b].group;
        f.msb = n.banks[b].msb;
        f.lsb = n.banks[b].lsb;
    }
    else
        f.group = "From the synth";
    std::erase_if (names, [&] (const Found& old) { return old.group == f.group && old.program == f.program; });
    names.push_back (std::move (f));
}

} // namespace synth_edit

// ---- YAML -------------------------------------------------------------------------------------

namespace
{
    [[noreturn]] void fail (const YAML::Node& node, const std::string& what)
    {
        const auto mark = node.Mark();
        std::ostringstream msg;
        if (mark.line >= 0)
            msg << "Line " << (mark.line + 1) << ": ";
        msg << what;
        throw device_profile::Error (msg.str());
    }

    MidiBytes hexBytes (const YAML::Node& n, const char* what)
    {
        MidiBytes out;
        std::istringstream in (n.as<std::string>());
        std::string t;
        while (in >> t)
        {
            const int b = hexByte (lower (t));
            if (b < 0 || b > 127)
                fail (n, std::string (what) + ": \"" + t + "\" is not a hex byte 00-7F");
            out.push_back (static_cast<std::uint8_t> (b));
        }
        if (out.empty())
            fail (n, std::string (what) + " is empty");
        return out;
    }

    int intIn (const YAML::Node& n, const char* what, int lo, int hi)
    {
        int v = 0;
        try
        {
            v = n.as<int>();
        }
        catch (const YAML::Exception&)
        {
            fail (n, std::string (what) + " must be a number");
        }
        if (v < lo || v > hi)
            fail (n, std::string (what) + " must be " + std::to_string (lo) + "-" + std::to_string (hi));
        return v;
    }

    SynthParameter parseParam (const YAML::Node& n, const MidiBytes& base, const SysExFormat& sysex)
    {
        if (! n.IsMap())
            fail (n, "a parameter is a map like { name: Cutoff, cc: 74 }");
        SynthParameter p;
        p.name = n["name"].as<std::string> ("");
        if (p.name.empty())
            fail (n, "a parameter needs a name");

        int kinds = 0;
        if (const auto cc = n["cc"])
        {
            p.kind = SynthParameter::Kind::cc;
            p.number = intIn (cc, "cc", 0, 127);
            ++kinds;
        }
        for (const char* key : { "nrpn", "rpn" })
            if (const auto v = n[key])
            {
                p.kind = std::string (key) == "nrpn" ? SynthParameter::Kind::nrpn : SynthParameter::Kind::rpn;
                if (v.IsSequence())
                {
                    if (v.size() != 2)
                        fail (v, std::string (key) + " is a number or [MSB, LSB]");
                    p.number = intIn (v[0], "MSB", 0, 127) * 128 + intIn (v[1], "LSB", 0, 127);
                }
                else
                    p.number = intIn (v, key, 0, 16383);
                ++kinds;
            }
        if (const auto at = n["at"])
        {
            if (sysex.empty())
                fail (at, "\"at\" needs a sysex: part in the editor");
            p.kind = SynthParameter::Kind::sysex;
            const auto offset = hexBytes (at, "at");
            p.address = synth_edit::addressBytes (synth_edit::addressValue (base) + synth_edit::addressValue (offset), sysex.addressBytes);
            ++kinds;
        }
        if (kinds != 1)
            fail (n, "\"" + p.name + "\" needs one of cc, nrpn, rpn or at");

        if (const auto b = n["bytes"])
            p.valueBytes = intIn (b, "bytes", 1, 4);
        if (const auto f = n["format"])
        {
            const auto format = lower (f.as<std::string>());
            if (format == "nibble")
                p.nibble = true;
            else if (format == "signed")
                p.signedValue = true;
            else if (format == "14bit" || format == "14-bit")
                p.valueBytes = 2;
            else
                fail (f, "format is nibble, signed or 14bit");
        }

        if (const auto c = n["choices"])
        {
            if (! c.IsSequence() || c.size() == 0)
                fail (c, "choices is a list of names");
            for (const auto& x : c)
                p.choices.push_back (x.as<std::string>());
        }
        if (const auto vs = n["values"])
        {
            if (! vs.IsSequence() || vs.size() != p.choices.size())
                fail (vs, "values needs one number for each choice");
            for (const auto& x : vs)
                p.values.push_back (intIn (x, "a value", 0, 127));
        }
        const auto readRange = [&] (const YAML::Node& node, const char* what) {
            int v = 0;
            try
            {
                v = node.as<int>();
            }
            catch (const YAML::Exception&)
            {
                fail (node, std::string (what) + " must be a number");
            }
            return v;
        };
        if (n["min"])
            p.min = readRange (n["min"], "min");
        if (n["max"])
            p.max = readRange (n["max"], "max");
        else if (p.isChoice())
            p.max = p.min + static_cast<int> (p.choices.size()) - 1;
        if (! p.values.empty())
        {
            p.min = *std::min_element (p.values.begin(), p.values.end());
            p.max = *std::max_element (p.values.begin(), p.values.end());
        }
        if (p.min > p.max)
            fail (n, "min is more than max");
        if (p.kind == SynthParameter::Kind::sysex)
        {
            const int bits = (p.nibble ? 4 : 7) * p.valueBytes;
            if (p.signedValue)
            {
                if (p.min < -(1 << (bits - 1)) || p.max > (1 << (bits - 1)) - 1)
                    fail (n, "a signed value must fit in " + std::to_string (bits) + " bits");
            }
            else if (p.min < 0 || p.max >= (1 << bits))
                fail (n, "a value must fit in " + std::to_string (bits) + " bits");
        }
        else if (p.kind != SynthParameter::Kind::nrpn && p.kind != SynthParameter::Kind::rpn && p.max > 127)
            fail (n, "max must be 127 or less");
        p.offset = n["offset"] ? intIn (n["offset"], "offset", -16383, 16383) : 0;
        // Without a default: the first choice, or 0 as shown (the middle of a -64..+63 setting), or min.
        if (n["default"])
            p.defaultValue = intIn (n["default"], "default", p.min, p.max);
        else if (p.isChoice())
            p.defaultValue = p.choiceValue (0);
        else
            p.defaultValue = p.min + p.offset < 0 && p.max + p.offset >= 0 ? -p.offset : p.min;
        p.unit = n["unit"].as<std::string> ("");
        if (const auto nf = n["noteFrom"])
        {
            // "C1" -> MIDI 24 (C4 = 60)
            const auto text = nf.as<std::string>();
            static const std::string letters = "C D EF G A B";
            const auto pos = text.empty() ? std::string::npos : letters.find (static_cast<char> (std::toupper (static_cast<unsigned char> (text[0]))));
            if (pos == std::string::npos || text[0] == ' ')
                fail (nf, "noteFrom is a note name like C1");
            int note = static_cast<int> (pos);
            std::size_t i = 1;
            if (i < text.size() && text[i] == '#') { ++note; ++i; }
            else if (i < text.size() && text[i] == 'b') { --note; ++i; }
            try
            {
                p.noteFrom = note + 12 * (std::stoi (text.substr (i)) + 1);
            }
            catch (const std::exception&)
            {
                fail (nf, "noteFrom is a note name like C1");
            }
        }
        return p;
    }
}

namespace synth_edit
{
SynthEditor parseEditor (const YAML::Node& e)
{
    SynthEditor editor;
    if (! e.IsMap())
        fail (e, "editor is a map with pages:");

    if (const auto s = e["sysex"])
    {
        editor.sysex.write = s["write"].as<std::string> ("");
        editor.sysex.read = s["read"].as<std::string> ("");
        if (editor.sysex.write.empty())
            fail (s, "sysex needs a write: pattern");
        if (const auto d = s["device"])
            editor.sysex.device = lower (d.as<std::string>()) == "channel" ? -1 : intIn (d, "device", 0, 127);
        if (const auto c = s["checksum"])
        {
            const auto text = lower (c.as<std::string>());
            if (text == "roland") editor.sysex.checksum = SysExFormat::Checksum::roland;
            else if (text == "sum7") editor.sysex.checksum = SysExFormat::Checksum::sum7;
            else if (text == "xor") editor.sysex.checksum = SysExFormat::Checksum::bitXor;
            else if (text == "none") editor.sysex.checksum = SysExFormat::Checksum::none;
            else fail (c, "checksum is roland, sum7, xor or none");
        }
        if (s["addressBytes"])
            editor.sysex.addressBytes = intIn (s["addressBytes"], "addressBytes", 1, 4);
        if (s["sizeBytes"])
            editor.sysex.sizeBytes = intIn (s["sizeBytes"], "sizeBytes", 1, 4);
        // Check the patterns now, so mistakes show with the file.
        try
        {
            const MidiBytes a (static_cast<std::size_t> (editor.sysex.addressBytes), 0);
            build (editor.sysex.write, { { "{dev}", { 0 } }, { "{addr}", a }, { "{value}", { 0 } } }, editor.sysex.checksum);
            if (! editor.sysex.read.empty())
                build (editor.sysex.read, { { "{dev}", { 0 } }, { "{addr}", a }, { "{size}", MidiBytes (static_cast<std::size_t> (editor.sysex.sizeBytes), 0) } }, editor.sysex.checksum);
        }
        catch (const device_profile::Error& err)
        {
            fail (s, err.what());
        }
    }

    std::map<std::string, YAML::Node> sections;
    if (const auto s = e["sections"])
    {
        if (! s.IsMap())
            fail (s, "sections is a map of name: [parameters]");
        for (const auto& kv : s)
            sections[kv.first.as<std::string>()] = kv.second;
    }

    const auto pages = e["pages"];
    if (! pages || ! pages.IsSequence())
        fail (e, "editor needs pages:");
    for (const auto& pg : pages)
    {
        SynthPage page;
        page.name = pg["name"].as<std::string> ("");
        if (page.name.empty())
            fail (pg, "a page needs a name");
        MidiBytes base;
        if (const auto b = pg["base"])
            base = hexBytes (b, "base");
        const auto fromSection = [&]() -> YAML::Node {
            const auto sec = pg["section"];
            if (! sec)
                return pg["params"];
            const auto it = sections.find (sec.as<std::string>());
            if (it == sections.end())
                fail (sec, "no section named " + sec.as<std::string>());
            return it->second;
        };
        const YAML::Node params = fromSection();
        if (! params || ! params.IsSequence())
            fail (pg, "page \"" + page.name + "\" needs params: or section:");
        for (const auto& p : params)
            page.params.push_back (parseParam (p, base, editor.sysex));
        editor.pages.push_back (std::move (page));
    }

    if (const auto texts = e["text"])
    {
        if (! texts.IsSequence())
            fail (texts, "text is a list of { name, at, length, charset }");
        if (editor.sysex.write.empty())
            fail (texts, "text needs a sysex write: pattern");
        for (const auto& t : texts)
        {
            SynthNameField f;
            f.name = t["name"].as<std::string>("");
            if (f.name.empty())
                fail (t, "a text field needs a name");
            f.address = hexBytes (t["at"], "at");
            if (static_cast<int> (f.address.size()) != editor.sysex.addressBytes)
                fail (t, "at needs " + std::to_string (editor.sysex.addressBytes) + " address bytes");
            f.length = t["length"] ? intIn (t["length"], "length", 1, 64) : 16;
            f.charset = t["charset"].as<std::string>("");
            editor.nameFields.push_back (std::move (f));
        }
    }

    if (const auto n = e["names"])
    {
        if (editor.sysex.read.empty())
            fail (n, "names: needs a sysex read: pattern");
        NameDump d;
        d.first = hexBytes (n["first"], "first");
        d.stride = hexBytes (n["stride"], "stride");
        d.count = n["count"] ? intIn (n["count"], "count", 1, 128) : 64;
        d.length = n["length"] ? intIn (n["length"], "length", 1, 64) : 16;
        d.charset = n["charset"].as<std::string> ("");
        if (const auto banks = n["banks"])
            for (const auto& b : banks)
            {
                if (! b.IsSequence() || b.size() != 3)
                    fail (b, "a bank is [group name, bank MSB, bank LSB]");
                NameDump::Bank bank;
                bank.group = b[0].as<std::string>();
                const auto byte = [&] (const YAML::Node& x) { return x.as<std::string>() == "-" ? -1 : intIn (x, "bank", 0, 127); };
                bank.msb = byte (b[1]);
                bank.lsb = byte (b[2]);
                d.banks.push_back (std::move (bank));
            }
        editor.names = std::move (d);
    }
    return editor;
}

std::vector<MidiBytes> nameMessages (const SynthEditor& editor, const SynthNameField& field, const std::string& text, int channel)
{
    std::vector<MidiBytes> out;
    if (editor.sysex.write.empty())
        return out;
    for (int i = 0; i < field.length; ++i)
    {
        const char c = static_cast<std::size_t> (i) < text.size() ? text[static_cast<std::size_t> (i)] : ' ';
        int value = static_cast<int> (static_cast<unsigned char> (c));
        if (! field.charset.empty())
        {
            const auto at = field.charset.find (c);
            value = at == std::string::npos ? static_cast<int> (field.charset.find (' ')) : static_cast<int> (at);
            if (value < 0)
                value = 0;
        }
        else if (value > 127)
            value = ' ';
        const auto address = addAddress (field.address, addressBytes (i, static_cast<int> (field.address.size())));
        out.push_back (build (editor.sysex.write, { { "{dev}", { deviceId (editor.sysex, channel) } },
                                                    { "{addr}", address },
                                                    { "{value}", { static_cast<std::uint8_t> (value) } } }, editor.sysex.checksum));
    }
    return out;
}

std::string nameFromAnswer (const SynthEditor& editor, const SynthNameField& field, const MidiBytes& message, int channel)
{
    MidiBytes address, data;
    if (! parseWrite (editor, message, channel, address, data))
        return {};
    if (address != field.address)
        return {};
    std::string name;
    for (int i = 0; i < field.length && static_cast<std::size_t> (i) < data.size(); ++i)
    {
        const auto b = data[static_cast<std::size_t> (i)];
        if (! field.charset.empty())
            name += b < field.charset.size() ? field.charset[b] : '?';
        else
            name += b >= 32 && b < 127 ? static_cast<char> (b) : ' ';
    }
    while (! name.empty() && (name.back() == ' ' || name.back() == '\0'))
        name.pop_back();
    return name;
}

} // namespace synth_edit

} // namespace arranger

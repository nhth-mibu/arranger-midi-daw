#include "arranger/core/Sample.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace arranger
{

std::vector<float> Sample::mono() const
{
    std::vector<float> out (frames(), 0.0f);
    if (channels.empty())
        return out;
    for (const auto& ch : channels)
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] += ch[i];
    const float scale = 1.0f / static_cast<float> (channels.size());
    for (auto& v : out)
        v *= scale;
    return out;
}

namespace wav
{

namespace
{
    std::uint32_t u32 (const std::uint8_t* p) { return static_cast<std::uint32_t> (p[0] | (p[1] << 8) | (p[2] << 16)) | (static_cast<std::uint32_t> (p[3]) << 24); }
    std::uint16_t u16 (const std::uint8_t* p) { return static_cast<std::uint16_t> (p[0] | (p[1] << 8)); }

    void put32 (std::vector<std::uint8_t>& out, std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            out.push_back (static_cast<std::uint8_t> ((v >> (8 * i)) & 0xff));
    }
    void put16 (std::vector<std::uint8_t>& out, std::uint16_t v)
    {
        out.push_back (static_cast<std::uint8_t> (v & 0xff));
        out.push_back (static_cast<std::uint8_t> (v >> 8));
    }
    void putTag (std::vector<std::uint8_t>& out, const char* tag) { out.insert (out.end(), tag, tag + 4); }

    std::filesystem::path utf8Path (const std::string& path)
    {
        return std::filesystem::path (std::u8string (path.begin(), path.end()));
    }

    bool tagIs (const std::uint8_t* p, const char* tag) { return std::memcmp (p, tag, 4) == 0; }
}

Sample parse (const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() < 12 || ! tagIs (bytes.data(), "RIFF") || ! tagIs (bytes.data() + 8, "WAVE"))
        throw Error ("Not a WAV file");

    int format = 0, channelCount = 0, bits = 0, blockAlign = 0;
    double rate = 0;
    const std::uint8_t* data = nullptr;
    std::size_t dataSize = 0;

    std::size_t pos = 12;
    while (pos + 8 <= bytes.size())
    {
        const auto* chunk = bytes.data() + pos;
        const std::size_t size = std::min<std::size_t> (u32 (chunk + 4), bytes.size() - pos - 8);
        if (tagIs (chunk, "fmt ") && size >= 16)
        {
            format = u16 (chunk + 8);
            channelCount = u16 (chunk + 10);
            rate = u32 (chunk + 12);
            blockAlign = u16 (chunk + 20);
            bits = u16 (chunk + 22);
            if (format == 0xfffe && size >= 26) // extensible: the real format is in the sub-format
                format = u16 (chunk + 32);
        }
        else if (tagIs (chunk, "data"))
        {
            data = chunk + 8;
            dataSize = size;
        }
        pos += 8 + size + (size & 1);
    }

    if (data == nullptr || channelCount < 1 || channelCount > 32 || rate <= 0)
        throw Error ("WAV file has no audio");
    const bool isFloat = format == 3;
    if (! (format == 1 || isFloat) || (isFloat && bits != 32 && bits != 64) || (! isFloat && bits != 8 && bits != 16 && bits != 24 && bits != 32))
        throw Error ("This kind of WAV file is not supported (only PCM and float)");

    const int bytesPerSample = bits / 8;
    if (blockAlign < bytesPerSample * channelCount)
        blockAlign = bytesPerSample * channelCount;
    const std::size_t frames = dataSize / static_cast<std::size_t> (blockAlign);

    Sample s;
    s.sampleRate = rate;
    s.channels.assign (static_cast<std::size_t> (channelCount), std::vector<float> (frames));
    for (std::size_t f = 0; f < frames; ++f)
        for (int c = 0; c < channelCount; ++c)
        {
            const auto* p = data + f * static_cast<std::size_t> (blockAlign) + static_cast<std::size_t> (c * bytesPerSample);
            float v = 0.0f;
            if (isFloat && bits == 32)
            {
                std::memcpy (&v, p, 4);
            }
            else if (isFloat)
            {
                double d = 0;
                std::memcpy (&d, p, 8);
                v = static_cast<float> (d);
            }
            else if (bits == 8)
                v = (static_cast<float> (p[0]) - 128.0f) / 128.0f;
            else if (bits == 16)
                v = static_cast<float> (static_cast<std::int16_t> (u16 (p))) / 32768.0f;
            else if (bits == 24)
            {
                std::int32_t i = p[0] | (p[1] << 8) | (p[2] << 16);
                if (i & 0x800000)
                    i -= 0x1000000;
                v = static_cast<float> (i) / 8388608.0f;
            }
            else
                v = static_cast<float> (static_cast<double> (static_cast<std::int32_t> (u32 (p))) / 2147483648.0);
            s.channels[static_cast<std::size_t> (c)][f] = v;
        }
    return s;
}

Sample read (const std::string& path)
{
    std::ifstream in (utf8Path (path), std::ios::binary);
    if (! in)
        throw Error ("Can't open " + path);
    const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    auto s = parse (bytes);
    s.name = utf8Path (path).stem().string();
    return s;
}

std::vector<std::uint8_t> serialise (const Sample& s, int bits)
{
    if (bits != 16 && bits != 24 && bits != 32)
        throw Error ("WAV export is 16, 24 or 32 bit");
    const int channelCount = std::max (1, s.channelCount());
    const int bytesPerSample = bits / 8;
    const auto frames = s.frames();
    const auto dataSize = static_cast<std::uint32_t> (frames * static_cast<std::size_t> (channelCount * bytesPerSample));

    std::vector<std::uint8_t> out;
    out.reserve (44 + dataSize);
    putTag (out, "RIFF");
    put32 (out, 36 + dataSize);
    putTag (out, "WAVE");
    putTag (out, "fmt ");
    put32 (out, 16);
    put16 (out, bits == 32 ? 3 : 1);
    put16 (out, static_cast<std::uint16_t> (channelCount));
    put32 (out, static_cast<std::uint32_t> (std::lround (s.sampleRate)));
    put32 (out, static_cast<std::uint32_t> (std::lround (s.sampleRate)) * static_cast<std::uint32_t> (channelCount * bytesPerSample));
    put16 (out, static_cast<std::uint16_t> (channelCount * bytesPerSample));
    put16 (out, static_cast<std::uint16_t> (bits));
    putTag (out, "data");
    put32 (out, dataSize);

    for (std::size_t f = 0; f < frames; ++f)
        for (int c = 0; c < channelCount; ++c)
        {
            const float v = s.channels.empty() ? 0.0f : s.channels[static_cast<std::size_t> (c)][f];
            if (bits == 32)
            {
                std::uint32_t raw = 0;
                std::memcpy (&raw, &v, 4);
                put32 (out, raw);
            }
            else
            {
                const double full = bits == 16 ? 32767.0 : 8388607.0;
                const auto i = static_cast<std::int32_t> (std::lround (std::clamp (static_cast<double> (v), -1.0, 1.0) * full));
                for (int b = 0; b < bytesPerSample; ++b)
                    out.push_back (static_cast<std::uint8_t> ((static_cast<std::uint32_t> (i) >> (8 * b)) & 0xff));
            }
        }
    return out;
}

void write (const Sample& s, const std::string& path, int bits)
{
    const auto bytes = serialise (s, bits);
    std::ofstream out (utf8Path (path), std::ios::binary);
    if (! out)
        throw Error ("Can't write " + path);
    out.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size()));
    if (! out)
        throw Error ("Can't write " + path);
}

} // namespace wav
} // namespace arranger

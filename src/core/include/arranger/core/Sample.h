#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace arranger
{

/** Audio held in memory: a drum hit, a loop, a slice source. Channels have equal length. */
struct Sample
{
    std::string name;
    double sampleRate = 48000.0;
    std::vector<std::vector<float>> channels;

    std::size_t frames() const noexcept { return channels.empty() ? 0 : channels.front().size(); }
    int channelCount() const noexcept { return static_cast<int> (channels.size()); }
    double seconds() const noexcept { return sampleRate > 0 ? static_cast<double> (frames()) / sampleRate : 0.0; }

    /** All channels averaged to one (for analysis and waveform drawing). */
    std::vector<float> mono() const;
};

namespace wav
{
    struct Error : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    /** Reads a WAV file: PCM 8, 16, 24 or 32 bit, or 32/64 bit float. Throws Error. */
    Sample read (const std::string& path);
    Sample parse (const std::vector<std::uint8_t>& bytes);

    /** Writes 16, 24 bit PCM or 32 bit float. Throws Error. */
    void write (const Sample&, const std::string& path, int bits = 24);
    std::vector<std::uint8_t> serialise (const Sample&, int bits = 24);
}

} // namespace arranger

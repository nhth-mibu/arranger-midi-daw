#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace arranger::test
{

/**
    Writes a tiny but valid SF2 file: one preset ("Test Sine", bank 0 program 0)
    playing a looped sine wave. Enough to check the SoundFont player makes sound.
*/
inline std::filesystem::path writeTestSoundFont (const std::string& fileName = "arranger-test.sf2")
{
    std::vector<std::uint8_t> out;
    auto u8 = [&] (int v) { out.push_back (static_cast<std::uint8_t> (v)); };
    auto u16 = [&] (std::uint32_t v) { u8 (static_cast<int> (v & 0xff)); u8 (static_cast<int> ((v >> 8) & 0xff)); };
    auto u32 = [&] (std::uint32_t v) { u16 (v & 0xffff); u16 (v >> 16); };
    auto tag = [&] (const char* t) { out.insert (out.end(), t, t + 4); };
    auto name20 = [&] (const char* n) { char b[20] {}; std::copy_n (n, std::min<std::size_t> (std::strlen (n), 19), b); out.insert (out.end(), b, b + 20); };

    // Chunk helpers: write a header with a placeholder size, patch it later.
    std::vector<std::size_t> open;
    auto begin = [&] (const char* id, const char* listType = nullptr) {
        tag (id);
        open.push_back (out.size());
        u32 (0);
        if (listType != nullptr)
            tag (listType);
    };
    auto end = [&] {
        const auto at = open.back();
        open.pop_back();
        const auto size = static_cast<std::uint32_t> (out.size() - at - 4);
        for (int i = 0; i < 4; ++i)
            out[at + static_cast<std::size_t> (i)] = static_cast<std::uint8_t> ((size >> (8 * i)) & 0xff);
        if (size % 2 != 0)
            u8 (0);
    };

    constexpr std::uint32_t numSamples = 4400; // 44 cycles of a 100-sample period (441 Hz at 44.1 kHz)

    begin ("RIFF", "sfbk");
    begin ("LIST", "INFO");
    begin ("ifil"); u16 (2); u16 (1); end();
    begin ("isng"); for (char c : std::string ("EMU8000")) u8 (c); u8 (0); end();
    begin ("INAM"); for (char c : std::string ("Test")) u8 (c); u8 (0); u8 (0); end();
    end();

    begin ("LIST", "sdta");
    begin ("smpl");
    for (std::uint32_t i = 0; i < numSamples; ++i)
        u16 (static_cast<std::uint16_t> (static_cast<std::int16_t> (std::lround (12000.0 * std::sin (2.0 * 3.14159265358979 * i / 100.0)))));
    for (int i = 0; i < 46; ++i)
        u16 (0);
    end();
    end();

    begin ("LIST", "pdta");
    begin ("phdr");
    name20 ("Test Sine"); u16 (0); u16 (0); u16 (0); u32 (0); u32 (0); u32 (0);
    name20 ("EOP"); u16 (0); u16 (0); u16 (1); u32 (0); u32 (0); u32 (0);
    end();
    begin ("pbag"); u16 (0); u16 (0); u16 (1); u16 (0); end();
    begin ("pmod"); for (int i = 0; i < 10; ++i) u8 (0); end();
    begin ("pgen"); u16 (41); u16 (0); u16 (0); u16 (0); end(); // instrument 0, then terminator
    begin ("inst"); name20 ("Sine"); u16 (0); name20 ("EOI"); u16 (1); end();
    begin ("ibag"); u16 (0); u16 (0); u16 (2); u16 (0); end();
    begin ("imod"); for (int i = 0; i < 10; ++i) u8 (0); end();
    begin ("igen"); u16 (54); u16 (1); u16 (53); u16 (0); u16 (0); u16 (0); end(); // looped, sample 0
    begin ("shdr");
    name20 ("Sine"); u32 (0); u32 (numSamples); u32 (0); u32 (numSamples); u32 (44100); u8 (69); u8 (0); u16 (0); u16 (1);
    name20 ("EOS"); u32 (0); u32 (0); u32 (0); u32 (0); u32 (0); u8 (0); u8 (0); u16 (0); u16 (0);
    end();
    end();
    end();

    // Tests run in parallel and read the same file: write a copy, close it, then move it into place in one step.
    const auto path = std::filesystem::temp_directory_path() / fileName;
    static std::atomic<unsigned> count { 0 };
    auto part = path;
    part += "." + std::to_string (std::hash<std::thread::id>() (std::this_thread::get_id())) + "-"
            + std::to_string (static_cast<long long> (std::chrono::steady_clock::now().time_since_epoch().count())) + "-" + std::to_string (count++) + ".part";
    {
        std::ofstream f (part, std::ios::binary | std::ios::trunc);
        f.write (reinterpret_cast<const char*> (out.data()), static_cast<std::streamsize> (out.size()));
    }
    std::filesystem::rename (part, path);
    return path;
}

} // namespace arranger::test

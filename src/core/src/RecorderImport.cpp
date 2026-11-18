#include "arranger/core/RecorderImport.h"

#include <algorithm>
#include <map>
#include <regex>

namespace arranger::recorder_import
{

std::vector<TrackFiles> zoomL6Project (const std::vector<std::string>& fileNames)
{
    // TRACK05_ST_01.wav, TRACK01_01.wav, MASTER_01.wav, and TRACK01_01_02.wav for a part after 2 GB.
    static const std::regex pattern (R"(^(?:TRACK(\d{2})|(MASTER))(_ST)?_(\d{2})(?:_(\d{2}))?\.wav$)", std::regex::icase);

    struct Part
    {
        int part = 0;
        std::string file;
    };
    struct Found
    {
        bool stereo = false;
        std::map<int, std::vector<Part>> takes;
    };
    std::map<int, Found> byChannel; // 1..8, and 9 for the mix (so it sorts last)

    for (const auto& file : fileNames)
    {
        std::smatch m;
        if (! std::regex_match (file, m, pattern))
            continue;
        const int channel = m[2].matched ? 9 : std::stoi (m[1].str());
        if (channel < 1 || channel > 9)
            continue;
        auto& f = byChannel[channel];
        f.stereo = f.stereo || m[3].matched || channel == 9;
        f.takes[std::stoi (m[4].str())].push_back ({ m[5].matched ? std::stoi (m[5].str()) : 0, file });
    }

    std::vector<TrackFiles> out;
    for (auto& [channel, f] : byChannel)
    {
        if (f.takes.empty())
            continue;
        auto& [take, parts] = *f.takes.rbegin();
        std::sort (parts.begin(), parts.end(), [] (const Part& a, const Part& b) { return a.part < b.part; });
        TrackFiles t;
        t.channel = channel == 9 ? 0 : channel;
        t.name = channel == 9 ? "Master" : "CH" + std::to_string (channel);
        t.stereo = f.stereo;
        t.take = take;
        for (const auto& p : parts)
            t.files.push_back (p.file);
        out.push_back (std::move (t));
    }
    return out;
}

} // namespace arranger::recorder_import

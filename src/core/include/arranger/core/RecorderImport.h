#pragma once

#include <string>
#include <vector>

namespace arranger::recorder_import
{

/** The files of one channel in a multitrack recorder's project folder, in play order. */
struct TrackFiles
{
    std::string name;    ///< the track name for the app: "CH1", "Master"
    int channel = 0;     ///< 1..8 for a channel, 0 for the stereo mix
    bool stereo = false;
    int take = 1;        ///< the take that was chosen (the last one)
    std::vector<std::string> files; ///< the first file, then the files that continue it (over 2 GB)
};

/**
    Reads the file names of a Zoom LiveTrak L6max project folder (RECORDER/yymmdd_hhmmss).
    Names are TRACK01_01.wav (channels 1-4, mono), TRACK05_ST_01.wav (channels 5-8,
    stereo) and MASTER_01.wav (the stereo mix); the last number is the take, which
    goes up with each bounce. A recording over 2 GB continues in files with _01 to
    _99 added. Returns the newest take of each channel, channels first, the mix last.
    Other files are ignored. Case is ignored.
*/
std::vector<TrackFiles> zoomL6Project (const std::vector<std::string>& fileNames);

} // namespace arranger::recorder_import

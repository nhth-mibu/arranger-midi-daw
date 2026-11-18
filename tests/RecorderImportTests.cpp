#include "arranger/core/RecorderImport.h"

#include <catch2/catch_test_macros.hpp>

using namespace arranger;

TEST_CASE ("L6max recording folder: newest take of each channel, the mix last", "[recorder]")
{
    // The folder in the manual (p. 97), with a bounce (take 02) on channel 1,
    // a recording over 2 GB on channel 5 and files that are not recordings.
    const std::vector<std::string> files {
        "MASTER_01.wav", "TRACK01_01.wav", "TRACK01_02.wav", "TRACK02_01.wav", "TRACK03_01.wav", "TRACK04_01.wav",
        "TRACK05_ST_01_01.wav", "TRACK05_ST_01.wav", "TRACK06_ST_01.wav", "TRACK07_ST_01.wav", "track08_st_01.WAV",
        "L6maxPRJDATA.l6maxprj", ".DS_Store", "._TRACK02_01.wav", "TRACK09_01.wav",
    };
    const auto tracks = recorder_import::zoomL6Project (files);
    REQUIRE (tracks.size() == 9);

    CHECK (tracks[0].name == "CH1");
    CHECK (tracks[0].channel == 1);
    CHECK (tracks[0].take == 2);
    CHECK (tracks[0].files == std::vector<std::string> { "TRACK01_02.wav" });
    CHECK_FALSE (tracks[0].stereo);

    CHECK (tracks[4].name == "CH5");
    CHECK (tracks[4].stereo);
    CHECK (tracks[4].files == std::vector<std::string> { "TRACK05_ST_01.wav", "TRACK05_ST_01_01.wav" });

    CHECK (tracks[7].name == "CH8");
    CHECK (tracks[7].files == std::vector<std::string> { "track08_st_01.WAV" });

    CHECK (tracks[8].name == "Master");
    CHECK (tracks[8].channel == 0);
    CHECK (tracks[8].stereo);

    CHECK (recorder_import::zoomL6Project ({ "song.wav", "notes.txt" }).empty());
}

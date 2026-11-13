#pragma once

#include "arranger/core/Song.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace arranger
{

/**
    Project files are UTF-8 JSON, readable and easy to diff.

    {
      "format": "arranger-midi-daw-project",
      "version": 1,
      "song": { ... }
    }

    Missing fields take their default value, unknown fields are ignored, and a
    file from a newer version is refused rather than half-read.
*/
namespace project_file
{
    inline constexpr const char* kFormatName = "arranger-midi-daw-project";
    inline constexpr int kVersion = 3;
    inline constexpr const char* kExtension = ".arranger";

    struct Error : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    std::string toJson (const Song&);
    Song fromJson (std::string_view json); ///< throws Error

    /** Writes to a temporary file first, then renames, so a crash never leaves half a file. Throws Error. */
    void save (const Song&, const std::filesystem::path&);
    Song load (const std::filesystem::path&); ///< throws Error
}

} // namespace arranger

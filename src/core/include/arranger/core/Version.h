#pragma once

#include <string_view>

namespace arranger::version
{

/**
    True when `latest` is a newer release than `current`.

    Tags may start with "v". Build metadata after "+" is ignored. A stable
    release is newer than a prerelease with the same numbers. Invalid or
    empty versions are never newer.
*/
bool isNewer (std::string_view current, std::string_view latest);

} // namespace arranger::version

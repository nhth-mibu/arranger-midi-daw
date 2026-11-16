#include "arranger/core/Version.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("Release versions compare by number")
{
    CHECK (arranger::version::isNewer ("0.1.0", "0.2.0"));
    CHECK (arranger::version::isNewer ("v0.9.9", "v0.10.0"));
    CHECK (arranger::version::isNewer ("1.2", "1.2.1"));
    CHECK_FALSE (arranger::version::isNewer ("0.2.0", "0.2.0"));
    CHECK_FALSE (arranger::version::isNewer ("0.3.0", "0.2.9"));
    CHECK_FALSE (arranger::version::isNewer ("0.2.0", "v0.1.0"));
}

TEST_CASE ("A stable release is newer than a prerelease")
{
    CHECK (arranger::version::isNewer ("1.0.0-beta.2", "1.0.0"));
    CHECK_FALSE (arranger::version::isNewer ("1.0.0", "1.0.0-rc.1"));
}

TEST_CASE ("Invalid versions do not report an update")
{
    CHECK_FALSE (arranger::version::isNewer ("", "1.0.0"));
    CHECK_FALSE (arranger::version::isNewer ("0.1.0", ""));
    CHECK_FALSE (arranger::version::isNewer ("dev", "1.0.0"));
    CHECK_FALSE (arranger::version::isNewer ("0.1.0", "release"));
}

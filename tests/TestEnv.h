#pragma once

#include <cstdlib>
#include <optional>
#include <string>

namespace testenv
{
    // std::getenv trips MSVC's C4996 (an error with warnings as errors); this works everywhere.
    inline std::optional<std::string> envVar (const char* name)
    {
#if defined(_MSC_VER)
        char* buffer = nullptr;
        std::size_t size = 0;
        if (_dupenv_s (&buffer, &size, name) != 0 || buffer == nullptr)
            return std::nullopt;
        std::string value (buffer);
        std::free (buffer);
        return value;
#else
        if (const char* value = std::getenv (name))
            return std::string (value);
        return std::nullopt;
#endif
    }
}

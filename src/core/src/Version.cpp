#include "arranger/core/Version.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace arranger::version
{
namespace
{
    struct Parsed
    {
        std::vector<int> numbers;
        bool hasPrerelease = false;
        std::string prerelease;
    };

    std::string trimmed (std::string_view text)
    {
        auto first = text.begin();
        auto last = text.end();
        while (first != last && std::isspace (static_cast<unsigned char> (*first)) != 0)
            ++first;
        while (last != first && std::isspace (static_cast<unsigned char> (last[-1])) != 0)
            --last;
        return { first, last };
    }

    bool parse (std::string_view text, Parsed& out)
    {
        auto value = trimmed (text);
        if (! value.empty() && (value[0] == 'v' || value[0] == 'V'))
            value.erase (value.begin());

        if (const auto plus = value.find ('+'); plus != std::string::npos)
            value.erase (plus);

        if (const auto dash = value.find ('-'); dash != std::string::npos)
        {
            out.hasPrerelease = true;
            out.prerelease = value.substr (dash + 1);
            value.erase (dash);
        }

        if (value.empty())
            return false;

        std::size_t start = 0;
        while (start <= value.size())
        {
            const auto dot = value.find ('.', start);
            const auto end = dot == std::string::npos ? value.size() : dot;
            if (end == start)
                return false;

            int number = 0;
            for (auto i = start; i < end; ++i)
            {
                const auto c = static_cast<unsigned char> (value[i]);
                if (std::isdigit (c) == 0)
                    return false;
                number = std::min (1000000, number * 10 + (value[i] - '0'));
            }
            out.numbers.push_back (number);

            if (dot == std::string::npos)
                break;
            start = dot + 1;
        }

        return ! out.numbers.empty();
    }

    int compareNumbers (const std::vector<int>& a, const std::vector<int>& b)
    {
        const auto count = std::max (a.size(), b.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto left = i < a.size() ? a[i] : 0;
            const auto right = i < b.size() ? b[i] : 0;
            if (left != right)
                return left < right ? -1 : 1;
        }
        return 0;
    }
} // namespace

bool isNewer (std::string_view current, std::string_view latest)
{
    Parsed currentVersion, latestVersion;
    if (! parse (current, currentVersion) || ! parse (latest, latestVersion))
        return false;

    if (const auto order = compareNumbers (currentVersion.numbers, latestVersion.numbers); order != 0)
        return order < 0;

    if (latestVersion.hasPrerelease != currentVersion.hasPrerelease)
        return ! latestVersion.hasPrerelease;

    return false;
}

} // namespace arranger::version

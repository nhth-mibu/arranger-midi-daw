#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace arranger::fuzz
{

using Bytes = std::span<const std::uint8_t>;

/** A problem the target found that is not a crash (for example a song that does not round-trip). */
struct Finding : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct Target
{
    const char* name;
    const char* what;
    void (*run) (Bytes); ///< throws Finding; crashes are caught by the sanitizers
};

const std::vector<Target>& targets();

/** How many reads finished without a refusal: a check that the fuzzer gets past the first checks of a reader. */
std::size_t& acceptedCount();

} // namespace arranger::fuzz

#pragma once

#include <algorithm>

namespace arranger
{

/** A non-owning view of planar float audio for one callback. */
struct AudioBlock
{
    float* const* channels = nullptr;
    int numChannels = 0;
    int numSamples = 0;

    void clear() noexcept
    {
        for (int c = 0; c < numChannels; ++c)
            if (channels[c] != nullptr)
                std::fill (channels[c], channels[c] + numSamples, 0.0f);
    }
};

} // namespace arranger

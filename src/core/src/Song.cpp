#include "arranger/core/Song.h"

#include <algorithm>

namespace arranger
{

bool Track::operator== (const Track& o) const
{
    if (! (id == o.id && name == o.name && colour == o.colour && output == o.output && sound == o.sound && kit == o.kit
           && mixer == o.mixer && armed == o.armed && inputPort == o.inputPort && clips.size() == o.clips.size()))
        return false;

    // Compare clip contents, not pointers.
    for (std::size_t i = 0; i < clips.size(); ++i)
        if (clips[i] != o.clips[i] && ! (*clips[i] == *o.clips[i]))
            return false;
    return true;
}

Tick Song::contentEnd() const noexcept
{
    Tick end = timeSignature.ticksPerBar();
    for (const auto& t : tracks)
        for (const auto& c : t.clips)
            end = std::max (end, c->end());
    return end;
}

} // namespace arranger

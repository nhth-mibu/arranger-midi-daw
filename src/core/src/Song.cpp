#include "arranger/core/Song.h"

#include <algorithm>

namespace arranger
{

bool Track::operator== (const Track& o) const
{
    if (! (id == o.id && name == o.name && colour == o.colour && output == o.output && sound == o.sound && kit == o.kit
           && mixer == o.mixer && armed == o.armed && styleChannel == o.styleChannel && inputPort == o.inputPort && marks == o.marks && volumeAutomation == o.volumeAutomation && panAutomation == o.panAutomation && instrument == o.instrument && effects == o.effects && clips.size() == o.clips.size()))
        return false;

    if (audio.size() != o.audio.size())
        return false;
    for (std::size_t i = 0; i < audio.size(); ++i)
        if (audio[i] != o.audio[i] && ! (*audio[i] == *o.audio[i]))
            return false;

    // Compare clip contents, not pointers.
    for (std::size_t i = 0; i < clips.size(); ++i)
        if (clips[i] != o.clips[i] && ! (*clips[i] == *o.clips[i]))
            return false;

    // Slots: trailing empty slots don't count.
    const auto used = [] (const std::vector<ClipPtr>& s) {
        auto n = s.size();
        while (n > 0 && s[n - 1] == nullptr)
            --n;
        return n;
    };
    if (used (slots) != used (o.slots))
        return false;
    for (std::size_t i = 0; i < used (slots); ++i)
    {
        if ((slots[i] == nullptr) != (o.slots[i] == nullptr))
            return false;
        if (slots[i] != nullptr && slots[i] != o.slots[i] && ! (*slots[i] == *o.slots[i]))
            return false;
    }
    return true;
}

Tick Song::contentEnd() const noexcept
{
    Tick end = timeSignature.ticksPerBar();
    for (const auto& t : tracks)
    {
        for (const auto& c : t.clips)
            end = std::max (end, c->end());
        for (const auto& c : t.audio)
            end = std::max (end, c->end());
    }
    return end;
}

int Song::sceneCount() const noexcept
{
    std::size_t rows = std::max<std::size_t> (8, scenes.size());
    for (const auto& t : tracks)
        for (std::size_t i = 0; i < t.slots.size(); ++i)
            if (t.slots[i] != nullptr)
                rows = std::max (rows, i + 2); // always one free row below
    return static_cast<int> (rows);
}

} // namespace arranger

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace arranger
{

/** One short MIDI message placed at a sample offset inside a block. */
struct MidiEvent
{
    int sampleOffset = 0;
    std::array<std::uint8_t, 3> bytes {};
    std::uint8_t size = 0;

    static MidiEvent noteOn (int offset, int channel, int note, int velocity) noexcept
    {
        return { offset, { static_cast<std::uint8_t> (0x90 | ((channel - 1) & 0x0f)),
                           static_cast<std::uint8_t> (note & 0x7f),
                           static_cast<std::uint8_t> (velocity & 0x7f) }, 3 };
    }

    static MidiEvent noteOff (int offset, int channel, int note) noexcept
    {
        return { offset, { static_cast<std::uint8_t> (0x80 | ((channel - 1) & 0x0f)),
                           static_cast<std::uint8_t> (note & 0x7f), 0 }, 3 };
    }

    bool isNoteOn() const noexcept { return size == 3 && (bytes[0] & 0xf0) == 0x90 && bytes[2] > 0; }
};

/**
    A fixed-capacity list of MIDI events. Memory is reserved up front, so adding
    an event on the audio thread never allocates. When full, new events are
    dropped and counted.
*/
class MidiEventBuffer
{
public:
    explicit MidiEventBuffer (std::size_t capacity = 1024) { events.reserve (capacity); }

    bool add (const MidiEvent& e) noexcept
    {
        if (events.size() == events.capacity())
        {
            ++dropped;
            return false;
        }
        events.push_back (e);
        return true;
    }

    void clear() noexcept { events.clear(); }
    std::size_t size() const noexcept { return events.size(); }
    bool empty() const noexcept { return events.empty(); }
    std::size_t droppedCount() const noexcept { return dropped; }

    auto begin() const noexcept { return events.begin(); }
    auto end() const noexcept { return events.end(); }
    const MidiEvent& operator[] (std::size_t i) const noexcept { return events[i]; }

private:
    std::vector<MidiEvent> events;
    std::size_t dropped = 0;
};

} // namespace arranger

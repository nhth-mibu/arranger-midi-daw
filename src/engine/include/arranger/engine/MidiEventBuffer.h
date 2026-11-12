#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace arranger
{

/** One short MIDI message placed at a sample offset inside a block. */
struct MidiEvent
{
    /** Destination port. 0 is the built-in SoundFont player, 1 and up are
        hardware outputs listed in the playback snapshot, kNoPort goes nowhere. */
    static constexpr std::uint16_t kSoundFontPort = 0;
    static constexpr std::uint16_t kNoPort = 0xffff;

    int sampleOffset = 0;
    std::array<std::uint8_t, 3> bytes {};
    std::uint8_t size = 0;
    std::uint16_t port = kSoundFontPort;

    static MidiEvent noteOn (int offset, int channel, int note, int velocity) noexcept
    {
        return { offset, { static_cast<std::uint8_t> (0x90 | ((channel - 1) & 0x0f)),
                           static_cast<std::uint8_t> (note & 0x7f),
                           static_cast<std::uint8_t> (velocity & 0x7f) }, 3, kSoundFontPort };
    }

    static MidiEvent noteOff (int offset, int channel, int note) noexcept
    {
        return { offset, { static_cast<std::uint8_t> (0x80 | ((channel - 1) & 0x0f)),
                           static_cast<std::uint8_t> (note & 0x7f), 0 }, 3, kSoundFontPort };
    }

    static MidiEvent make (int offset, std::uint8_t b0, std::uint8_t b1, std::uint8_t b2, std::uint16_t port) noexcept
    {
        const auto kind = b0 & 0xf0;
        return { offset, { b0, b1, b2 }, static_cast<std::uint8_t> ((kind == 0xc0 || kind == 0xd0) ? 2 : 3), port };
    }

    bool isNoteOn() const noexcept { return size == 3 && (bytes[0] & 0xf0) == 0x90 && bytes[2] > 0; }
    bool isNoteOff() const noexcept { return size == 3 && ((bytes[0] & 0xf0) == 0x80 || ((bytes[0] & 0xf0) == 0x90 && bytes[2] == 0)); }
    int channel() const noexcept { return (bytes[0] & 0x0f) + 1; }
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

    /** Sorts by sample offset, keeping the order of events at the same offset. Never allocates. */
    void sortByTime() noexcept
    {
        // Insertion sort: buffers are short and nearly sorted, and std::stable_sort may allocate.
        for (std::size_t i = 1; i < events.size(); ++i)
        {
            auto e = events[i];
            auto j = i;
            while (j > 0 && events[j - 1].sampleOffset > e.sampleOffset)
            {
                events[j] = events[j - 1];
                --j;
            }
            events[j] = e;
        }
    }
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

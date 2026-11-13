#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace arranger
{

/**
    Audio on its way from the audio thread to the UI thread, for recording.
    The audio thread pushes stereo frames into a ring; the UI thread pops them
    a few times per second and keeps them. No locks and no allocation on the
    audio side. When the UI thread is too slow the newest frames are dropped
    and counted.
*/
class AudioCapture
{
public:
    explicit AudioCapture (std::size_t frames = std::size_t { 1 } << 18)
        : left (frames), right (frames), mask (frames - 1) {}

    /** Audio thread. `r` may be null (a mono input). */
    void push (const float* l, const float* r, int count) noexcept
    {
        auto w = writeIndex.load (std::memory_order_relaxed);
        const auto rd = readIndex.load (std::memory_order_acquire);
        const std::size_t room = left.size() - 1 - ((w - rd) & mask);
        const auto n = static_cast<std::size_t> (count > 0 ? count : 0);
        const auto take = n < room ? n : room;
        for (std::size_t i = 0; i < take; ++i)
        {
            left[w] = l[i];
            right[w] = r != nullptr ? r[i] : l[i];
            w = (w + 1) & mask;
        }
        writeIndex.store (w, std::memory_order_release);
        if (take < n)
            lost.fetch_add (n - take, std::memory_order_relaxed);
    }

    /** UI thread: appends what has arrived to the two vectors. Returns the number of frames. */
    std::size_t pop (std::vector<float>& l, std::vector<float>& r)
    {
        auto rd = readIndex.load (std::memory_order_relaxed);
        const auto w = writeIndex.load (std::memory_order_acquire);
        const std::size_t count = (w - rd) & mask;
        for (std::size_t i = 0; i < count; ++i)
        {
            l.push_back (left[rd]);
            r.push_back (right[rd]);
            rd = (rd + 1) & mask;
        }
        readIndex.store (rd, std::memory_order_release);
        return count;
    }

    /** Frames that did not fit since the last call. */
    std::uint64_t takeLost() noexcept { return lost.exchange (0, std::memory_order_relaxed); }

private:
    std::vector<float> left, right;
    std::size_t mask;
    alignas (64) std::atomic<std::size_t> writeIndex { 0 };
    alignas (64) std::atomic<std::size_t> readIndex { 0 };
    std::atomic<std::uint64_t> lost { 0 };
};

} // namespace arranger

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace arranger
{

/**
    Lock-free single-producer, single-consumer queue. Used to pass commands from
    the UI thread to the audio thread (and events back) without locks or
    allocation. Capacity must be a power of two; one slot stays empty.
*/
template <typename T, std::size_t Capacity>
class SpscQueue
{
    static_assert ((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    /** Producer side. Returns false when full. */
    bool push (const T& item) noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto next = (w + 1) & mask;
        if (next == readIndex.load (std::memory_order_acquire))
            return false;
        slots[w] = item;
        writeIndex.store (next, std::memory_order_release);
        return true;
    }

    /** Consumer side. */
    std::optional<T> pop() noexcept
    {
        const auto r = readIndex.load (std::memory_order_relaxed);
        if (r == writeIndex.load (std::memory_order_acquire))
            return std::nullopt;
        T item = slots[r];
        readIndex.store ((r + 1) & mask, std::memory_order_release);
        return item;
    }

    bool empty() const noexcept
    {
        return readIndex.load (std::memory_order_acquire) == writeIndex.load (std::memory_order_acquire);
    }

private:
    static constexpr std::size_t mask = Capacity - 1;
    std::array<T, Capacity> slots {};
    alignas (64) std::atomic<std::size_t> writeIndex { 0 };
    alignas (64) std::atomic<std::size_t> readIndex { 0 };
};

} // namespace arranger

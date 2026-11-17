#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace arranger
{

/**
    Hands an object from the UI thread to the audio thread without the audio
    thread ever locking, allocating or freeing.

    The UI publishes a shared_ptr; the audio thread picks up the raw pointer at
    the start of a block and reports which one it is using. The UI keeps every
    published object alive until the audio thread has moved past it, then frees
    it in collectGarbage() (on the UI thread).
*/
template <typename T>
class Handoff
{
public:
    // ---- UI thread -----------------------------------------------------------
    void publish (std::shared_ptr<T> object)
    {
        std::lock_guard lock (uiLock);
        latest.store (object.get());
        retained.push_back (std::move (object));
        collectLocked();
    }

    /** Frees objects the audio thread no longer uses. Call from a UI timer. */
    void collectGarbage()
    {
        std::lock_guard lock (uiLock);
        collectLocked();
    }

    /** The newest published object (UI side). */
    std::shared_ptr<T> current() const
    {
        std::lock_guard lock (uiLock);
        return retained.empty() ? nullptr : retained.back();
    }

    // ---- audio thread --------------------------------------------------------
    /** Protects the newest and previous objects until finishTransition(). */
    T* acquire() noexcept
    {
        previous.store (inUse.load());
        // Mark the pointer as in use, then check it is still the latest. If the UI
        // published (and maybe freed the one we loaded) in between, try again:
        // the pointer we return was protected the whole time we hold it.
        for (;;)
        {
            auto* p = latest.load();
            inUse.store (p);
            if (latest.load() == p)
                return p;
        }
    }

    /** Call after the audio thread has finished reading the previous object. */
    void finishTransition() noexcept { previous.store (nullptr); }

    /** Called when the audio thread stops touching objects (device stopped). */
    void release() noexcept
    {
        previous.store (nullptr);
        inUse.store (nullptr);
    }

private:
    void collectLocked()
    {
        const auto* keepA = latest.load();
        const auto* keepB = inUse.load();
        const auto* keepC = previous.load();
        std::erase_if (retained, [&] (const std::shared_ptr<T>& p) { return p.get() != keepA && p.get() != keepB && p.get() != keepC; });
    }

    std::atomic<T*> latest { nullptr };
    std::atomic<T*> inUse { nullptr };
    std::atomic<T*> previous { nullptr };
    mutable std::mutex uiLock;
    std::vector<std::shared_ptr<T>> retained;
};

} // namespace arranger

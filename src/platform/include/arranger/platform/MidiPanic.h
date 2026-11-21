#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace arranger::platform
{
/** Messages waiting for the sender, with a panic that drops them before the next send.
    The sender and the panic caller hold the same lock, so no queued note can pass the panic.
*/
template <typename Message>
class PanicQueue
{
public:
    void reserve (std::size_t n) { messages.reserve (n); }
    void push (Message m) { messages.push_back (std::move (m)); }
    void requestPanic() noexcept { panicRequested = true; }

    /** Sender, under its output lock. Clears pending messages if a panic was requested. */
    bool discardBeforeSending() noexcept
    {
        if (! panicRequested)
            return false;
        panicRequested = false;
        messages.clear();
        return true;
    }

    std::vector<Message>& pending() noexcept { return messages; }
    const std::vector<Message>& pending() const noexcept { return messages; }
    std::size_t size() const noexcept { return messages.size(); }
    bool empty() const noexcept { return messages.empty(); }
    void eraseSent (std::size_t count) { messages.erase (messages.begin(), messages.begin() + static_cast<std::ptrdiff_t> (count)); }

private:
    std::vector<Message> messages;
    bool panicRequested = false;
};

/** The channel messages that silence a synth: sustain off, all sound off, all notes off. */
inline std::vector<std::array<std::uint8_t, 3>> panicMessages()
{
    std::vector<std::array<std::uint8_t, 3>> messages;
    messages.reserve (16 * 3);
    for (int channel = 0; channel < 16; ++channel)
    {
        const auto status = static_cast<std::uint8_t> (0xb0 | channel);
        messages.push_back ({ status, 64, 0 });  // sustain off
        messages.push_back ({ status, 120, 0 }); // all sound off
        messages.push_back ({ status, 123, 0 }); // all notes off
    }
    return messages;
}
} // namespace arranger::platform

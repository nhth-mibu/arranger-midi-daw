#pragma once

#include <juce_events/juce_events.h>

#include <atomic>
#include <exception>
#include <functional>
#include <memory>

namespace arranger
{

/** Queues owned work. Cancellation skips work that has not started yet.
    The function must capture its inputs and result storage by value. */
inline bool onMessageThread (std::function<void()> fn, const std::function<bool()>& cancelled)
{
    struct Task
    {
        std::function<void()> run;
        juce::WaitableEvent ready;
        std::atomic<bool> abandoned { false };
        std::exception_ptr error;
    };
    auto task = std::make_shared<Task>();
    task->run = std::move (fn);
    juce::MessageManager::callAsync ([task] {
        if (! task->abandoned.load())
            try
            {
                task->run();
            }
            catch (...)
            {
                task->error = std::current_exception();
            }
        task->ready.signal();
    });
    while (! task->ready.wait (100))
        if (cancelled && cancelled())
        {
            task->abandoned.store (true);
            return false;
        }
    if (task->error)
        std::rethrow_exception (task->error);
    return true;
}

} // namespace arranger

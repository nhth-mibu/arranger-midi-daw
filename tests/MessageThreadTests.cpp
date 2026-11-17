#include "MessageThreadTask.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>

TEST_CASE ("Cancellation skips queued message-thread work and releases its state", "[audit][async]")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto ran = std::make_shared<bool> (false);
    auto held = std::make_shared<int> (1);
    std::weak_ptr<int> weak = held;
    auto worker = std::async (std::launch::async, [ran, held] {
        return arranger::onMessageThread ([ran, held] { *ran = true; }, [] { return true; });
    });
    held.reset();
    REQUIRE_FALSE (worker.get());
    CHECK_FALSE (*ran);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK_FALSE (*ran);
    CHECK (weak.expired());
}

TEST_CASE ("Message-thread exceptions reach the waiting worker", "[audit][async]")
{
    juce::ScopedJuceInitialiser_GUI init;
    std::atomic<bool> done { false };
    auto worker = std::async (std::launch::async, [&done] {
        try
        {
            arranger::onMessageThread ([] { throw std::runtime_error ("invalid tool call"); }, {});
        }
        catch (const std::runtime_error& e)
        {
            done.store (true);
            return std::string (e.what());
        }
        done.store (true);
        return std::string();
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);
    while (! done.load() && std::chrono::steady_clock::now() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    CHECK (done.load());
    CHECK (worker.get() == "invalid tool call");
}

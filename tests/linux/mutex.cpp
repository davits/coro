#include <coro/coro.hpp>
#include <coro/sleep.hpp>
#include <coro/sync/mutex.hpp>
#include <coro/executors/serial_executor.hpp>

#include <gtest/gtest.h>

coro::Task<void> advance(size_t& counter, coro::Mutex& mutex) {
    for (int i = 0; i < 1000; ++i) {
        auto lock = co_await mutex;
        ++counter;
    }
    co_return;
}

coro::Task<void>
criticalSection(coro::Mutex& mutex, std::atomic<int>& inside, std::atomic<int>& overall, uint32_t hold) {
    auto lock = co_await mutex;
    // fails if the lock was handed to more than one coroutine at a time
    EXPECT_EQ(++inside, 1);
    co_await coro::sleep(hold);
    --inside;
    ++overall;
}

TEST(Mutex, Cancellation) {
    coro::Mutex mutex;
    std::atomic<int> inside = 0;
    std::atomic<int> overall = 0;

    auto e1 = coro::SerialExecutor::create();
    auto e2 = coro::SerialExecutor::create();
    auto e3 = coro::SerialExecutor::create();

    using namespace std::chrono_literals;
    // takes the lock and keeps holding it while the others queue up
    auto holder = e1->future(criticalSection(mutex, inside, overall, 300));
    std::this_thread::sleep_for(50ms);
    // queued first, gets the lock only once the holder releases it
    auto queued = e2->future(criticalSection(mutex, inside, overall, 10));
    std::this_thread::sleep_for(50ms);
    // queued second and cancelled while waiting, so it never receives the lock
    coro::StopSource ss;
    auto cancelled = e3->future(criticalSection(mutex, inside, overall, 10).setStopToken(ss.token()));
    std::this_thread::sleep_for(50ms);

    ss.requestStop();
    EXPECT_THROW(cancelled.get(), coro::StopError);

    // cancelled awaiter must not be left in the queue, unlock() would use it after it is destroyed
    holder.get();
    queued.get();
    EXPECT_EQ(inside, 0);
    EXPECT_EQ(overall, 2);
}

TEST(Mutex, CrossExecutorDataRace) {
    coro::Mutex mutex;
    size_t counter = 0;
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 10; ++i) {
        auto task = advance(counter, mutex);
        auto executor = coro::SerialExecutor::create();
        futures.push_back(executor->future(std::move(task)));
    }
    for (auto& future : futures) {
        future.wait();
    }
    EXPECT_EQ(counter, 10000);
}

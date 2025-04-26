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

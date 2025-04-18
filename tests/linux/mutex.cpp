#include <coro/coro.hpp>
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

TEST(Mutex, DataRace) {
    coro::Mutex mutex;
    size_t counter = 0;
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        auto task = advance(counter, mutex);
        threads.push_back(std::thread {[task = std::move(task)]() mutable {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(10ms);
            auto executor = coro::SerialExecutor::create();
            executor->run(task);
        }});
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(counter, 10000);
}

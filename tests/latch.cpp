#include <coro/coro.hpp>
#include <coro/sync/latch.hpp>
#include <coro/executors/serial_executor.hpp>
#include <coro/helpers/timeout.hpp>

#include <gtest/gtest.h>

coro::Task<int> work() {
    co_await coro::timeout(100);
    co_return 42;
}

coro::Task<int> defer() {
    auto task = work();
    coro::Latch latch {1};
    auto wrapper = [](coro::Task<int> task, coro::Latch& latch) -> coro::Task<int> {
        int result = co_await std::move(task);
        latch.count_down();
        co_return result;
    }(std::move(task), latch);
    std::thread thread {[task = std::move(wrapper)]() mutable {
        auto executor = coro::SerialExecutor::create();
        int result = executor->run(task);
        EXPECT_EQ(result, 42);
    }};
    co_await latch;
    thread.join();
    co_return 42;
}

TEST(LatchTest, CrossExecutorLatch) {
    auto executor = coro::SerialExecutor::create();
    auto task = defer();
    int result = executor->run(task);
    EXPECT_EQ(result, 42);
}

coro::Task<int> signaled(coro::Latch& latch) {
    co_await latch;
    co_return 42;
}

TEST(Latch, AlreadySignaled) {
    auto executor = coro::SerialExecutor::create();
    coro::Latch latch {0};
    auto task = signaled(latch);
    int result = executor->run(task);
    EXPECT_EQ(result, 42);
}

#include <coro/coro.hpp>
#include <coro/sync/latch.hpp>
#include <coro/executors/serial_executor.hpp>
#include <coro/sleep.hpp>

#include <gtest/gtest.h>

coro::Task<int> work() {
    co_await coro::sleep(50);
    co_return 42;
}

coro::Task<int> consumer(coro::Latch& latch) {
    co_await latch;
    co_return co_await work();
}

coro::Task<int> producer(coro::Latch& latch) {
    auto result = co_await work();
    latch.count_down();
    co_return result;
}

TEST(Latch, Simple) {
    coro::Latch latch {1};
    auto executor = coro::SerialExecutor::create();
    auto task1 = consumer(latch);
    auto future = executor->future(std::move(task1));

    using namespace std::chrono_literals;
    EXPECT_EQ(future.wait_for(100ms), std::future_status::timeout);

    executor->schedule(producer(latch));
    auto result = future.get();
    EXPECT_EQ(result, 42);
}

TEST(Latch, Cancellation) {
    coro::Latch latch {1};
    auto executor = coro::SerialExecutor::create();
    auto stop = coro::StopSource {};
    auto future = executor->future(consumer(latch).setStopToken(stop.token()));

    using namespace std::chrono_literals;
    EXPECT_EQ(future.wait_for(100ms), std::future_status::timeout);

    stop.requestStop();
    EXPECT_THROW(future.get(), coro::StopError);
}

TEST(Latch, CrossExecutor) {
    coro::Latch latch {1};
    auto executor1 = coro::SerialExecutor::create();
    auto task1 = consumer(latch);
    auto future = executor1->future(std::move(task1));

    using namespace std::chrono_literals;
    EXPECT_EQ(future.wait_for(100ms), std::future_status::timeout);

    auto executor2 = coro::SerialExecutor::create();
    executor2->schedule(producer(latch));

    auto result = future.get();
    EXPECT_EQ(result, 42);
}

TEST(Latch, AlreadySignaled) {
    auto executor = coro::SerialExecutor::create();
    coro::Latch latch {0};
    auto task = consumer(latch);
    int result = executor->syncWait(std::move(task));
    EXPECT_EQ(result, 42);
}

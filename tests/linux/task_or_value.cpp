#include <coro/coro.hpp>
#include <coro/executors/serial_executor.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <stdexcept>

namespace {

coro::Task<int> work(std::atomic<bool>& started) {
    started = true;
    co_return 42;
}

coro::Task<int> failing() {
    throw std::runtime_error("failed");
    co_return 0;
}

coro::Task<void> voidWork(std::atomic<bool>& started) {
    started = true;
    co_return;
}

coro::Task<int> awaitIt(coro::TaskOrValue<int> taskOrValue) {
    co_return co_await std::move(taskOrValue);
}

coro::Task<void> awaitVoid(coro::TaskOrValue<void> taskOrValue) {
    co_await std::move(taskOrValue);
}

} // namespace

TEST(TaskOrValue, ScheduledTaskRunsBeforeItIsAwaited) {
    auto executor = coro::SerialExecutor::create();
    std::atomic<bool> started = false;
    auto scheduled = executor->schedule(coro::TaskOrValue<int> {work(started)});
    EXPECT_TRUE(scheduled.isTask());
    executor->drain();
    EXPECT_TRUE(started);
    EXPECT_EQ(executor->syncWait(awaitIt(std::move(scheduled))), 42);
}

TEST(TaskOrValue, NextRunsTheTask) {
    auto executor = coro::SerialExecutor::create();
    std::atomic<bool> started = false;
    auto scheduled = executor->next(coro::TaskOrValue<int> {work(started)});
    executor->drain();
    EXPECT_TRUE(started);
    EXPECT_EQ(executor->syncWait(awaitIt(std::move(scheduled))), 42);
}

TEST(TaskOrValue, ValuePassesThrough) {
    auto executor = coro::SerialExecutor::create();
    auto scheduled = executor->schedule(coro::TaskOrValue<int> {7});
    EXPECT_TRUE(scheduled.isValue());
    EXPECT_EQ(executor->syncWait(awaitIt(std::move(scheduled))), 7);
}

TEST(TaskOrValue, ExceptionReachesTheAwaiter) {
    auto executor = coro::SerialExecutor::create();
    auto scheduled = executor->schedule(coro::TaskOrValue<int> {failing()});
    executor->drain();
    EXPECT_THROW(executor->syncWait(awaitIt(std::move(scheduled))), std::runtime_error);
}

TEST(TaskOrValue, AwaitedFromAnotherExecutor) {
    auto runner = coro::SerialExecutor::create();
    auto awaiter = coro::SerialExecutor::create();
    std::atomic<bool> started = false;
    auto scheduled = runner->schedule(coro::TaskOrValue<int> {work(started)});
    EXPECT_EQ(awaiter->syncWait(awaitIt(std::move(scheduled))), 42);
    EXPECT_TRUE(started);
}

TEST(TaskOrValue, VoidTaskAndValue) {
    auto executor = coro::SerialExecutor::create();
    std::atomic<bool> started = false;
    auto scheduled = executor->schedule(coro::TaskOrValue<void> {voidWork(started)});
    executor->drain();
    EXPECT_TRUE(started);
    executor->syncWait(awaitVoid(std::move(scheduled)));

    auto value = executor->schedule(coro::TaskOrValue<void> {});
    EXPECT_TRUE(value.isValue());
    executor->syncWait(awaitVoid(std::move(value)));
}

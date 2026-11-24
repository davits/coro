#include <coro/coro.hpp>
#include <coro/executors/serial_executor.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {

coro::Task<int> work(std::atomic<bool>& started) {
    started = true;
    co_return 42;
}

coro::Task<int> counted(std::atomic<int>& runs) {
    ++runs;
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

template <typename R>
coro::Task<R> awaitIt(coro::TaskOrValue<R> taskOrValue) {
    co_return co_await std::move(taskOrValue);
}

coro::Task<void> awaitVoid(coro::TaskOrValue<void> taskOrValue) {
    co_await std::move(taskOrValue);
}

static_assert(std::is_copy_constructible_v<coro::TaskOrValue<int>>);
static_assert(std::is_copy_assignable_v<coro::TaskOrValue<int>>);
static_assert(std::is_copy_constructible_v<coro::TaskOrValue<void>>);
static_assert(std::is_copy_assignable_v<coro::TaskOrValue<void>>);
static_assert(!std::is_copy_constructible_v<coro::TaskOrValue<std::unique_ptr<int>>>);
static_assert(std::is_move_constructible_v<coro::TaskOrValue<std::unique_ptr<int>>>);

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

TEST(TaskOrValue, CopiesOfATaskShareIt) {
    auto executor = coro::SerialExecutor::create();
    std::atomic<int> runs = 0;
    auto original = executor->schedule(coro::TaskOrValue<int> {counted(runs)});
    auto copy = original;
    EXPECT_TRUE(copy.isTask());
    EXPECT_EQ(executor->syncWait(awaitIt(std::move(original))), 42);
    EXPECT_EQ(executor->syncWait(awaitIt(std::move(copy))), 42);
    EXPECT_EQ(runs, 1);
}

TEST(TaskOrValue, CopyAssignedTaskIsShared) {
    auto executor = coro::SerialExecutor::create();
    std::atomic<int> runs = 0;
    auto original = executor->schedule(coro::TaskOrValue<int> {counted(runs)});
    coro::TaskOrValue<int> copy {7};
    copy = original;
    EXPECT_TRUE(copy.isTask());
    EXPECT_EQ(executor->syncWait(awaitIt(std::move(copy))), 42);
    EXPECT_EQ(executor->syncWait(awaitIt(std::move(original))), 42);
    EXPECT_EQ(runs, 1);
}

TEST(TaskOrValue, DroppedCopyLeavesTheTaskRunning) {
    auto executor = coro::SerialExecutor::create();
    std::atomic<int> runs = 0;
    auto original = executor->schedule(coro::TaskOrValue<int> {counted(runs)});
    {
        auto copy = original;
    }
    executor->drain();
    EXPECT_EQ(runs, 1);
    EXPECT_EQ(executor->syncWait(awaitIt(std::move(original))), 42);
}

TEST(TaskOrValue, CopiesOfAFailedTaskEachThrow) {
    auto executor = coro::SerialExecutor::create();
    auto original = executor->schedule(coro::TaskOrValue<int> {failing()});
    auto copy = original;
    executor->drain();
    EXPECT_THROW(executor->syncWait(awaitIt(std::move(original))), std::runtime_error);
    EXPECT_THROW(executor->syncWait(awaitIt(std::move(copy))), std::runtime_error);
}

TEST(TaskOrValue, CopiesOfAValueHoldTheirOwn) {
    auto executor = coro::SerialExecutor::create();
    coro::TaskOrValue<std::string> original {std::string {"value"}};
    auto copy = original;
    EXPECT_TRUE(copy.isValue());
    // awaiting a value moves it out of the awaited TaskOrValue only
    EXPECT_EQ(executor->syncWait(awaitIt(std::move(copy))), "value");
    EXPECT_EQ(executor->syncWait(awaitIt(std::move(original))), "value");
}

TEST(TaskOrValue, CopiesOfAVoidTaskShareIt) {
    auto executor = coro::SerialExecutor::create();
    std::atomic<bool> started = false;
    auto original = executor->schedule(coro::TaskOrValue<void> {voidWork(started)});
    auto copy = original;
    executor->syncWait(awaitVoid(std::move(original)));
    executor->syncWait(awaitVoid(std::move(copy)));
    EXPECT_TRUE(started);

    coro::TaskOrValue<void> value;
    auto valueCopy = value;
    EXPECT_TRUE(valueCopy.isValue());
    executor->syncWait(awaitVoid(std::move(valueCopy)));
}

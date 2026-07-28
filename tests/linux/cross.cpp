#include <coro/coro.hpp>
#include <coro/sleep.hpp>
#include <coro/executors/serial_executor.hpp>

#include <gtest/gtest.h>

coro::Task<void> sleepy() {
    co_await coro::sleep(100);
}

coro::Task<int> second() {
    co_await sleepy();
    co_return 42;
}

coro::Task<int> first() {
    auto e = coro::SerialExecutor::create();
    int r = co_await e->schedule(second());
    co_return r;
}

TEST(Cross, Executor) {
    auto e = coro::SerialExecutor::create();
    auto r = e->syncWait(first());
    EXPECT_EQ(r, 42);
}

class CustomData : public coro::UserData {
public:
    ~CustomData() {}
};

coro::Task<int> innerContextChecker(coro::StopToken expectedToken, coro::UserData::Ref expectedData) {
    auto token = co_await coro::currentStopToken;
    auto data = co_await coro::currentUserData;
    EXPECT_EQ(token, expectedToken);
    EXPECT_EQ(data, expectedData);
    co_return 42;
}

coro::Task<int> contextChecker(coro::StopToken expectedToken, coro::UserData::Ref expectedData) {
    auto token = co_await coro::currentStopToken;
    auto data = co_await coro::currentUserData;
    EXPECT_EQ(token, expectedToken);
    EXPECT_EQ(data, expectedData);

    auto executor = co_await coro::currentExecutor;

    coro::StopSource stopSource;
    auto newToken = stopSource.token();
    auto newData = std::make_shared<CustomData>();
    auto task = innerContextChecker(newToken, newData).setStopToken(newToken).setUserData(newData);
    int r = co_await executor->schedule(std::move(task));
    co_return r;
}

TEST(Cross, SplitContext) {
    auto e = coro::SerialExecutor::create();

    coro::StopSource stopSource;
    auto newToken = stopSource.token();
    auto newData = std::make_shared<CustomData>();
    auto task = contextChecker(newToken, newData);
    task.setStopToken(newToken);
    task.setUserData(newData);
    int r = e->syncWait(std::move(task));
    EXPECT_EQ(r, 42);
}

coro::Task<int> finished() {
    auto e = coro::SerialExecutor::create();
    auto task = e->schedule(second());
    co_await coro::sleep(200);
    EXPECT_EQ(task.ready(), true);
    co_return co_await std::move(task);
};

TEST(Cross, Finished) {
    auto e = coro::SerialExecutor::create();
    auto r = e->syncWait(finished());
    EXPECT_EQ(r, 42);
}

coro::Task<int> finishedMultipleAwaits() {
    auto e = coro::SerialExecutor::create();
    auto task = e->schedule(second());
    co_await coro::sleep(200);
    EXPECT_EQ(task.ready(), true);
    // task is finished on another executor, both awaits resume without suspension
    int r1 = co_await task;
    int r2 = co_await task;
    co_return r1 + r2;
};

TEST(Cross, FinishedMultipleAwaits) {
    auto e = coro::SerialExecutor::create();
    auto r = e->syncWait(finishedMultipleAwaits());
    EXPECT_EQ(r, 84);
}

coro::Task<int> multipleAwaitersOnOtherExecutor() {
    auto e = coro::SerialExecutor::create();
    auto task = e->schedule(second());
    // both awaiters live on the current executor, while the task runs on `e`,
    // so each of them is marked as external and scheduled once the task is finished
    auto results = co_await coro::all(task, task);
    co_return results[0] + results[1];
};

TEST(Cross, MultipleAwaiters) {
    auto e = coro::SerialExecutor::create();
    auto r = e->syncWait(multipleAwaitersOnOtherExecutor());
    EXPECT_EQ(r, 84);
}

std::atomic<int> sharedTaskCounter = 0;

coro::Task<int> sharedTask() {
    ++sharedTaskCounter;
    co_await coro::sleep(100);
    co_return 42;
}

coro::Task<int> awaitShared(coro::Task<int> task) {
    co_return co_await task;
}

// Single task awaited both from its own executor and from another one, so that one awaiter is
// a plain queue entry while the other is marked as external, and a single completion has to
// wake up both of them.
TEST(Cross, MixedAwaiterExecutors) {
    auto own = coro::SerialExecutor::create();
    auto other = coro::SerialExecutor::create();

    auto task = own->schedule(sharedTask());
    auto sameExecutor = own->future(awaitShared(task));
    auto crossExecutor = other->future(awaitShared(task));

    EXPECT_EQ(sameExecutor.get(), 42);
    EXPECT_EQ(crossExecutor.get(), 42);
    EXPECT_EQ(sharedTaskCounter, 1);
}

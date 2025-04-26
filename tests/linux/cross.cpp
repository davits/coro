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
    auto newToken = stopSource.get_token();
    auto newData = std::make_shared<CustomData>();
    auto task = innerContextChecker(newToken, newData);
    task.setStopToken(newToken);
    task.setUserData(newData);
    int r = co_await executor->schedule(std::move(task));
    co_return r;
}

TEST(Cross, SplitContext) {
    auto e = coro::SerialExecutor::create();

    coro::StopSource stopSource;
    auto newToken = stopSource.get_token();
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

#include <coro/coro.hpp>
#include <coro/executors/serial_executor.hpp>
#include <coro/sleep.hpp>

#include <gtest/gtest.h>

coro::Task<void> simple0() {
    using namespace std::chrono_literals;
    co_await coro::sleep(100);
    auto stop = co_await coro::currentStopToken;
    stop.throwIfStopped();
    co_return;
}

coro::Task<int> simple1() {
    co_await simple0();
    co_return 1;
}

coro::Task<int> simple2() {
    int x = co_await simple1();
    co_return x + 1;
}

coro::Task<double> simple() {
    int x1 = co_await simple1();
    int x2 = co_await simple2();
    co_return static_cast<double>(x1) / static_cast<double>(x2);
}

struct Cancelled {};

TEST(Stop, Normal) {
    coro::StopSource ss1;
    coro::StopSource ss2 {std::make_exception_ptr(Cancelled {})};
    coro::StopSource ss3;

    auto executor = coro::SerialExecutor::create();
    auto task1 = simple();
    task1.setStopToken(ss1.token());
    auto future1 = executor->future(std::move(task1));

    auto task2 = simple();
    task2.setStopToken(ss2.token());
    auto future2 = executor->future(std::move(task2));

    auto task3 = simple();
    task3.setStopToken(ss3.token());
    auto future3 = executor->future(std::move(task3));

    ss1.requestStop();
    ss2.requestStop();

    EXPECT_THROW(future1.get(), coro::StopError);
    EXPECT_THROW(future2.get(), Cancelled);
    EXPECT_DOUBLE_EQ(future3.get(), 0.5);
}

TEST(Stop, StoppedToken) {
    coro::StopSource ss;
    ss.requestStop();

    auto executor = coro::SerialExecutor::create();
    auto task1 = simple();
    task1.setStopToken(ss.token());
    executor->schedule(std::move(task1));

    auto task2 = simple();
    task2.setStopToken(ss.token());
    auto future = executor->future(std::move(task2));

    EXPECT_THROW(future.get(), coro::StopError);
}

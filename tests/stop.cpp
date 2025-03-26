#include <coro/coro.hpp>
#include <coro/executors/serial_executor.hpp>
#include <coro/helpers/timeout.hpp>

#include <gtest/gtest.h>
#include <chrono>
#include <thread>

struct Cancelled {};

coro::Task<void> simple0() {
    using namespace std::chrono_literals;
    co_await coro::timeout(100);
    auto stop = co_await coro::currentStopToken;
    if (stop.stop_requested()) {
        throw Cancelled {};
    }
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

TEST(Stop, Builtin) {
    coro::StopSource ss1;
    coro::StopSource ss2;
    coro::StopSource ss3;
    auto t1 = std::thread {[&]() {
        auto task = simple();
        task.setStopToken(ss1.get_token());
        auto e1 = coro::SerialExecutor::create();
        EXPECT_THROW(e1->run(task), Cancelled);
    }};
    auto t2 = std::thread {[&]() {
        auto task = simple();
        task.setStopToken(ss2.get_token());
        auto e2 = coro::SerialExecutor::create();
        EXPECT_THROW(e2->run(task), Cancelled);
    }};
    auto t3 = std::thread {[&]() {
        auto task = simple();
        task.setStopToken(ss3.get_token());
        auto e3 = coro::SerialExecutor::create();
        auto d = e3->run(task);
        EXPECT_DOUBLE_EQ(d, 0.5);
    }};
    ss1.request_stop();
    ss2.request_stop();
    t1.join();
    t2.join();
    t3.join();
}

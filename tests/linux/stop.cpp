#include <coro/coro.hpp>
#include <coro/executors/serial_executor.hpp>
#include <coro/sleep.hpp>

#include <gtest/gtest.h>

struct Cancelled {};

int exceptions[5][3] = {{0}, {0}, {0}};

coro::Task<void> simple0(const int idx) {
    try {
        using namespace std::chrono_literals;
        co_await coro::sleep(100);
    } catch (const coro::StopError&) {
        ++exceptions[idx][0];
    } catch (const Cancelled&) {
        ++exceptions[idx][0];
    } catch (...) {
        EXPECT_FALSE("Invalid exception was thrown.");
    }
}

coro::Task<int> simple1(const int idx) {
    try {
        co_await simple0(idx);
        co_return 1;
    } catch (const coro::StopError&) {
        ++exceptions[idx][1];
    } catch (const Cancelled&) {
        ++exceptions[idx][1];
    } catch (...) {
        EXPECT_FALSE("Invalid exception was thrown.");
    }
    co_return 2;
}

coro::Task<int> simple2(const int idx) {
    try {
        co_await simple0(idx);
        co_return 2;
    } catch (const coro::StopError&) {
        ++exceptions[idx][2];
    } catch (const Cancelled&) {
        ++exceptions[idx][2];
    } catch (...) {
        EXPECT_FALSE("Invalid exception was thrown.");
    }
    co_return 3;
}

coro::Task<double> simple(const int idx) {
    int x1 = co_await simple1(idx);
    int x2 = co_await simple2(idx);
    co_return static_cast<double>(x1) / static_cast<double>(x2);
}

TEST(Stop, Normal) {
    coro::StopSource ss0;
    coro::StopSource ss1 {std::make_exception_ptr(Cancelled {})};
    coro::StopSource ss2;

    auto executor = coro::SerialExecutor::create();
    auto task0 = simple(0);
    task0.setStopToken(ss0.token());
    auto future0 = executor->future(std::move(task0));
    auto future1 = executor->future(simple(1).setStopToken(ss1.token()));
    auto future2 = executor->future(simple(2).setStopToken(ss2.token()));

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(70ms);
    ss0.requestStop();
    EXPECT_THROW(future0.get(), coro::StopError);
    EXPECT_EQ(exceptions[0][0], 1);
    EXPECT_EQ(exceptions[0][1], 1);
    EXPECT_EQ(exceptions[0][2], 0);

    std::this_thread::sleep_for(70ms);
    ss1.requestStop();
    EXPECT_THROW(future1.get(), Cancelled);
    EXPECT_EQ(exceptions[1][0], 1);
    EXPECT_EQ(exceptions[1][1], 0);
    EXPECT_EQ(exceptions[1][2], 1);

    EXPECT_DOUBLE_EQ(future2.get(), 0.5);
    EXPECT_EQ(exceptions[2][0], 0);
    EXPECT_EQ(exceptions[2][1], 0);
    EXPECT_EQ(exceptions[2][2], 0);
}

TEST(Stop, StoppedToken) {
    coro::StopSource ss;
    ss.requestStop();

    auto executor = coro::SerialExecutor::create();
    auto task3 = simple(3);
    task3.setStopToken(ss.token());
    // not getting the result does not throw
    executor->schedule(std::move(task3));

    auto task2 = simple(4);
    task2.setStopToken(ss.token());
    auto future = executor->future(std::move(task2));

    EXPECT_THROW(future.get(), coro::StopError);
    EXPECT_EQ(exceptions[4][0], 1);
    EXPECT_EQ(exceptions[4][1], 1);
    EXPECT_EQ(exceptions[4][2], 0);
}

coro::Task<int> resetTest() {
    co_await coro::sleep(100);
    co_return 42;
}

TEST(Stop, Reset) {
    coro::StopSource ss;
    coro::StopSource clone = ss;

    auto executor = coro::SerialExecutor::create();
    auto future0 = executor->future(resetTest().setStopToken(ss.token()));
    auto future1 = executor->future(resetTest().setStopToken(clone.token()));
    clone.reset();
    auto future2 = executor->future(resetTest().setStopToken(clone.token()));

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(50ms);
    ss.requestStop();
    EXPECT_THROW(future0.get(), coro::StopError);
    EXPECT_THROW(future1.get(), coro::StopError);
    EXPECT_EQ(future2.get(), 42);
}

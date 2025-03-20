#include <coro/coro.hpp>
#include <coro/executors/serial_executor.hpp>
#include <coro/helpers/timeout.hpp>
#include <coro/helpers/all.hpp>

#include <vector>
#include <gtest/gtest.h>

coro::Task<void> simple0() {
    co_await coro::timeout(100);
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
    auto t1 = simple1();
    auto t2 = simple2();
    auto r = co_await coro::all(std::move(t1), std::move(t2));
    co_return static_cast<double>(r[0]) / static_cast<double>(r[1]);
}

TEST(Simple, Builtin) {
    auto e = coro::SerialExecutor::create();
    auto task = simple();
    auto d = e->run(task);
    EXPECT_DOUBLE_EQ(d, 0.5);
}

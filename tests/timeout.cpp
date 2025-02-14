#include <coro/coro.hpp>

#include <gtest/gtest.h>

coro::Task<void> simple0() {
    co_await coro::timeout(500);
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

TEST(SimpleTest, Builtin) {
    auto e = coro::SerialExecutor::create();
    auto task = simple();
    auto d = e->run(task);
    EXPECT_DOUBLE_EQ(d, 0.5);
}

#include <coro/coro.hpp>
#include <coro/serial_executor.hpp>

#include <gtest/gtest.h>

coro::Task<void> simple0() {
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

struct MyError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

coro::Task<int> errorenous0() {
    throw MyError {"testing exceptions"};
    co_return 1;
}

coro::Task<int> errorenous1() {
    co_return co_await errorenous0();
}

coro::Task<int> try_errorenous2() {
    int r = 0;
    try {
        r = co_await errorenous1();
    } catch (const MyError& err) {
        r = -1;
    }
    co_return r;
}

coro::Task<int> errorenous2() {
    co_return co_await errorenous1();
}

TEST(SimpleTest, Error) {
    auto e = coro::SerialExecutor::create();
    auto task = try_errorenous2();
    auto result = e->run(task);
    EXPECT_EQ(result, -1);
    EXPECT_THROW(
        {
            auto e = coro::SerialExecutor::create();
            auto task = errorenous2();
            [[maybe_unused]] auto result = e->run(task);
        },
        MyError);
}

coro::Task<void> error_void0() {
    throw MyError {"exception from void function"};
    co_return;
}

coro::Task<void> error_void1() {
    co_await error_void0();
}

coro::Task<void> error_void2() {
    co_await error_void1();
}

TEST(SimpleTest, ThrowingVoidReturn) {
    EXPECT_THROW(
        {
            auto e = coro::SerialExecutor::create();
            auto task = error_void2();
            e->run(task);
        },
        MyError);
}

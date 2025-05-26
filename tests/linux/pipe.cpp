#include <coro/coro.hpp>
#include <coro/sync/pipe.hpp>
#include <coro/executors/serial_executor.hpp>
#include <coro/sleep.hpp>

#include <gtest/gtest.h>

coro::Task<int> consumer(coro::Pipe<int>& pipe) {
    int first = co_await pipe.read();
    int second = co_await pipe.read();
    co_return first + second;
}

coro::Task<void> producer(coro::Pipe<int>& pipe) {
    co_await coro::sleep(50);
    pipe.write(11);
    co_await coro::sleep(50);
    pipe.write(22);
}

TEST(Pipe, Simple) {
    coro::Pipe<int> pipe;
    auto executor = coro::SerialExecutor::create();
    executor->schedule(producer(pipe));
    int result = executor->syncWait(consumer(pipe));
    EXPECT_EQ(result, 33);
}

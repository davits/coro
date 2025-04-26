#include <coro/coro.hpp>
#include <coro/executors/serial_executor.hpp>
#include <coro/sleep.hpp>
#include <coro/helpers/all.hpp>

#include <vector>
#include <gtest/gtest.h>

template <size_t N>
coro::Task<int> sleepyWork() {
    int r = co_await sleepyWork<N - 1>();
    co_return r + 1;
}

template <>
coro::Task<int> sleepyWork<0>() {
    co_await coro::sleep(100);
    co_return 0;
}

coro::Task<int> work() {
    auto r = co_await coro::all(sleepyWork<1>(), sleepyWork<2>(), sleepyWork<3>(), sleepyWork<4>());
    co_return r[0] + r[1] + r[2] + r[3];
}

TEST(Simple, Builtin) {
    auto executor = coro::SerialExecutor::create();
    auto future = executor->future(work());
    using namespace std::chrono_literals;
    // make sure sleep is working and the future is not ready right away
    EXPECT_EQ(future.wait_for(50ms), std::future_status::timeout);
    // make sure all 4 async sleeps are sleeping in parallel and the overall
    // execution is complete in roughly 150ms rather then 400ms.
    EXPECT_EQ(future.wait_for(100ms), std::future_status::ready);
    EXPECT_EQ(future.get(), 10);
}

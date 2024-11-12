#include <coro/coro.hpp>

#include <gtest/gtest.h>
#include <chrono>
#include <thread>

namespace coro {
template <>
struct await_ready_trait<StopToken> {
    static ReadyAwaitable<StopToken> await_transform(Executor* executor, const StopToken&) {
        auto se = static_cast<CancellableSerialExecutor*>(executor);
        return se->stopToken();
    }
};
} // namespace coro

coro::StopToken stopToken;
struct Cancelled {};

coro::Task<void> simple0() {
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(0.1s);
    auto stop = co_await stopToken;
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

TEST(StopTest, Builtin) {
    coro::StopSource ss1;
    coro::StopSource ss2;
    coro::StopSource ss3;
    auto t1 = std::thread {[&]() {
        auto task = simple();
        coro::CancellableSerialExecutor e1 {ss1.get_token()};
        EXPECT_THROW(e1.run(task), Cancelled);
    }};
    auto t2 = std::thread {[&]() {
        auto task = simple();
        coro::CancellableSerialExecutor e2 {ss2.get_token()};
        EXPECT_THROW(e2.run(task), Cancelled);
    }};
    auto t3 = std::thread {[&]() {
        auto task = simple();
        coro::CancellableSerialExecutor e3 {ss3.get_token()};
        auto d = e3.run(task);
        EXPECT_DOUBLE_EQ(d, 0.5);
    }};
    ss1.request_stop();
    ss2.request_stop();
    t1.join();
    t2.join();
    t3.join();
}

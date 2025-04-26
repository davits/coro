#include <coro/coro.hpp>
#include <coro/executors/serial_executor.hpp>
#include <coro/sleep.hpp>

#include <gtest/gtest.h>

class CountingExecutor : public coro::SerialExecutor {
public:
    using Ref = std::shared_ptr<CountingExecutor>;
    static Ref create(std::shared_ptr<std::atomic<size_t>> counter) {
        return std::make_shared<CountingExecutor>(Tag {}, std::move(counter));
    }

protected:
    using Tag = coro::SerialExecutor::Tag;

public:
    CountingExecutor(Tag, std::shared_ptr<std::atomic<size_t>> counter)
        : coro::SerialExecutor(Tag {})
        , _counter(std::move(counter)) {
        ++*_counter;
    }

    ~CountingExecutor() {
        --*_counter;
    }

private:
    std::shared_ptr<std::atomic<size_t>> _counter;
};

template <size_t N>
coro::Task<int> foo(bool timeout) {
    co_return co_await foo<N - 1>(timeout);
}

template <>
coro::Task<int> foo<0>(bool timeout) {
    if (timeout) {
        co_await coro::sleep(500);
    }
    co_return 42;
}

TEST(Simple, Lifetime) {
    auto counter = std::make_shared<std::atomic<size_t>>(0);
    int result = 0;
    {
        auto executor = CountingExecutor::create(counter);
        auto task = foo<5>(false);
        result = executor->syncWait(std::move(task));
        EXPECT_EQ(*counter, 1);
    }
    // Make sure that executor was destroyed despite the fact that there are circular shared references
    // between executor and task promises.
    EXPECT_EQ(*counter, 0);
    EXPECT_EQ(result, 42);
}

TEST(NoReference, Lifetime) {
    auto counter = std::make_shared<std::atomic<size_t>>(0);
    int result = 0;

    auto executor = CountingExecutor::create(counter);
    auto task = foo<5>(true);
    auto future = executor->future(std::move(task));
    EXPECT_EQ(*counter, 1);
    executor.reset();
    EXPECT_EQ(*counter, 1); // still there because there is a scheduled task
    using namespace std::chrono_literals;
    EXPECT_EQ(future.wait_for(200ms), std::future_status::timeout);
    // future is still not satisfied after 200ms and the executor is still alive,
    // because there is a task <-> executor cyclic dependency keeping it alive
    EXPECT_EQ(*counter, 1);
    result = future.get(); // wait for future to fulfill
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(*counter, 0); // executor was destroyed
    EXPECT_EQ(result, 42);
}

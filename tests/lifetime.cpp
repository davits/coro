#include <coro/coro.hpp>
#include <coro/executors/serial_executor.hpp>
#include <coro/helpers/timeout.hpp>

#include <gtest/gtest.h>

class CountingExecutor : public coro::SerialExecutor {
public:
    using Ref = std::shared_ptr<CountingExecutor>;
    static Ref create(std::shared_ptr<size_t> counter) {
        return std::make_shared<CountingExecutor>(Tag {}, std::move(counter));
    }

protected:
    using Tag = coro::SerialExecutor::Tag;

public:
    CountingExecutor(Tag, std::shared_ptr<size_t> counter)
        : coro::SerialExecutor(Tag {})
        , _counter(std::move(counter)) {
        ++*_counter;
    }

    ~CountingExecutor() {
        --*_counter;
    }

private:
    std::shared_ptr<size_t> _counter;
};

template <size_t N>
coro::Task<int> foo(bool timeout) {
    co_return co_await foo<N - 1>(timeout);
}

template <>
coro::Task<int> foo<0>(bool timeout) {
    if (timeout) {
        co_await coro::timeout(500);
    }
    co_return 42;
}

TEST(Simple, Lifetime) {
    auto counter = std::make_shared<size_t>(0);
    int result = 0;
    {
        auto executor = CountingExecutor::create(counter);
        auto task = foo<5>(false);
        result = executor->run(task);
        EXPECT_EQ(*counter, 1);
    }
    // Make sure that executor was destroyed despite the fact that there are circular shared references
    // between executor and task promises.
    EXPECT_EQ(*counter, 0);
    EXPECT_EQ(result, 42);
}

TEST(NoReference, Lifetime) {
    auto counter = std::make_shared<size_t>(0);
    int result = 0;

    auto executor = CountingExecutor::create(counter);
    auto task = foo<5>(true);
    auto future = executor->future(std::move(task));
    EXPECT_EQ(*counter, 1);
    auto raw = executor.get();
    executor.reset();
    EXPECT_EQ(*counter, 1); // still there because there is a scheduled task
    std::thread thread {[raw] { raw->runScheduled(); }};
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(200ms);
    EXPECT_EQ(*counter, 1); // still there because there is an external timeout task holding executor
    result = future.get();
    EXPECT_EQ(*counter, 0); // executor was destroyed
    EXPECT_EQ(result, 42);
    thread.join();
}

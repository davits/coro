#include <coro/coro.hpp>
#include <coro/helpers/shared_task.hpp>
#include <coro/executors/serial_executor.hpp>
#include <coro/sleep.hpp>

#include <gtest/gtest.h>

coro::Task<int> produce(int& runCount, int value) {
    ++runCount;
    co_await coro::sleep(10);
    co_return value;
}

coro::Task<void> produceVoid(int& runCount) {
    ++runCount;
    co_await coro::sleep(10);
    co_return;
}

struct TestError {};

coro::Task<int> failing(int& runCount) {
    ++runCount;
    co_await coro::sleep(10);
    throw TestError {};
}

TEST(SharedTask, SingleAwaiter) {
    int runCount = 0;
    auto shared = coro::share(produce(runCount, 42));

    auto awaiter = [](coro::SharedTask<int> shared) -> coro::Task<int> {
        const int& result = co_await shared;
        co_return result;
    };

    auto executor = coro::SerialExecutor::create();
    EXPECT_EQ(executor->syncWait(awaiter(shared)), 42);
    EXPECT_EQ(runCount, 1);
}

TEST(SharedTask, MultipleAwaitersRunOnce) {
    int runCount = 0;
    auto shared = coro::share(produce(runCount, 7));

    auto awaiter = [](coro::SharedTask<int> shared, int& sum) -> coro::Task<void> {
        sum += co_await shared;
    };

    int sum = 0;
    auto executor = coro::SerialExecutor::create();
    executor->syncWait(coro::all(awaiter(shared, sum), awaiter(shared, sum), awaiter(shared, sum)));
    EXPECT_EQ(runCount, 1);
    EXPECT_EQ(sum, 21);
}

TEST(SharedTask, AwaitAfterCompletion) {
    int runCount = 0;
    auto shared = coro::share(produce(runCount, 5));

    auto awaiter = [](coro::SharedTask<int> shared) -> coro::Task<int> { co_return co_await shared; };

    auto executor = coro::SerialExecutor::create();
    EXPECT_FALSE(shared.ready());
    EXPECT_EQ(executor->syncWait(awaiter(shared)), 5);
    EXPECT_TRUE(shared.ready());
    // Second await resumes immediately with the cached value; the task doesn't rerun.
    EXPECT_EQ(executor->syncWait(awaiter(shared)), 5);
    EXPECT_EQ(runCount, 1);
}

TEST(SharedTask, SameResultReference) {
    int runCount = 0;
    auto shared = coro::share(produce(runCount, 9));

    auto awaiter = [](coro::SharedTask<int> shared, const int*& address) -> coro::Task<void> {
        const int& result = co_await shared;
        address = &result;
    };

    const int* first = nullptr;
    const int* second = nullptr;
    const int* third = nullptr;
    auto executor = coro::SerialExecutor::create();
    executor->syncWait(coro::all(awaiter(shared, first), awaiter(shared, second), awaiter(shared, third)));
    EXPECT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_EQ(first, third);
}

TEST(SharedTask, ExceptionRethrownToEveryAwaiter) {
    int runCount = 0;
    auto shared = coro::share(failing(runCount));

    auto awaiter = [](coro::SharedTask<int> shared, int& caught) -> coro::Task<void> {
        try {
            co_await shared;
        } catch (const TestError&) {
            ++caught;
        }
    };

    int caught = 0;
    auto executor = coro::SerialExecutor::create();
    executor->syncWait(coro::all(awaiter(shared, caught), awaiter(shared, caught), awaiter(shared, caught)));
    EXPECT_EQ(runCount, 1);
    EXPECT_EQ(caught, 3);
    // Late awaiter gets the same cached exception.
    executor->syncWait(awaiter(shared, caught));
    EXPECT_EQ(caught, 4);
    EXPECT_EQ(runCount, 1);
}

TEST(SharedTask, VoidResult) {
    int runCount = 0;
    auto shared = coro::share(produceVoid(runCount));

    auto awaiter = [](coro::SharedTask<void> shared) -> coro::Task<void> { co_await shared; };

    auto executor = coro::SerialExecutor::create();
    executor->syncWait(coro::all(awaiter(shared), awaiter(shared), awaiter(shared)));
    executor->syncWait(awaiter(shared));
    EXPECT_EQ(runCount, 1);
    EXPECT_TRUE(shared.ready());
}

TEST(SharedTask, EmptyAndReset) {
    coro::SharedTask<int> empty;
    EXPECT_FALSE(static_cast<bool>(empty));
    EXPECT_FALSE(empty.ready());

    // Wrapping an empty task yields an empty SharedTask.
    coro::SharedTask<int> fromEmpty {coro::Task<int> {}};
    EXPECT_FALSE(static_cast<bool>(fromEmpty));

    int runCount = 0;
    auto shared = coro::share(produce(runCount, 1));
    EXPECT_TRUE(static_cast<bool>(shared));
    auto copy = shared;
    shared.reset();
    EXPECT_FALSE(static_cast<bool>(shared));
    EXPECT_TRUE(static_cast<bool>(copy));

    auto awaiter = [](coro::SharedTask<int> shared) -> coro::Task<int> { co_return co_await shared; };
    auto executor = coro::SerialExecutor::create();
    EXPECT_EQ(executor->syncWait(awaiter(copy)), 1);
}

TEST(SharedTask, StopTokenAffectsOnlyAwaiter) {
    int runCount = 0;
    auto shared = coro::share(produce(runCount, 3));

    auto awaiter = [](coro::SharedTask<int> shared) -> coro::Task<int> { co_return co_await shared; };

    auto executor = coro::SerialExecutor::create();
    coro::StopSource ss;
    ss.requestStop();
    EXPECT_THROW(executor->syncWait(awaiter(shared).setStopToken(ss.token())), coro::StopError);
    // The shared computation itself is unaffected; a fresh awaiter still gets the value.
    EXPECT_EQ(executor->syncWait(awaiter(shared)), 3);
}

#include <coro/coro.hpp>
#include <coro/sleep.hpp>
#include <coro/executors/serial_executor.hpp>

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

TEST(Simple, Builtin) {
    auto e = coro::SerialExecutor::create();
    auto d = e->syncWait(simple());
    EXPECT_DOUBLE_EQ(d, 0.5);
}

coro::Task<int> calculate() {
    co_await coro::sleep(10);
    co_return 42;
}

coro::TaskOrValue<int> cachedCalculation(bool useCache) {
    if (useCache) {
        return 21;
    }
    return calculate();
}

coro::Task<int> testTaskOrValue(bool useCache) {
    co_return co_await cachedCalculation(useCache);
}

size_t calculateVoidCounter = 0;
size_t calculateVoidCacheCounter = 0;

coro::Task<void> calculateVoid() {
    ++calculateVoidCounter;
    co_await coro::sleep(10);
}

coro::TaskOrValue<void> cachedVoidCalculation(bool useCache) {
    if (useCache) {
        ++calculateVoidCacheCounter;
        return {};
    }
    return calculateVoid();
}

coro::Task<void> testTaskOrValueVoid(bool useCache) {
    co_await cachedVoidCalculation(useCache);
}

TEST(Simple, TaskOrValue) {
    auto e = coro::SerialExecutor::create();
    EXPECT_EQ(e->syncWait(testTaskOrValue(true)), 21);
    EXPECT_EQ(e->syncWait(testTaskOrValue(false)), 42);

    e->syncWait(testTaskOrValueVoid(false));
    EXPECT_EQ(calculateVoidCounter, 1);
    EXPECT_EQ(calculateVoidCacheCounter, 0);
    e->syncWait(testTaskOrValueVoid(true));
    EXPECT_EQ(calculateVoidCounter, 1);
    EXPECT_EQ(calculateVoidCacheCounter, 1);
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

TEST(Simple, Error) {
    auto e = coro::SerialExecutor::create();
    auto result = e->syncWait(try_errorenous2());
    EXPECT_EQ(result, -1);
    EXPECT_THROW(
        {
            auto e = coro::SerialExecutor::create();
            [[maybe_unused]] auto result = e->syncWait(errorenous2());
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

TEST(Simple, ThrowingVoidReturn) {
    EXPECT_THROW(
        {
            auto e = coro::SerialExecutor::create();
            auto task = error_void2();
            e->syncWait(error_void2());
        },
        MyError);
}

coro::Task<std::vector<int>> multipleAwaiters() {
    auto task = calculate();
    auto result = co_await coro::all(task, task);
    co_return result;
}

TEST(Simple, MultipleAwaiters) {
    auto e = coro::SerialExecutor::create();
    auto result = e->syncWait(multipleAwaiters());
    EXPECT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 42);
    EXPECT_EQ(result[1], 42);
}

coro::Task<void> sleepAndCount(std::atomic<int>& counter) {
    co_await coro::sleep(20);
    ++counter;
}

// blocks the executor thread, so the task is still executing while the queue is already empty
coro::Task<void> blockAndCount(std::atomic<int>& counter) {
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(50ms);
    ++counter;
    co_return;
}

TEST(Simple, Drain) {
    auto e = coro::SerialExecutor::create();
    std::atomic<int> counter = 0;
    for (int i = 0; i < 5; ++i) {
        e->schedule(sleepAndCount(counter));
    }
    // queued tasks are not done yet, sleeping ones are registered as external
    e->drain();
    EXPECT_EQ(counter, 5);
    // already idle, must return right away
    e->drain();
    EXPECT_EQ(counter, 5);

    counter = 0;
    e->schedule(blockAndCount(counter));
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(10ms);
    // queue is empty by now, but the task is still executing
    e->drain();
    EXPECT_EQ(counter, 1);
}

size_t countedCalculateCounter = 0;

coro::Task<int> countedCalculate() {
    ++countedCalculateCounter;
    co_await coro::sleep(10);
    co_return 42;
}

coro::Task<int> awaitTwice() {
    auto task = countedCalculate();
    // second co_await hits the already finished task, so it is resumed without suspension
    int first = co_await task;
    int second = co_await task;
    co_return first + second;
}

TEST(Simple, RepeatedAwait) {
    countedCalculateCounter = 0;
    auto e = coro::SerialExecutor::create();
    EXPECT_EQ(e->syncWait(awaitTwice()), 84);
    // task body is executed only once, no matter how many times it is awaited
    EXPECT_EQ(countedCalculateCounter, 1);
}

coro::Task<std::vector<int>> threeAwaiters() {
    auto task = countedCalculate();
    co_return co_await coro::all(task, task, task);
}

TEST(Simple, MultipleAwaitersRunOnce) {
    countedCalculateCounter = 0;
    auto e = coro::SerialExecutor::create();
    auto result = e->syncWait(threeAwaiters());
    EXPECT_EQ(result, (std::vector<int> {42, 42, 42}));
    EXPECT_EQ(countedCalculateCounter, 1);
}

coro::TaskOrValue<int> wrapTask(coro::Task<int> task) {
    return std::move(task);
}

coro::Task<int> awaitFinishedTaskOrValue() {
    auto task = countedCalculate();
    int first = co_await task;
    // TaskOrValue does not check whether the wrapped task is finished in its await_ready(),
    // so awaiting a finished task must be handled by the await_suspend() result
    int second = co_await wrapTask(task);
    co_return first + second;
}

TEST(Simple, FinishedTaskOrValue) {
    countedCalculateCounter = 0;
    auto e = coro::SerialExecutor::create();
    EXPECT_EQ(e->syncWait(awaitFinishedTaskOrValue()), 84);
    EXPECT_EQ(countedCalculateCounter, 1);
}

size_t throwingCounter = 0;

coro::Task<int> throwingCalculate() {
    ++throwingCounter;
    co_await coro::sleep(10);
    throw MyError {"multiple awaiters"};
}

coro::Task<int> catchTwice() {
    auto task = throwingCalculate();
    int caught = 0;
    for (int i = 0; i < 2; ++i) {
        try {
            co_await task;
        } catch (const MyError&) {
            ++caught;
        }
    }
    co_return caught;
}

coro::Task<void> allOfThrowing() {
    auto task = throwingCalculate();
    co_await coro::all(task, task);
}

TEST(Simple, MultipleAwaitersError) {
    {
        throwingCounter = 0;
        auto e = coro::SerialExecutor::create();
        // stored exception is rethrown for every awaiter, including the ones awaiting a finished task
        EXPECT_EQ(e->syncWait(catchTwice()), 2);
        EXPECT_EQ(throwingCounter, 1);
    }
    {
        throwingCounter = 0;
        auto e = coro::SerialExecutor::create();
        EXPECT_THROW(e->syncWait(allOfThrowing()), MyError);
        EXPECT_EQ(throwingCounter, 1);
    }
}

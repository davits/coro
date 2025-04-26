#include <coro/coro.hpp>
#include <coro/helpers/all.hpp>
#include <coro/executors/serial_executor.hpp>

#include <gtest/gtest.h>

coro::Task<void> increment(int& counter) {
    ++counter;
    co_return;
};

coro::Task<int> returnNumber(int x) {
    co_return x;
};

struct TestError {
    TestError()
        : id {next++} {}
    int id;
    static int next;
};

int TestError::next = 0;

coro::Task<void> failingTask(int& failingTaskCount) {
    co_await increment(failingTaskCount);
    throw TestError {};
}

coro::Task<int> failingIntTask(int& failingTaskCount) {
    co_await increment(failingTaskCount);
    throw TestError {};
}

TEST(CoroAll, MultipleIntTasks) {
    std::vector<coro::Task<int>> tasks;
    tasks.push_back(returnNumber(10));
    tasks.push_back(returnNumber(20));
    tasks.push_back(returnNumber(30));

    auto e = coro::SerialExecutor::create();
    auto results = e->syncWait(coro::all(std::move(tasks)));
    EXPECT_EQ(results[0], 10);
    EXPECT_EQ(results[1], 20);
    EXPECT_EQ(results[2], 30);
}

TEST(CoroAll, MixedTasksAndSyncWait) {
    int voidTaskCount = 0;

    auto executor = coro::SerialExecutor::create();
    auto task = coro::all(increment(voidTaskCount), returnNumber(123), increment(voidTaskCount));
    auto results = executor->syncWait(std::move(task));

    EXPECT_EQ(voidTaskCount, 2);
    EXPECT_EQ(std::any_cast<int>(results[1]), 123);

    voidTaskCount = 0;
    auto task1 = coro::all(increment(voidTaskCount), increment(voidTaskCount), increment(voidTaskCount));
    executor->syncWait(std::move(task1));
    EXPECT_EQ(voidTaskCount, 3);
}

coro::Task<void> executeMixedTasks(std::vector<std::any>& results, int& voidTaskCount) {
    results = co_await coro::all(increment(voidTaskCount), returnNumber(123), increment(voidTaskCount));
}

coro::Task<void> executeIntTasks(std::vector<int>& results) {
    std::vector<coro::Task<int>> tasks;
    tasks.push_back(returnNumber(10));
    tasks.push_back(returnNumber(20));
    tasks.push_back(returnNumber(30));

    results = co_await coro::all(std::move(tasks));
}

TEST(CoroAll, NestedCoroAllCalls) {
    std::vector<int> results1;
    std::vector<std::any> results2;
    int voidTaskCount = 0;

    std::vector<coro::Task<void>> tasks;
    tasks.push_back(executeIntTasks(results1));
    tasks.push_back(executeMixedTasks(results2, voidTaskCount));
    tasks.push_back(increment(voidTaskCount));

    auto all_tasks = coro::all(std::move(tasks));
    auto executor = coro::SerialExecutor::create();
    executor->syncWait(std::move(all_tasks));

    EXPECT_EQ(voidTaskCount, 3);
    EXPECT_EQ(results1[0], 10);
    EXPECT_EQ(results1[1], 20);
    EXPECT_EQ(results1[2], 30);
    EXPECT_EQ(std::any_cast<int>(results2[1]), 123);
}

TEST(CoroAll, FailingCalls) {
    {
        int counter = 0;
        int failCount = 0;
        auto task = coro::all(increment(counter), failingTask(failCount), failingTask(failCount), increment(counter));
        auto executor = coro::SerialExecutor::create();
        try {
            executor->syncWait(std::move(task));
            FAIL();
        } catch (const TestError& err) {
            EXPECT_EQ(err.id, 0);
        }
        EXPECT_EQ(counter, 2);
        EXPECT_EQ(failCount, 2);
    }
    {
        int counter = 0;
        int failCount = 0;
        std::vector<coro::Task<void>> tasks;
        tasks.push_back(increment(counter));
        tasks.push_back(failingTask(failCount));
        tasks.push_back(failingTask(failCount));
        tasks.push_back(increment(counter));
        auto task = coro::all(std::move(tasks));
        auto executor = coro::SerialExecutor::create();
        try {
            executor->syncWait(std::move(task));
            FAIL();
        } catch (const TestError& err) {
            EXPECT_EQ(err.id, 2);
        }
        EXPECT_EQ(counter, 2);
        EXPECT_EQ(failCount, 2);
    }
    {
        int counter = 0;
        int failCount = 0;
        auto task =
            coro::all(increment(counter), failingIntTask(failCount), failingIntTask(failCount), increment(counter));
        auto executor = coro::SerialExecutor::create();
        try {
            std::vector<std::any> result = executor->syncWait(std::move(task));
            FAIL();
        } catch (const TestError& err) {
            EXPECT_EQ(err.id, 4);
        }
        EXPECT_EQ(counter, 2);
        EXPECT_EQ(failCount, 2);
    }
    {
        int number = 0;
        int failCount = 0;
        auto task =
            coro::all(returnNumber(number), failingIntTask(failCount), failingIntTask(failCount), returnNumber(number));
        auto executor = coro::SerialExecutor::create();
        try {
            std::vector<int> result = executor->syncWait(std::move(task));
            FAIL();
        } catch (const TestError& err) {
            EXPECT_EQ(err.id, 6);
        }
        EXPECT_EQ(failCount, 2);
    }
    {
        int number = 0;
        int failCount = 0;
        std::vector<coro::Task<int>> tasks;
        tasks.push_back(returnNumber(number));
        tasks.push_back(failingIntTask(failCount));
        tasks.push_back(failingIntTask(failCount));
        tasks.push_back(returnNumber(number));
        auto task = coro::all(std::move(tasks));
        auto executor = coro::SerialExecutor::create();
        try {
            executor->syncWait(std::move(task));
            FAIL();
        } catch (const TestError& err) {
            EXPECT_EQ(err.id, 8);
        }
        EXPECT_EQ(failCount, 2);
    }
}

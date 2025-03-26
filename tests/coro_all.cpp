#include <coro/coro.hpp>
#include <coro/helpers/all.hpp>
#include <coro/executors/serial_executor.hpp>

#include <gtest/gtest.h>

coro::Task<void> makeVoidTask(int& voidTaskCount) {
    ++voidTaskCount;
    co_return;
};

coro::Task<int> makeIntTask(int x) {
    co_return x;
};

TEST(CoroAll, MultipleIntTasks) {
    std::vector<coro::Task<int>> tasks;
    tasks.push_back(makeIntTask(10));
    tasks.push_back(makeIntTask(20));
    tasks.push_back(makeIntTask(30));

    auto e = coro::SerialExecutor::create();
    auto task = coro::all(std::move(tasks));
    auto results = e->run(task);
    EXPECT_EQ(results[0], 10);
    EXPECT_EQ(results[1], 20);
    EXPECT_EQ(results[2], 30);
}

TEST(CoroAll, MixedTasksAndSyncWait) {
    int voidTaskCount = 0;

    auto executor = coro::SerialExecutor::create();
    auto task = coro::all(makeVoidTask(voidTaskCount), makeIntTask(123), makeVoidTask(voidTaskCount));
    auto results = executor->run(task);

    EXPECT_EQ(voidTaskCount, 2);
    EXPECT_EQ(std::any_cast<int>(results[1]), 123);

    voidTaskCount = 0;
    auto task1 = coro::all(makeVoidTask(voidTaskCount), makeVoidTask(voidTaskCount), makeVoidTask(voidTaskCount));
    executor->run(task1);
    EXPECT_EQ(voidTaskCount, 3);
}

coro::Task<void> executeMixedTasks(std::vector<std::any>& results, int& voidTaskCount) {
    results = co_await coro::all(makeVoidTask(voidTaskCount), makeIntTask(123), makeVoidTask(voidTaskCount));
}

coro::Task<void> executeIntTasks(std::vector<int>& results) {
    std::vector<coro::Task<int>> tasks;
    tasks.push_back(makeIntTask(10));
    tasks.push_back(makeIntTask(20));
    tasks.push_back(makeIntTask(30));

    results = co_await coro::all(std::move(tasks));
}

TEST(CoroAll, NestedCoroAllCalls) {
    std::vector<int> results1;
    std::vector<std::any> results2;
    int voidTaskCount = 0;

    std::vector<coro::Task<void>> tasks;
    tasks.push_back(executeIntTasks(results1));
    tasks.push_back(executeMixedTasks(results2, voidTaskCount));
    tasks.push_back(makeVoidTask(voidTaskCount));

    auto all_tasks = coro::all(std::move(tasks));
    auto executor = coro::SerialExecutor::create();
    executor->run(all_tasks);

    EXPECT_EQ(voidTaskCount, 3);
    EXPECT_EQ(results1[0], 10);
    EXPECT_EQ(results1[1], 20);
    EXPECT_EQ(results1[2], 30);
    EXPECT_EQ(std::any_cast<int>(results2[1]), 123);
}

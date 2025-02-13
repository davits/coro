#include "coro/executor.hpp"
#include "coro/promise.hpp"
#include "coro/task.hpp"
#include "coro/utils.hpp"
#include <coro/coro.hpp>

#include <gtest/gtest.h>

coro::Task<void> makeVoidTask(int& voidTaskCount) {
    ++voidTaskCount;
    co_return;
};

coro::Task<int> makeIntTask(int x) {
    co_return x;
};

template <typename T>
T syncWait(coro::Task<T>&& task) {
    coro::SerialExecutor executor;
    std::optional<T> result;

    auto t = [&result, &task]() -> coro::Task<void> {
        result = co_await std::move(task);
    }();
    executor.run(t);

    return std::move(*result);
}

template <>
inline void syncWait(coro::Task<void>&& task) {
    coro::SerialExecutor executor;
    executor.run(task);
}

TEST(CoroAllTest, MultipleIntTasks) {
    std::vector<coro::Task<int>> tasks;
    tasks.push_back(makeIntTask(10)); 
    tasks.push_back(makeIntTask(20)); 
    tasks.push_back(makeIntTask(30)); 

    coro::SerialExecutor e;
    auto task = coro::all(std::move(tasks));
    auto results = e.run(task);  
    EXPECT_EQ(results[0], 10);
    EXPECT_EQ(results[1], 20);
    EXPECT_EQ(results[2], 30);
}

TEST(CoroAllTest, MixedTasksAndSyncWait) {
    int voidTaskCount = 0;

    auto results = syncWait(coro::all(
        makeVoidTask(voidTaskCount),
        makeIntTask(123),
        makeVoidTask(voidTaskCount)
    ));

    EXPECT_EQ(voidTaskCount, 2);
    EXPECT_EQ(std::any_cast<int>(results[1]), 123);

    voidTaskCount = 0;
    syncWait(coro::all(
        makeVoidTask(voidTaskCount),
        makeVoidTask(voidTaskCount),
        makeVoidTask(voidTaskCount)
    ));
    EXPECT_EQ(voidTaskCount, 3);
}

coro::Task<void> executeMixedTasks(std::vector<std::any>& results, int& voidTaskCount) {
    results = co_await coro::all(
        makeVoidTask(voidTaskCount),
        makeIntTask(123),
        makeVoidTask(voidTaskCount)
    );
}

coro::Task<void> executeIntTasks(std::vector<int>& results) {
    std::vector<coro::Task<int>> tasks;
    tasks.push_back(makeIntTask(10)); 
    tasks.push_back(makeIntTask(20)); 
    tasks.push_back(makeIntTask(30)); 

    results = co_await coro::all(std::move(tasks));
}

TEST(CoroAllTest, NestedCoroAllCalls) {
    std::vector<int> results1;
    std::vector<std::any> results2;
    int voidTaskCount = 0;

    std::vector<coro::Task<void>> tasks;
    tasks.push_back(executeIntTasks(results1)); 
    tasks.push_back(executeMixedTasks(results2, voidTaskCount)); 
    tasks.push_back([&]() -> coro::Task<void> {
        ++voidTaskCount;
        co_return;
    }());

    syncWait(coro::all(std::move(tasks)));

    EXPECT_EQ(voidTaskCount, 3);
    EXPECT_EQ(results1[0], 10);
    EXPECT_EQ(results1[1], 20);  
    EXPECT_EQ(results1[2], 30);
    EXPECT_EQ(std::any_cast<int>(results2[1]), 123);
}

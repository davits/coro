#pragma once

#include "../core/promise.hpp"
#include "../core/task.hpp"
#include "../sync/latch.hpp"

#include <any>

namespace coro {

namespace detail {

template <typename T, typename RType = T>
Task<void> runAndNotify(Task<T> task, Latch& latch, std::vector<RType>& results, int idx) {
    if constexpr (!std::is_same_v<T, void>) {
        results[idx] = co_await std::move(task);
    } else {
        co_await std::move(task);
    }
    latch.count_down();
}

inline Task<void> runAndNotify(Task<void> task, Latch& latch) {
    co_await std::move(task);
    latch.count_down();
}

} // namespace detail

template <typename... Args>
concept AllVoid = (std::same_as<Args, void> && ...);

template <AllVoid... Args>
Task<void> all(Task<Args>... tasks) {
    Latch latch {sizeof...(tasks)};
    auto executor = co_await currentExecutor;

    std::vector<Task<void>> runningTasks;
    runningTasks.reserve(sizeof...(tasks));

    (runningTasks.push_back(detail::runAndNotify(std::move(tasks), latch)), ...);

    for (auto& task : runningTasks) {
        task.scheduleOn(executor);
    }

    co_await latch;
}

template <typename... Args>
Task<std::vector<std::any>> all(Task<Args>... tasks) {
    std::vector<std::any> results(sizeof...(tasks));
    Latch latch {sizeof...(tasks)};
    auto executor = co_await currentExecutor;

    std::vector<Task<void>> runningTasks;
    runningTasks.reserve(sizeof...(tasks));

    size_t idx = 0;
    (runningTasks.push_back(detail::runAndNotify(std::move(tasks), latch, results, idx++)), ...);

    for (auto& task : runningTasks) {
        task.scheduleOn(executor);
    }

    co_await latch;
    co_return std::move(results);
}

template <typename T>
Task<std::vector<T>> all(std::vector<Task<T>> tasks) {
    if (tasks.empty()) {
        co_return {};
    }

    std::vector<T> results(tasks.size());
    Latch latch {static_cast<uint32_t>(tasks.size())};
    auto executor = co_await currentExecutor;

    std::vector<Task<void>> runningTasks;
    runningTasks.reserve(tasks.size());

    size_t idx = 0;
    for (auto& task : tasks) {
        runningTasks.push_back(detail::runAndNotify(std::move(task), latch, results, idx++));
    }

    for (auto& task : runningTasks) {
        task.scheduleOn(executor);
    }

    co_await latch;
    co_return std::move(results);
}

inline Task<void> all(std::vector<Task<void>> tasks) {
    if (tasks.empty()) {
        co_return;
    }

    Latch latch {static_cast<std::ptrdiff_t>(tasks.size())};

    std::vector<Task<void>> helperTasks;
    helperTasks.reserve(tasks.size());

    for (auto& task : tasks) {
        helperTasks.push_back(detail::runAndNotify(std::move(task), latch));
    }
    tasks.clear();

    auto executor = co_await currentExecutor;
    for (auto& task : helperTasks) {
        task.scheduleOn(executor);
    }

    co_await latch;
    helperTasks.clear();
}

} // namespace coro

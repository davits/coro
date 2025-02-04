#pragma once

#include "task.hpp"
#include "detail/latch.hpp"
#include <any>

namespace coro {

template <typename T>
Task<void> runAndNotify(Task<T>&& task, detail::Latch& latch, std::vector<std::any>& results, int idx) {
    T result = co_await task;
    results[idx] = std::move(result);
    latch.count_down();
    co_return;
}

template <typename... Args>
Task<std::vector<std::any>> all(Task<Args>&&... tasks) {
    std::vector<std::any> results {sizeof...(tasks)};
    detail::Latch latch {sizeof...(tasks)};
    auto executor = co_await current_executor;
    size_t idx = 0;
    (executor->schedule(runAndNotify(tasks, latch, results, idx++)), ...);
    co_await latch;
    co_return std::move(results);
}

} // namespace coro

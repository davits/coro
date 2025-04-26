#pragma once

#include "../core/promise.hpp"
#include "../core/task.hpp"
#include "../sync/latch.hpp"

#include <any>
#include <exception>

namespace coro {

namespace detail {

template <typename T, typename RType = T>
Task<void> runAndNotify(Task<T> task, Latch& latch, std::exception_ptr& eptr, std::vector<RType>& results, int idx) {
    try {
        if constexpr (!std::is_same_v<T, void>) {
            results[idx] = co_await std::move(task);
        } else {
            co_await std::move(task);
        }
    } catch (...) {
        if (!eptr) {
            eptr = std::current_exception();
        }
    }
    latch.count_down();
}

inline Task<void> runAndNotify(Task<void> task, Latch& latch, std::exception_ptr& eptr) {
    try {
        co_await std::move(task);
    } catch (...) {
        if (!eptr) {
            eptr = std::current_exception();
        }
    }
    latch.count_down();
}

} // namespace detail

template <typename... Args>
    requires(std::same_as<Args, void> && ...)
Task<void> all(Task<Args>... tasks) {
    Latch latch {sizeof...(tasks)};
    auto executor = co_await currentExecutor;

    std::exception_ptr eptr = nullptr;
    (executor->schedule(detail::runAndNotify(std::move(tasks), latch, eptr)), ...);
    co_await latch;
    if (eptr) {
        std::rethrow_exception(eptr);
    }
}

template <typename T, typename... Args>
    requires(std::same_as<Args, T> && ... && !std::same_as<T, void>)
Task<std::vector<T>> all(Task<T> first, Task<Args>... rest) {
    constexpr size_t count = 1 + sizeof...(rest);
    std::vector<int> results(count);
    Latch latch {count};
    auto executor = co_await currentExecutor;

    std::exception_ptr eptr = nullptr;
    executor->schedule(detail::runAndNotify(std::move(first), latch, eptr, results, 0));
    size_t idx = 1;
    (executor->schedule(detail::runAndNotify(std::move(rest), latch, eptr, results, idx++)), ...);

    co_await latch;
    if (eptr) {
        std::rethrow_exception(eptr);
    }
    co_return std::move(results);
}

template <typename... Args>
Task<std::vector<std::any>> all(Task<Args>... tasks) {
    std::vector<std::any> results(sizeof...(tasks));
    Latch latch {sizeof...(tasks)};
    auto executor = co_await currentExecutor;

    std::exception_ptr eptr = nullptr;
    size_t idx = 0;
    (executor->schedule(detail::runAndNotify(std::move(tasks), latch, eptr, results, idx++)), ...);

    co_await latch;
    if (eptr) {
        std::rethrow_exception(eptr);
    }
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

    std::exception_ptr eptr = nullptr;
    size_t idx = 0;
    for (auto& task : tasks) {
        executor->schedule(detail::runAndNotify(std::move(task), latch, eptr, results, idx++));
    }

    co_await latch;
    if (eptr) {
        std::rethrow_exception(eptr);
    }
    co_return std::move(results);
}

inline Task<void> all(std::vector<Task<void>> tasks) {
    if (tasks.empty()) {
        co_return;
    }
    auto executor = co_await currentExecutor;
    Latch latch {static_cast<std::ptrdiff_t>(tasks.size())};
    std::exception_ptr eptr = nullptr;
    for (auto& task : tasks) {
        executor->schedule(detail::runAndNotify(std::move(task), latch, eptr));
    }
    tasks.clear();

    co_await latch;
    if (eptr) {
        std::rethrow_exception(eptr);
    }
}

} // namespace coro

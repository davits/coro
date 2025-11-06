#pragma once

#include "../core/promise.hpp"
#include "../core/task.hpp"
#include "../sync/latch.hpp"

#include <any>
#include <exception>

namespace coro {

namespace detail {

template <typename R, typename T>
Task<void> runAndNotify(Task<T> task, Latch latch, std::exception_ptr& eptr, R* result) {
    try {
        if constexpr (!std::is_same_v<T, void>) {
            *result = co_await std::move(task);
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

} // namespace detail

template <typename... Args>
    requires(std::same_as<Args, void> && ...)
Task<void> all(Task<Args>... tasks) {
    constexpr size_t count = sizeof...(tasks);
    static_assert(count > 2, "It does not make sense to use coro::all() with <2 arguments...");
    Latch latch {static_cast<std::ptrdiff_t>(count)};
    auto executor = co_await currentExecutor;
    std::exception_ptr eptr = nullptr;

    PromiseBase& promise = co_await currentPromise;

    (executor->next(detail::runAndNotify<void>(std::move(tasks), latch, eptr, nullptr).setContext(promise.context)),
     ...);

    // Reset stop token before awaiting for the latch, so we don't wake up from cancellation here when child tasks are
    // running, this way we will wait while all child tasks cancel and trigger the latch for correct handling.
    promise.context.stopToken.reset();
    co_await latch;
    if (eptr) {
        std::rethrow_exception(eptr);
    }
}

template <typename T, typename... Args>
    requires(std::same_as<Args, T> && ... && !std::same_as<T, void>)
Task<std::vector<T>> all(Task<T> first, Task<Args>... rest) {
    constexpr size_t count = 1 + sizeof...(rest);
    static_assert(count > 2, "It does not make sense to use coro::all() with <2 arguments...");
    std::vector<T> results(count);
    Latch latch {static_cast<std::ptrdiff_t>(count)};
    auto executor = co_await currentExecutor;
    std::exception_ptr eptr = nullptr;

    PromiseBase& promise = co_await currentPromise;

    executor->next(detail::runAndNotify(std::move(first), latch, eptr, &results[0]).setContext(promise.context));
    size_t idx = 1;
    (executor->next(detail::runAndNotify(std::move(rest), latch, eptr, &results[idx++])).setContext(promise.context),
     ...);

    // Reset stop token before awaiting for the latch, so we don't wake up from cancellation here when child tasks are
    // running, this way we will wait while all child tasks cancel and trigger the latch for correct handling.
    promise.context.stopToken.reset();
    co_await latch;
    if (eptr) {
        std::rethrow_exception(eptr);
    }
    co_return std::move(results);
}

template <typename... Args>
Task<std::vector<std::any>> all(Task<Args>... tasks) {
    constexpr size_t count = sizeof...(tasks);
    static_assert(count > 2, "It does not make sense to use coro::all() with <2 arguments...");

    std::vector<std::any> results(count);
    Latch latch {static_cast<std::ptrdiff_t>(count)};
    auto executor = co_await currentExecutor;
    std::exception_ptr eptr = nullptr;

    PromiseBase& promise = co_await currentPromise;
    size_t idx = 0;
    (executor->next(detail::runAndNotify(std::move(tasks), latch, eptr, &results[idx++]).setContext(promise.context)),
     ...);

    // Reset stop token before awaiting for the latch, so we don't wake up from cancellation here when child tasks are
    // running, this way we will wait while all child tasks cancel and trigger the latch for correct handling.
    promise.context.stopToken.reset();
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
    const size_t count = tasks.size();
    std::vector<T> results(count);
    Latch latch {static_cast<std::ptrdiff_t>(count)};
    auto executor = co_await currentExecutor;
    std::exception_ptr eptr = nullptr;

    PromiseBase& promise = co_await currentPromise;
    for (size_t i = 0; i < tasks.size(); ++i) {
        executor->next(detail::runAndNotify(std::move(tasks[i]), latch, eptr, &results[i]).setContext(promise.context));
    }

    // Reset stop token before awaiting for the latch, so we don't wake up from cancellation here when child tasks are
    // running, this way we will wait while all child tasks cancel and trigger the latch for correct handling.
    promise.context.stopToken.reset();
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

    PromiseBase& promise = co_await currentPromise;
    for (auto& task : tasks) {
        executor->next(detail::runAndNotify<void>(std::move(task), latch, eptr, nullptr).setContext(promise.context));
    }

    // Reset stop token before awaiting for the latch, so we don't wake up from cancellation here when child tasks are
    // running, this way we will wait while all child tasks cancel and trigger the latch for correct handling.
    promise.context.stopToken.reset();
    co_await latch;
    if (eptr) {
        std::rethrow_exception(eptr);
    }
}

} // namespace coro

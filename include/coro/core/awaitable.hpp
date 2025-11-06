#pragma once

#include "handle.hpp"
#include "../detail/utils.hpp"

#include <utility>
#include <coroutine>

namespace coro {

template <typename R>
struct ReadyAwaitable {
    R result;

    ReadyAwaitable(const R& r)
        : result(r) {}

    ReadyAwaitable(R&& r)
        : result(std::move(r)) {}

    bool await_ready() noexcept {
        return true;
    }
    void await_suspend(std::coroutine_handle<>) noexcept {}

    R await_resume() {
        if constexpr (std::is_move_constructible_v<R>) {
            return std::move(result);
        } else {
            return result;
        }
    }
};

template <typename R>
struct ReadyAwaitable<R&> {
    R& result;

    ReadyAwaitable(R& r)
        : result(r) {}

    bool await_ready() noexcept {
        return true;
    }

    void await_suspend(std::coroutine_handle<>) noexcept {}

    R& await_resume() {
        return result;
    }
};

template <>
struct ReadyAwaitable<void> {
    ReadyAwaitable() {}

    bool await_ready() noexcept {
        return true;
    }
    void await_suspend(std::coroutine_handle<>) noexcept {}

    void await_resume() noexcept {}
};

template <typename Task>
struct Awaitable {
    using Return = Task::Type;

    Awaitable(Task&& task)
        : _task(std::move(task)) {}

    Awaitable(Awaitable&&) = default;

    Awaitable& operator=(Awaitable&&) = default;

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> awaiter) noexcept {
        CoroHandle awaitingHandle = CoroHandle::fromTypedHandle(awaiter);
        auto& awaitingPromise = awaiter.promise();
        auto& taskPromise = _task.promise();
        if (taskPromise.executor == nullptr) {
            // if task is not scheduled on any executor,
            // copy awaiter task context and schedule on the same executor
            taskPromise.executor = awaitingPromise.executor;
            taskPromise.inheritContext(awaitingPromise);
            // schedule new task via next() to ensure that the call hierarchy has precedence.
            // This way scheduled tasks will work one by one rather then all at once with intermingled execution order.
            taskPromise.executor->next(_task.handle());
        } else if (taskPromise.executor != awaitingPromise.executor) {
            // mark awaiter as waiting for external execution
            awaitingPromise.executor->external(awaitingHandle);
        }
        taskPromise.set_continuation(std::move(awaitingHandle));
    }

    Return await_resume() {
        // eagerly destroy completed task at the end of the scope
        detail::AtExit exit {[this]() noexcept { _task.reset(); }};

        // throw if stop was requested
        auto& taskPromise = _task.promise();
        taskPromise.continuation.throwIfStopped();

        if constexpr (std::is_move_constructible_v<Return>) {
            return std::move(_task.promise()).value();
        } else {
            return _task.promise().value();
        }
    }

private:
    Task _task;
};

} // namespace coro

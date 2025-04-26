#pragma once

#include "handle.hpp"
#include "../detail/utils.hpp"

#include <utility>
#include <coroutine>

namespace coro {

template <typename R>
struct ReadyAwaitable {
    R result;

    ReadyAwaitable(R r)
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

template <typename Task>
struct Awaitable {
    using Return = Task::Type;

    Awaitable(Task&& task)
        : _task(std::move(task)) {}

    Awaitable(Awaitable&&) = default;

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> awaiter) noexcept {
        CoroHandle awaitingHandle = CoroHandle::fromTypedHandle(awaiter);
        auto& awaitingPromise = awaiter.promise();
        auto& taskPromise = _task.promise();
        if (taskPromise.context.executor == nullptr) {
            // if task is not scheduled on any executor,
            // copy awaiter task context and schedule on the same executor
            taskPromise.context = awaitingPromise.context;
            awaitingPromise.context.executor->schedule(_task.handle());
        } else if (taskPromise.context.executor != awaitingPromise.context.executor) {
            // mark awaiter as waiting for external execution
            awaitingPromise.context.executor->external(awaitingHandle);
        }
        taskPromise.set_continuation(std::move(awaitingHandle));
    }

    Return await_resume() {
        // eagerly destroy completed task at the end of the scope
        detail::AtExit exit {[this]() noexcept { _task.reset(); }};
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

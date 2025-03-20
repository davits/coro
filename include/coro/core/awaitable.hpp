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
        return _task.ready();
    }

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> continuation) noexcept {
        auto& continuationPromise = continuation.promise();
        _task.promise().continuation = CoroHandle::fromTypedHandle(continuation);
        _task.promise().context = continuationPromise.context;
        _task.schedule();
    }

    Return await_resume() {
        // eagerly destroy completed task at the end of the scope
        detail::AtExit exit {[this]() noexcept { _task.destroy(); }};
        if constexpr (std::is_move_constructible_v<Return>) {
            return std::move(_task).value();
        } else {
            return _task.value();
        }
    }

private:
    Task _task;
};

} // namespace coro

#pragma once

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
    R await_resume() noexcept(noexcept(R(std::declval<R&&>()))) {
        return std::move(result);
    }
};

// Light-weight non owning handle
template <typename Task>
struct Awaitable {
    using Return = Task::Type;

    Awaitable(Task&& task)
        : _task(std::move(task)) {}

    Awaitable(Awaitable&&) = default;
    Task _task;

    bool await_ready() const noexcept {
        return false;
    }

    template <typename ContextPromise>
    void await_suspend(ContextPromise) {}

    Return await_resume() {
        return std::move(_task.promise()).value();
    }
};

} // namespace coro

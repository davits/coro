#pragma once

#include "handle.hpp"
#include "stop.hpp"
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

    constexpr bool await_ready() noexcept {
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

    constexpr bool await_ready() noexcept {
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

    constexpr bool await_ready() noexcept {
        return true;
    }
    void await_suspend(std::coroutine_handle<>) noexcept {}

    void await_resume() noexcept {}
};

template <typename Task>
struct Awaitable {
    using Return = Task::Type;

    Awaitable(Task&& task, StopToken stopToken)
        : _task(std::move(task))
        , _stopToken(std::move(stopToken)) {}

    Awaitable(Awaitable&&) = default;

    Awaitable& operator=(Awaitable&&) = default;

    bool await_ready() noexcept {
        return _task.ready();
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> awaiter) noexcept {
        CoroHandle awaitingHandle = CoroHandle::fromTypedHandle(awaiter);
        auto& taskPromise = _task.promise();
        return taskPromise.add_awaiter(_task._handle, std::move(awaitingHandle));
    }

    Return await_resume() {
        // eagerly destroy completed task at the end of the scope
        detail::AtExit exit {[this]() noexcept { _task.reset(); }};
        // throw if stop was requested
        _stopToken.throwIfStopped();

        if constexpr (!std::is_copy_constructible_v<Return>) {
            return std::move(_task.promise()).value();
        } else {
            return _task.promise().value();
        }
    }

private:
    Task _task;
    StopToken _stopToken;
};

} // namespace coro

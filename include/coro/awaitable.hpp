#pragma once

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
        if constexpr (std::move_constructible<R>) {
            return std::move(result);
        } else {
            return result;
        }
    }
};

template <typename F>
class AtExit {
public:
    AtExit(F f)
        : _callback(std::move(f)) {}

    ~AtExit() {
        _callback();
    }

private:
    F _callback;
};

template <typename Task>
struct Awaitable {
    using Return = Task::Type;

    Awaitable(Task&& task)
        : _task(std::move(task)) {}

    Awaitable(Awaitable&&) = default;
    Task _task;

    bool await_ready() const noexcept {
        return _task.ready();
    }

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> continuation) noexcept {
        auto& promise = continuation.promise();
        _task.promise().continuation = continuation;
        _task.scheduleOn(promise.executor);
    }

    Return await_resume() {
        AtExit exit {[this]() { _task.destroy(); }};
        if constexpr (std::move_constructible<Return>) {
            return std::move(_task).value();
        } else {
            return _task.value();
        }
    }
};

} // namespace coro

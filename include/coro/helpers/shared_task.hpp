#pragma once

#include "../core/executor.hpp"
#include "../core/promise.hpp"
#include "../core/task.hpp"
#include "../core/traits.hpp"

#include "../detail/containers.hpp"

#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <variant>

namespace coro {

template <typename R>
class SharedTask;

namespace detail {

template <typename R>
class SharedTaskAwaitable;

template <typename R>
class SharedTaskState : public std::enable_shared_from_this<SharedTaskState<R>> {
public:
    using Ref = std::shared_ptr<SharedTaskState>;
    using Stored = std::conditional_t<std::is_void_v<R>, std::monostate, R>;

    SharedTaskState(Task<R>&& task)
        : _task(std::move(task)) {}

    /// Starts the wrapped task on the given executor, unless it was started already.
    void start(const Executor::Ref& executor) {
        Task<R> task;
        {
            std::scoped_lock lock {_mutex};
            if (_started) {
                return;
            }
            _started = true;
            task = std::move(_task);
        }
        // Preserve the context set on the wrapped task by the user: the driver runs with an empty
        // context and would otherwise wipe it on co_await.
        task.handle().promise().enableContextInheritance(false);
        executor->next(run(std::move(task), this->shared_from_this()));
    }

    bool finished() const {
        std::scoped_lock lock {_mutex};
        return _finished;
    }

    bool queue(SharedTaskAwaitable<R>* awaitable) {
        std::scoped_lock lock {_mutex};
        if (_finished) {
            return false;
        }
        _awaiters.pushBack(awaitable);
        return true;
    }

    void remove(const SharedTaskAwaitable<R>* awaitable) {
        std::scoped_lock lock {_mutex};
        _awaiters.erase(awaitable);
    }

    /// Only valid after finished(): rethrows if the wrapped task failed,
    /// otherwise returns const reference to the produced value.
    decltype(auto) value() const {
        if (_exception) {
            std::rethrow_exception(_exception);
        }
        if constexpr (!std::is_void_v<R>) {
            return static_cast<const R&>(*_value);
        }
    }

private:
    static Task<void> run(Task<R> task, Ref state) {
        try {
            if constexpr (std::is_void_v<R>) {
                co_await std::move(task);
                state->finish(std::monostate {}, nullptr);
            } else {
                state->finish(co_await std::move(task), nullptr);
            }
        } catch (...) {
            state->finish(std::nullopt, std::current_exception());
        }
    }

    void finish(std::optional<Stored>&& value, std::exception_ptr exception) {
        std::scoped_lock lock {_mutex};
        _value = std::move(value);
        _exception = exception;
        _finished = true;
        while (true) {
            auto next = _awaiters.popFront().value_or(nullptr);
            if (!next) break;
            next->taskFinished();
        }
    }

private:
    Task<R> _task;
    bool _started = false;
    bool _finished = false;
    std::optional<Stored> _value;
    std::exception_ptr _exception;
    detail::Deque<SharedTaskAwaitable<R>*> _awaiters;
    mutable std::mutex _mutex;
};

template <typename R>
class SharedTaskAwaitable {
private:
    friend await_ready_trait<SharedTask<R>>;
    SharedTaskAwaitable(const typename SharedTaskState<R>::Ref& state, const PromiseBase& promise)
        : _state(state)
        , _executor(promise.executor)
        , _stopToken(promise.context.stopToken) {}

public:
    bool await_ready() noexcept {
        return _state->finished();
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> continuation) noexcept {
        _continuation = CoroHandle::fromTypedHandle(continuation);
        _state->start(_executor);
        const bool queued = _state->queue(this);
        if (queued) {
            _executor->external(_continuation);
        }
        return queued;
    }

    decltype(auto) await_resume() {
        if (_stopToken.stopRequested()) {
            _state->remove(this);
            _stopToken.throwException();
        }
        return _state->value();
    }

private:
    friend class SharedTaskState<R>;
    void taskFinished() {
        _executor->schedule(_continuation);
    }

private:
    typename SharedTaskState<R>::Ref _state;
    Executor::Ref _executor;
    CoroHandle _continuation;
    StopToken _stopToken;
};

} // namespace detail

/**
 * Shareable variant of Task, which can be copied and co_await(ed) multiple times from multiple
 * coroutines, potentially running on different executors.
 * The wrapped task is started lazily when the SharedTask is first co_await(ed), on the executor of the
 * first awaiter (unless the wrapped task was scheduled on an executor beforehand). It runs exactly once;
 * every awaiter — including those arriving after completion — receives a const reference to the same
 * result value, or a rethrow of the same exception if the task failed.
 * The result is kept alive as long as at least one SharedTask copy references it.
 * Cancellation via a stop token affects only the awaiting coroutine, not the shared computation itself.
 * To cancel the computation set a stop token on the wrapped Task before constructing the SharedTask.
 *
 * @code
 * SharedTask<int> shared {work()};
 * // coroutine 1 and 2, in any order
 * const int& result = co_await shared;
 * @endcode
 */
template <typename R>
class SharedTask {
public:
    using Type = R;

public:
    SharedTask() = default;

    explicit SharedTask(Task<R>&& task)
        : _state(task ? std::make_shared<detail::SharedTaskState<R>>(std::move(task)) : nullptr) {}

    SharedTask(const SharedTask&) = default;
    SharedTask& operator=(const SharedTask&) = default;
    SharedTask(SharedTask&&) = default;
    SharedTask& operator=(SharedTask&&) = default;

public:
    void reset() {
        _state.reset();
    }

    bool ready() const {
        return _state && _state->finished();
    }

    explicit operator bool() const {
        return static_cast<bool>(_state);
    }

private:
    friend struct await_ready_trait<SharedTask>;
    typename detail::SharedTaskState<R>::Ref _state;
};

/// Helper to wrap a Task into a SharedTask.
/// auto shared = coro::share(work());
template <typename R>
SharedTask<R> share(Task<R>&& task) {
    return SharedTask<R> {std::move(task)};
}

template <typename R>
struct await_ready_trait<SharedTask<R>> {
    static detail::SharedTaskAwaitable<R> await_transform(const PromiseBase& promise, const SharedTask<R>& task) {
        return detail::SharedTaskAwaitable<R> {task._state, promise};
    }
};

} // namespace coro

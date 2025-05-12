#pragma once

#include "awaitable.hpp"
#include "executor.hpp"
#include "handle.hpp"
#include "stop.hpp"
#include "task_context.hpp"
#include "task.fwd.hpp"
#include "traits.hpp"

#include <mutex>

namespace coro {

class PromiseBase {
public:
    enum class ExecutionState {
        Normal,
        Cancelling,
        Finished,
    };

    enum class ValueState {
        Uninitialized,
        Value,
        Exception,
    };

private:
    friend class CoroHandle;
    std::atomic<size_t> _useCount = 0;

public:
    Executor::Ref executor;
    TaskContext context;
    CoroHandle continuation = nullptr;

protected:
    std::exception_ptr _exception;
    ValueState _valueState;

private:
    mutable std::mutex _mutex;
    ExecutionState _executionState = ExecutionState::Normal;
    bool _inheritContext = true;

public:
    void emplace_exception(std::exception_ptr ptr) {
        _exception = ptr;
        _valueState = ValueState::Exception;
    }

public:
    PromiseBase() = default;

    std::suspend_always initial_suspend() {
        return {};
    }

    class FinalAwaiter {
    public:
        bool await_ready() noexcept {
            return false;
        }

        template <typename Promise>
        void await_suspend(std::coroutine_handle<Promise> handle) noexcept {
            auto& promise = handle.promise();
            promise.on_finished();
        }

        [[noreturn]] void await_resume() noexcept {
            // should not reach here
            std::abort();
        }
    };

    FinalAwaiter final_suspend() noexcept {
        return {};
    }

    void unhandled_exception() {
        emplace_exception(std::current_exception());
    }

    template <typename U>
    Awaitable<Task<U>> await_transform(Task<U>&& task);

    template <typename T>
    decltype(auto) await_transform(T&& obj) {
        using RawT = std::remove_cvref_t<T>;
        return await_ready_trait<RawT>::await_transform(executor, context, std::forward<T>(obj));
    }

public:
    void set_continuation(CoroHandle&& cont) {
        std::scoped_lock lock {_mutex};
        continuation = std::move(cont);
        if (_executionState == ExecutionState::Finished) {
            schedule_continuation();
        }
    }

    bool finished() const {
        std::scoped_lock lock {_mutex};
        return _executionState == ExecutionState::Finished;
    }

    bool stop_if_necessary() {
        std::scoped_lock lock {_mutex};
        if (context.stopToken.stopRequested() && _executionState != ExecutionState::Cancelling) [[unlikely]] {
            emplace_exception(context.stopToken.exception());
            _executionState = ExecutionState::Cancelling;
            schedule_continuation();
            _executionState = ExecutionState::Finished;
            return true;
        }
        return false;
    }

    void enableContextInheritance(bool inherit) {
        _inheritContext = inherit;
    }

    void inheritContext(const PromiseBase& from) {
        if (_inheritContext) [[likely]] {
            context = from.context;
        }
    }

private:
    void on_finished() {
        std::scoped_lock lock {_mutex};
        schedule_continuation();
        _executionState = ExecutionState::Finished;
    }

    void schedule_continuation() {
        if (continuation) {
            if (_executionState == ExecutionState::Cancelling) {
                continuation.promise()._executionState = ExecutionState::Cancelling;
            }
            auto continuationExecutor = continuation.promise().executor;
            if (continuationExecutor == executor) {
                continuationExecutor->next(std::move(continuation));
            } else {
                continuationExecutor->schedule(std::move(continuation));
            }
        }
    }
};

} // namespace coro

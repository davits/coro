#pragma once

#include "awaitable.hpp"
#include "executor.hpp"
#include "handle.hpp"
#include "stop_token.hpp"
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

public:
    TaskContext context;
    CoroHandle continuation = nullptr;
    ExecutionState executionState = ExecutionState::Normal;

private:
    friend class CoroHandle;
    std::atomic<size_t> _useCount = 0;
    mutable std::mutex _mutex;

protected:
    std::exception_ptr _exception;
    ValueState _valueState;

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

    template <typename U>
    Awaitable<Task<U>> await_transform(Task<U>&& task);

    template <typename T>
    decltype(auto) await_transform(T&& obj) {
        using RawT = std::remove_cvref_t<T>;
        return await_ready_trait<RawT>::await_transform(context, std::forward<T>(obj));
    }

    void set_continuation(CoroHandle&& cont) {
        std::scoped_lock lock {_mutex};
        continuation = std::move(cont);
        if (executionState == ExecutionState::Finished) {
            schedule_continuation();
        }
    }

    bool finished() const {
        std::scoped_lock lock {_mutex};
        return executionState == ExecutionState::Finished;
    }

private:
    void on_finished() {
        std::scoped_lock lock {_mutex};
        executionState = ExecutionState::Finished;
        schedule_continuation();
    }

    void schedule_continuation() {
        if (continuation) {
            auto contExecutor = continuation.promise().context.executor;
            if (contExecutor == context.executor) {
                contExecutor->next(std::move(continuation));
            } else {
                contExecutor->schedule(std::move(continuation));
            }
        }
    }
};

} // namespace coro

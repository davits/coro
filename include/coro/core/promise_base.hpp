#pragma once

#include "awaitable.hpp"
#include "executor.hpp"
#include "handle.hpp"
#include "stop.hpp"
#include "task.fwd.hpp"
#include "traits.hpp"

#include <atomic>
#include <mutex>
#include <vector>

namespace coro {

struct UserData {
    using Ref = std::shared_ptr<UserData>;

    virtual ~UserData() = 0;
};

inline UserData::~UserData() {}

struct TaskContext {
    StopToken stopToken = nullptr;
    UserData::Ref userData;
};

class PromiseBase {
public:
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

private:
    // A single awaiter is by far the most common case, so it is stored inline and the overflow storage
    // allocates only for a task which is awaited more than once. std::vector is used because its default
    // constructor is noexcept and hence can not allocate, unlike the deque behind std::queue.
    // Both are valid only while the mutex is held.
    CoroHandle _masterAwaiter;
    std::vector<CoroHandle> _overflowAwaiters;

protected:
    std::exception_ptr _exception;
    ValueState _valueState = ValueState::Uninitialized;

private:
    mutable std::mutex _mutex;
    // Read without the mutex, so that executors can query it from within their scheduling functions,
    // which can be called while the mutex is held. Only ever goes from false to true.
    std::atomic<bool> _finished = false;
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
        constexpr bool await_ready() noexcept {
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

    template <typename T>
    decltype(auto) await_transform(T&& obj) {
        using RawT = std::remove_cvref_t<T>;
        return await_ready_trait<RawT>::await_transform(*this, std::forward<T>(obj));
    }

public:
    bool add_awaiter(CoroHandle taskHandle, CoroHandle awaitingHandle) {
        auto& awaitingPromise = awaitingHandle.promise();
        std::scoped_lock lock {_mutex};
        if (_finished) {
            return false;
        }
        if (!_masterAwaiter) {
            _masterAwaiter = awaitingHandle;
        } else {
            _overflowAwaiters.push_back(awaitingHandle);
        }
        if (executor == nullptr) {
            // if task is not scheduled on any executor,
            // copy awaitingHandle task context and schedule on the same executor
            executor = awaitingPromise.executor;
            inheritContext(awaitingPromise);
            // schedule new task via next() to ensure that the call hierarchy has precedence.
            // This way scheduled tasks will work one by one rather then all at once with intermingled execution order.
            executor->next(taskHandle);
        } else if (executor != awaitingPromise.executor) {
            // mark awaitingHandle as waiting for external execution
            awaitingPromise.executor->external(awaitingHandle);
        }
        return true;
    }

    bool finished() const {
        return _finished;
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
        _finished = true;
        if (!_masterAwaiter) {
            return;
        }
        schedule_awaiter(std::move(_masterAwaiter));
        for (auto& awaiter : _overflowAwaiters) {
            schedule_awaiter(std::move(awaiter));
        }
        _overflowAwaiters.clear();
    }

    void schedule_awaiter(CoroHandle awaiter) {
        auto awaiterExecutor = awaiter.promise().executor;
        if (awaiterExecutor == executor) {
            awaiterExecutor->next(std::move(awaiter));
        } else {
            awaiterExecutor->schedule(std::move(awaiter));
        }
    }
};

template <typename T>
struct await_ready_trait<Task<T>> {
    static decltype(auto) await_transform(const PromiseBase& promise, Task<T> task) {
        return Awaitable<Task<T>> {std::move(task), promise.context.stopToken};
    }
};

/// Helper to easily access to the current executor within the coroutine.
/// const Executor::Ref& executor = co_await coro::currentExecutor;
struct ExecutorAwaitable {};
inline ExecutorAwaitable currentExecutor;

template <>
struct await_ready_trait<ExecutorAwaitable> {
    static decltype(auto) await_transform(const PromiseBase& promise, ExecutorAwaitable) {
        return ReadyAwaitable<const Executor::Ref&> {promise.executor};
    }
};

/// Helper to easily access to the stop token of the current task
/// const StopToken& token = co_await coro::currentStopToken;
struct StopTokenAwaitable {};
inline StopTokenAwaitable currentStopToken;

template <>
struct await_ready_trait<StopTokenAwaitable> {
    static decltype(auto) await_transform(const PromiseBase& promise, StopTokenAwaitable) {
        return ReadyAwaitable<const StopToken&> {promise.context.stopToken};
    }
};

/// Helper to easily access to the user data of the current task
/// const UserData::Ref& token = co_await coro::currentUserData;
struct UserDataAwaitable {};
inline UserDataAwaitable currentUserData;

template <>
struct await_ready_trait<UserDataAwaitable> {
    static decltype(auto) await_transform(const PromiseBase& promise, UserDataAwaitable) {
        return ReadyAwaitable<const UserData::Ref&> {promise.context.userData};
    }
};

/// Helper to easily access to the context of the current task
/// const TaskContext& context = co_await coro::currentContext;
struct TaskContextAwaitable {};
inline TaskContextAwaitable currentContext;

template <>
struct await_ready_trait<TaskContextAwaitable> {
    static decltype(auto) await_transform(PromiseBase& promise, TaskContextAwaitable) {
        return ReadyAwaitable<const TaskContext&> {promise.context};
    }
};

/// Helper to easily access to the underlying promise of the coroutine.
/// PromiseBase& promise = co_await coro::currentPromise;
struct PromiseAwaitable {};
inline PromiseAwaitable currentPromise;

template <>
struct await_ready_trait<PromiseAwaitable> {
    static decltype(auto) await_transform(PromiseBase& promise, PromiseAwaitable) {
        return ReadyAwaitable<PromiseBase&> {promise};
    }
};

} // namespace coro

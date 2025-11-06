#pragma once

#include "awaitable.hpp"
#include "executor.hpp"
#include "handle.hpp"
#include "stop.hpp"
#include "task.fwd.hpp"
#include "traits.hpp"

#include <mutex>

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
    CoroHandle continuation = nullptr;

protected:
    std::exception_ptr _exception;
    ValueState _valueState;

private:
    mutable std::mutex _mutex;
    bool _finished = false;
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
        return await_ready_trait<RawT>::await_transform(*this, std::forward<T>(obj));
    }

public:
    void set_continuation(CoroHandle&& cont) {
        std::scoped_lock lock {_mutex};
        continuation = std::move(cont);
        if (_finished) {
            schedule_continuation();
        }
    }

    bool finished() const {
        std::scoped_lock lock {_mutex};
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
        schedule_continuation();
        _finished = true;
    }

    void schedule_continuation() {
        if (continuation) {
            auto continuationExecutor = continuation.promise().executor;
            if (continuationExecutor == executor) {
                continuationExecutor->next(continuation);
            } else {
                continuationExecutor->schedule(continuation);
            }
        }
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

#pragma once

#include "task.fwd.hpp"
#include "awaitable.hpp"
#include "expected.hpp"
#include "executor.hpp"

#include <coroutine>
#include <exception>

namespace coro {

template <typename T>
struct await_ready_trait;

class PromiseBase {
public:
    Executor* executor = nullptr;

    std::coroutine_handle<> continuation;

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
            if (promise.continuation) {
                promise.executor->schedule(promise.continuation);
            }
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
    auto await_transform(T&& obj) {
        using RawT = std::remove_cvref_t<T>;
        return await_ready_trait<RawT>::await_transform(executor, std::forward<T>(obj));
    }
};

template <typename R>
class Promise : public PromiseBase {
public:
    using handle_t = std::coroutine_handle<Promise>;
    using return_t = R;

public:
    Task<R> get_return_object();

    void unhandled_exception() {
        _storage.emplace_error(std::current_exception());
    }

    void return_value(R r) {
        _storage.emplace_value(std::move(r));
    }

    expected<R>& storage() {
        return _storage;
    }

private:
    expected<R> _storage;
};

template <>
class Promise<void> : public PromiseBase {
public:
    using handle_t = std::coroutine_handle<Promise>;
    using return_t = void;

public:
    inline Task<void> get_return_object();

    void unhandled_exception() {
        _storage.emplace_error(std::current_exception());
    }

    void return_void() {
        _storage.emplace_value();
    }

    expected<void>& storage() {
        return _storage;
    }

private:
    expected<void> _storage;
};

} // namespace coro

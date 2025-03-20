#pragma once

#include "awaitable.hpp"
#include "executor.hpp"
#include "handle.hpp"
#include "stop_token.hpp"
#include "task_context.hpp"
#include "task.fwd.hpp"
#include "traits.hpp"

namespace coro {

class PromiseBase {
public:
    TaskContext context;
    CoroHandle continuation = nullptr;

private:
    friend class CoroHandle;
    std::atomic<size_t> _useCount = 0;

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
            promise.schedule_continuation();
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

private:
    void schedule_continuation() {
        if (continuation) {
            context.executor->schedule(continuation);
        }
    }
};

} // namespace coro

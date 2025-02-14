#pragma once

#include "promise.hpp"

#include <coroutine>

namespace coro {

class TimeoutAwaitable {
public:
    TimeoutAwaitable(uint32_t timeout)
        : _timeout(timeout) {}

    bool await_ready() noexcept {
        return false;
    }

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> continuation) noexcept {
        auto& promise = continuation.promise();
        promise.executor->timeout(_timeout, continuation);
    }

    void await_resume() noexcept {}

private:
    uint32_t _timeout;
};

inline TimeoutAwaitable timeout(uint32_t timeout) {
    return TimeoutAwaitable {timeout};
}

template <>
struct await_ready_trait<TimeoutAwaitable> {
    static TimeoutAwaitable&& await_transform(const Executor::Ref&, TimeoutAwaitable&& awaitable) {
        return std::move(awaitable);
    }
};

} // namespace coro

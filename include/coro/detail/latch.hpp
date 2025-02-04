#pragma once

#include "../promise.hpp"
#include "stack.hpp"

namespace coro::detail {

class Latch;

class LatchAwaitable {
public:
    LatchAwaitable(Latch* latch)
        : _latch(latch) {}

    bool await_ready() noexcept;

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> continuation) noexcept;

    void await_resume() noexcept {}

    void schedule() {
        _executor->schedule(_continuation);
    }

private:
    Latch* _latch;
    Executor* _executor;
    std::coroutine_handle<> _continuation;
};

/**
 * Latch latch{3};
 * co_await latch;
 */
class Latch {
public:
    Latch(uint32_t count)
        : _count(count) {}

    void count_down() {
        uint32_t count = _count.fetch_sub(1);
        if (count == 1) {
            while (true) {
                auto top = _awaiters.pop();
                if (!top) break;
                top->data->schedule();
            }
        } else if (count == 0) {
            throw 0;
        }
    }

private:
    friend class LatchAwaitable;
    bool signaled() const {
        return _count.load(std::memory_order_relaxed) == 0;
    }

    void queue(LatchAwaitable* awaitable) {
        _awaiters.push(awaitable);
    }

private:
    std::atomic<uint32_t> _count;
    detail::Stack<LatchAwaitable*> _awaiters;
};

inline bool LatchAwaitable::await_ready() noexcept {
    return _latch->signaled();
}

template <typename Promise>
void LatchAwaitable::await_suspend(std::coroutine_handle<Promise> continuation) noexcept {
    auto& promise = continuation.promise();
    _executor = promise.executor;
    _continuation = continuation;
    _latch->queue(this);
}

} // namespace coro::detail

namespace coro {

template <>
struct await_ready_trait<detail::Latch> {
    static detail::LatchAwaitable await_transform(Executor*, detail::Latch& awaitable) {
        return detail::LatchAwaitable {&awaitable};
    }
};

} // namespace coro

#pragma once

#include "../core/executor.hpp"
#include "../core/promise_base.hpp"
#include "../core/traits.hpp"

#include "../detail/containers.hpp"

#include <memory>

namespace coro {

namespace detail {
class LatchAwaitable;
class LatchState;
using LatchStateRef = std::shared_ptr<LatchState>;
} // namespace detail

/**
 * @brief A synchronization primitive that allows coroutines to wait until a specified count of events
 * has occurred. Once the count reaches zero, all coroutines waiting on the latch are resumed.
 *
 * @code
 * // coroutine 0
 * Latch latch{3};
 * co_await latch;
 * // coroutine 1
 * latch.count_down();
 * @endcode
 */
class Latch {
public:
    Latch(std::ptrdiff_t count)
        : _state(std::make_shared<detail::LatchState>(count)) {}

    /**
     * @brief Decrements the latch counter and resumes awaiting coroutines
     *        when/if the count reaches zero or negative.
     */
    void count_down(std::ptrdiff_t n = 1);

private:
    friend class detail::LatchAwaitable;
    const detail::LatchStateRef& state() const {
        return _state;
    }

private:
    detail::LatchStateRef _state;
};

namespace detail {

class LatchState {
public:
    using Ref = std::shared_ptr<LatchState>;

    LatchState(std::ptrdiff_t count)
        : _count(count) {}

    void count_down(std::ptrdiff_t n = 1);

    bool signaled() const {
        std::scoped_lock lock {_mutex};
        return _count <= 0;
    }

    bool queue(detail::LatchAwaitable* awaitable) {
        std::scoped_lock lock {_mutex};
        if (_count <= 0) {
            return false;
        }
        _awaiters.pushBack(awaitable);
        return true;
    }

    void remove(const detail::LatchAwaitable* awaitable) {
        std::scoped_lock lock {_mutex};
        _awaiters.erase(awaitable);
    }

private:
    std::ptrdiff_t _count;
    detail::Deque<detail::LatchAwaitable*> _awaiters;
    mutable std::mutex _mutex;
};

class LatchAwaitable {
private:
    friend await_ready_trait<Latch>;
    LatchAwaitable(const Latch& latch, const PromiseBase& promise)
        : _state(latch._state)
        , _executor(promise.executor)
        , _stopToken(promise.context.stopToken) {}

public:
    bool await_ready() noexcept {
        return _state->signaled();
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> continuation) noexcept {
        _continuation = CoroHandle::fromTypedHandle(continuation);
        const bool queued = _state->queue(this);
        if (queued) {
            _executor->external(_continuation);
        }
        return queued;
    }

    void await_resume() {
        if (_stopToken.stopRequested()) {
            _state->remove(this);
            _stopToken.throwException();
        }
    }

private:
    friend class LatchState;
    void latchSignaled() {
        _executor->schedule(_continuation);
    }

private:
    LatchState::Ref _state;
    Executor::Ref _executor;
    CoroHandle _continuation;
    StopToken _stopToken;
};

inline void LatchState::count_down(std::ptrdiff_t n) {
    std::scoped_lock lock {_mutex};
    _count -= n;
    if (_count <= 0) {
        while (true) {
            auto next = _awaiters.popFront().value_or(nullptr);
            if (!next) break;
            next->latchSignaled();
        }
    }
}

} // namespace detail

inline void Latch::count_down(std::ptrdiff_t n) {
    _state->count_down(n);
}

template <>
struct await_ready_trait<Latch> {
    static detail::LatchAwaitable await_transform(const PromiseBase& promise, const Latch& latch) {
        return detail::LatchAwaitable {latch, promise};
    }
};

} // namespace coro

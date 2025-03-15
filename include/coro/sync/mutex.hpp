#pragma once

#include "../detail/utils.hpp"
#include "../detail/containers.hpp"
#include "../core/traits.hpp"
#include "../core/executor.hpp"

#include <coroutine>

namespace coro {

class ScopedLock;
namespace detail {
class MutexAwaitable;
}

/**
 * Asynchronous mutex working on the first come first served basis.
 * Example usage: `auto lock = co_await mutex;`
 * co_await-ing to the mutex will return ScopedLock, which will unlock
 * the mutex when goes out of the scope.
 * When one coroutine has the lock all subsequent coroutines requesting the lock
 * will be suspended and queued. When the lock is released the next queued
 * coroutine will be given then lock and resumed on its executor.
 */
class Mutex {
public:
    Mutex() = default;

    ~Mutex() {
        if (!_awaiters.empty()) {
            std::abort();
        }
    }

private:
    friend detail::MutexAwaitable;
    friend ScopedLock;

    /// Try to lock the mutex and return whether the attempt was sucessfull
    bool try_lock() {
        std::scoped_lock lock {_mutex};
        if (_locked) return false;
        _locked = true;
        return true;
    }

    /// Unlock mutex allowing next queued or incoming awaiter to lock it.
    void unlock();

    /// Lock mutex if it is not currenlty locked, queue awaiter otherwise
    /// returns true if lock didn't succeed and awaiter was queued, false otherwise.
    bool lock_or_queue(detail::MutexAwaitable* awaiter) {
        std::scoped_lock lock {_mutex};
        if (!_locked) {
            _locked = true;
            return false;
        }
        _awaiters.push(awaiter);
        return true;
    }

private:
    detail::Queue<detail::MutexAwaitable*> _awaiters;
    mutable std::mutex _mutex;
    bool _locked = false;
};

class ScopedLock : public detail::OnlyMovable {
private:
    friend detail::MutexAwaitable;
    ScopedLock(Mutex* mutex)
        : _mutex(mutex) {}

public:
    ~ScopedLock() {
        reset();
    }

    void reset() {
        if (_mutex) {
            _mutex->unlock();
            _mutex = nullptr;
        }
    }

private:
    Mutex* _mutex;
};

namespace detail {

class MutexAwaitable {
private:
    friend await_ready_trait<Mutex>;

    MutexAwaitable(Mutex* mutex, ExecutorRef executor)
        : _mutex(mutex)
        , _executor(std::move(executor)) {}

public:
    bool await_ready() noexcept {
        return _mutex->try_lock();
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> continuation) noexcept {
        _continuation = continuation;
        const bool queued = _mutex->lock_or_queue(this);
        if (queued) {
            _executor->external(continuation);
        }
        return queued;
    }

    ScopedLock await_resume() noexcept {
        return ScopedLock {_mutex};
    }

private:
    friend class ::coro::Mutex;
    void mutex_available() {
        _executor->schedule(_continuation);
    }

private:
    Mutex* _mutex;
    Executor::Ref _executor;
    std::coroutine_handle<> _continuation;
};

} // namespace detail

inline void Mutex::unlock() {
    std::scoped_lock lock {_mutex};
    auto* awaiter = _awaiters.pop().value_or(nullptr);
    if (awaiter) {
        // since this is a first come first serve mutex
        // if there is an awaiter in the queue pass lock to it without unlocking
        awaiter->mutex_available();
    } else {
        _locked = false;
    }
}

template <>
struct await_ready_trait<Mutex> {
    static detail::MutexAwaitable await_transform(const Executor::Ref& executor, Mutex& mutex) {
        return detail::MutexAwaitable {&mutex, executor};
    }
};

} // namespace coro

#pragma once

#include "../core/promise.hpp"

#include <condition_variable>
#include <coroutine>
#include <map>
#include <mutex>
#include <thread>

namespace coro {

namespace detail {

class TimedScheduler {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    TimedScheduler() {
        _thread = std::thread([this] {
            while (_loop) {
                std::unique_lock lock {_mutex};
                if (_timeouts.empty()) {
                    _cv.wait(lock, [this] { return !_loop || !_timeouts.empty(); });
                }
                if (!_loop) return;
                while (true) {
                    auto timePoint = _timeouts.begin()->first;
                    auto status = _cv.wait_until(lock, timePoint);
                    if (!_loop) return;
                    if (status == std::cv_status::timeout) {
                        break;
                    }
                }
                cleanupFiredTimers();
            }
        });
    }

    ~TimedScheduler() {
        _loop = false;
        _cv.notify_one();
        _thread.join();
    }

    void timeout(std::chrono::milliseconds time, Callback::WeakRef callback) {
        auto now = Clock::now();
        now += time;
        std::scoped_lock lock {_mutex};
        _timeouts[now].push_back(std::move(callback));
        _cv.notify_one();
    }

private:
    void cleanupFiredTimers() {
        auto now = Clock::now();
        auto it = _timeouts.begin();
        for (; it != _timeouts.end() && now >= it->first; ++it) {
            for (auto& weakCB : it->second) {
                auto callback = weakCB.lock();
                if (callback) {
                    callback->invoke();
                }
            }
        }
        _timeouts.erase(_timeouts.begin(), it);
    }

private:
    std::thread _thread;
    std::map<TimePoint, std::vector<Callback::WeakRef>> _timeouts;
    std::condition_variable _cv;
    std::mutex _mutex;
    std::atomic<bool> _loop = true;
};

class SleepAwaitable {
public:
    SleepAwaitable(uint32_t sleep)
        : _sleep(sleep) {}

    bool await_ready() noexcept {
        return false;
    }

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> continuation) noexcept {
        static detail::TimedScheduler scheduler;
        _continuation = CoroHandle::fromTypedHandle(continuation);
        auto& promise = _continuation.promise();
        promise.executor->external(_continuation);
        _callback = Callback::create([handle = _continuation]() {
            auto& promise = handle.promise();
            promise.executor->schedule(std::move(handle));
        });
        scheduler.timeout(std::chrono::milliseconds {_sleep}, _callback);
    }

    void await_resume() {
        detail::AtExit exit {[this]() noexcept {
            _callback.reset();
            _continuation.reset();
        }};
        _continuation.throwIfStopped();
    }

private:
    CoroHandle _continuation;
    Callback::Ref _callback;
    uint32_t _sleep;
};

} // namespace detail

inline detail::SleepAwaitable sleep(uint32_t time) {
    return detail::SleepAwaitable {time};
}

template <>
struct await_ready_trait<detail::SleepAwaitable> {
    static detail::SleepAwaitable&& await_transform(const PromiseBase&, detail::SleepAwaitable&& awaitable) {
        return std::move(awaitable);
    }
};

} // namespace coro

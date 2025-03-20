#pragma once

#include "../core/promise.hpp"

#include <condition_variable>
#include <coroutine>
#include <map>
#include <mutex>
#include <thread>

#include <iostream>

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

    void timeout(std::chrono::milliseconds time, CoroHandle handle) {
        auto now = Clock::now();
        now += time;
        std::scoped_lock lock {_mutex};
        _timeouts[now].push_back(std::move(handle));
        _cv.notify_one();
    }

private:
    void cleanupFiredTimers() {
        auto now = Clock::now();
        auto it = _timeouts.begin();
        for (; it != _timeouts.end() && now >= it->first; ++it) {
            auto handles = std::move(it->second);
            for (auto& handle : handles) {
                auto& promise = handle.promise();
                promise.context.executor->schedule(std::move(handle));
            }
        }
        _timeouts.erase(_timeouts.begin(), it);
    }

private:
    std::thread _thread;
    std::map<TimePoint, std::vector<CoroHandle>> _timeouts;
    std::condition_variable _cv;
    std::mutex _mutex;
    std::atomic<bool> _loop = true;
};

} // namespace detail

class TimeoutAwaitable {
public:
    TimeoutAwaitable(uint32_t timeout)
        : _timeout(timeout) {}

    bool await_ready() noexcept {
        return false;
    }

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> continuation) noexcept {
        static detail::TimedScheduler scheduler;
        auto handle = CoroHandle::fromTypedHandle(continuation);
        auto& promise = handle.promise();
        promise.context.executor->external(handle);
        scheduler.timeout(std::chrono::milliseconds {_timeout}, std::move(handle));
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
    static TimeoutAwaitable&& await_transform(const TaskContext&, TimeoutAwaitable&& awaitable) {
        return std::move(awaitable);
    }
};

} // namespace coro

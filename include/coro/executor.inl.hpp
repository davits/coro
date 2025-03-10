#ifndef CORO_EXECUTOR_INL_HPP
#define CORO_EXECUTOR_INL_HPP

#include "executor.hpp"
#include "task.hpp"

#include <thread>

namespace coro {

template <typename R>
R SerialExecutor::run(Task<R>& task) {
    task.scheduleOn(shared_from_this());
    while (true) {
        std::unique_lock lock {_mutex};
        if (_tasks.empty()) {
            if (_external.empty()) {
                break;
            }
            _cv.wait(lock, [this] { return !_tasks.empty(); });
        }
        auto task = _tasks.pop().value();
        // release lock and give a chance to schedule while task is being executed
        lock.unlock();
        task.resume();
    }
    if constexpr (std::move_constructible<R>) {
        return std::move(task).value();
    } else {
        return task.value();
    }
}

inline void SerialExecutor::schedule(handle_t coro) {
    {
        std::scoped_lock lock {_mutex};
        _tasks.push(coro);
        _external.erase(coro);
    }
    _cv.notify_one();
}

inline void SerialExecutor::timeout(uint32_t timeout, handle_t coro) {
    external(coro);
    std::thread([this, timeout, coro]() {
        std::this_thread::sleep_for(std::chrono::milliseconds {timeout});
        schedule(coro);
    }).detach();
}

inline void SerialExecutor::external(handle_t coro) {
    std::scoped_lock lock {_mutex};
    _external.insert(coro);
}

} // namespace coro

#endif

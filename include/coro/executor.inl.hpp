#ifndef CORO_EXECUTOR_INL_HPP
#define CORO_EXECUTOR_INL_HPP

#include "executor.hpp"
#include "task.hpp"

#include <concepts>
#include <thread>

namespace coro {

template <typename R>
R SerialExecutor::run(Task<R>& task) {
    task.scheduleOn(this);
    while (step())
        ;

    if constexpr (std::move_constructible<R>) {
        return std::move(task).value();
    } else {
        return task.value();
    }
}

inline void SerialExecutor::schedule(std::coroutine_handle<> coro) {
    _tasks.push(coro);
}

inline void SerialExecutor::timeout(uint32_t timeout, std::coroutine_handle<> coro) {
    std::this_thread::sleep_for(std::chrono::milliseconds {timeout});
    _tasks.push(coro);
}

inline bool SerialExecutor::step() {
    if (_tasks.empty()) {
        return false;
    }
    auto coro = _tasks.front();
    _tasks.pop();
    coro.resume();
    return !_tasks.empty();
}

} // namespace coro

#endif

#ifndef CORO_EXECUTOR_INL_HPP
#define CORO_EXECUTOR_INL_HPP

#include "executor.hpp"
#include "task.hpp"

#include <concepts>

namespace coro {

template <typename R>
R Executor::run(Task<R>& task) {
    task.setExecutor(this);
    schedule(task.handle());
    while (resumeNext())
        ;

    if constexpr (std::move_constructible<R>) {
        return std::move(task).value();
    } else {
        return task.value();
    }
}

inline void StackedExecutor::schedule(std::coroutine_handle<> handle) {
    _tasks.push_back(handle);
}

inline bool StackedExecutor::resumeNext() {
    clearFinished();
    if (_tasks.empty()) {
        return false;
    }
    auto& task = _tasks.back();
    task.resume();
    clearFinished();
    return !_tasks.empty();
}

inline void StackedExecutor::clearFinished() {
    while (!_tasks.empty() && _tasks.back().done()) {
        _tasks.pop_back();
    }
}

} // namespace coro

#endif

#pragma once

#include "executor.hpp"
#include "task.hpp"

namespace coro {

template <typename R>
R&& Executor::runAndWait(Task<R>& task) {
    task.setExecutor(this);
    execute(task.handle());
    return std::move(task).value();
}

inline void Executor::runAndWait(Task<void>& task) {
    task.setExecutor(this);
    execute(task.handle());
    return std::move(task).value();
}

inline void StackedExecutor::schedule(std::coroutine_handle<> handle) {
    _tasks.push_back(handle);
}

inline void StackedExecutor::execute(std::coroutine_handle<> root) {
    while (!root.done()) {
        root.resume();
        while (!_tasks.empty()) {
            auto& task = _tasks.back();
            if (task.done()) {
                _tasks.pop_back();
            } else {
                task.resume();
            }
        }
    }
}

} // namespace coro

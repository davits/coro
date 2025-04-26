#pragma once

#include "executor.hpp"
#include "task.hpp"

namespace coro {

template <typename R>
Task<R> Executor::schedule(Task<R>&& task) {
    CoroHandle handle = task.handle();
    PromiseBase& p = handle.promise();
    p.context.executor = shared_from_this();
    p.context.executor->schedule(std::move(handle));
    return std::move(task);
}

} // namespace coro

#pragma once

#include "promise.hpp"
#include "executor.hpp"
#include "task.hpp"

namespace coro {

template <typename U>
Awaitable<Task<U>> PromiseBase::await_transform(Task<U>&& task) {
    task.setExecutor(executor);
    executor->schedule(task.handle());
    Awaitable<Task<U>> result {std::move(task)};
    return result;
}

template <typename R>
Task<R> Promise<R>::get_return_object() {
    return Task<R>(handle_t::from_promise(*this));
}

inline Task<void> Promise<void>::get_return_object() {
    return Task<void>(handle_t::from_promise(*this));
}

} // namespace coro

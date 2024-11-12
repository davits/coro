#pragma once

#include "promise.hpp"
#include "executor.hpp"
#include "task.hpp"

namespace coro {

template <typename U>
Awaitable<Task<U>> PromiseBase::await_transform(Task<U>&& task) {
    return Awaitable<Task<U>> {std::move(task)};
}

template <typename R>
Task<R> Promise<R>::get_return_object() {
    return Task<R>(handle_t::from_promise(*this));
}

inline Task<void> Promise<void>::get_return_object() {
    return Task<void>(handle_t::from_promise(*this));
}

} // namespace coro

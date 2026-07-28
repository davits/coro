#pragma once

#include "promise.hpp"
#include "executor.hpp"
#include "task.hpp"

namespace coro {

template <typename R>
Task<R> Promise<R>::get_return_object() {
    return Task<R>(CoroHandle::fromTypedHandle(handle_t::from_promise(*this)));
}

inline Task<void> Promise<void>::get_return_object() {
    return Task<void>(CoroHandle::fromTypedHandle(handle_t::from_promise(*this)));
}

} // namespace coro

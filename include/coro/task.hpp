#pragma once

#include "promise.hpp"
#include "expected.hpp"
#include "executor.hpp"
#include "detail/utils.hpp"

#include <coroutine>

namespace coro {

class Executor;

template <typename R>
class Task : public detail::OnlyMovable {
public:
    using Type = R;
    using promise_type = Promise<R>;
    using handle_t = promise_type::handle_t;

    Task(handle_t handle)
        : _handle(handle) {}

    Task(Task&& o)
        : _handle(std::exchange(o._handle, nullptr)) {}

    Task& operator=(Task&& o) {
        destroy();
        _handle = std::exchange(o._handle, nullptr);
    }

    ~Task() {
        #if defined(CORO_ABORT_ON_SUSPENDED_TASK_DESTRUCTION)
        if (_handle && !_handle.done()) {
            std::abort();
        }
        #endif
        destroy();
    }

public:
    // TODO make private with friend promise
    void destroy() {
        if (_handle) {
            _handle.destroy();
            _handle = nullptr;
        }
    }

public:
    bool ready() const {
        return false;
    }

    decltype(auto) value() const& {
        return promise().storage().value();
    }

    decltype(auto) value() && {
        return std::move(promise().storage()).value();
    }

    const promise_type& promise() const {
        return _handle.promise();
    }

    promise_type& promise() {
        return _handle.promise();
    }

public:
    void scheduleOn(Executor::Ref executor) {
        auto& p = promise();
        p.executor = std::move(executor);
        p.executor->schedule(_handle);
    }

private:
    handle_t _handle;
};

} // namespace coro

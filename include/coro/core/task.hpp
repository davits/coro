#pragma once

#include "promise.hpp"
#include "expected.hpp"
#include "executor.hpp"
#include "handle.hpp"

#include "../detail/utils.hpp"

#include <coroutine>
#include <future>

namespace coro {

class Executor;

template <typename R>
class Task : public detail::OnlyMovable {
public:
    using Type = R;
    using promise_type = Promise<R>;

    Task(CoroHandle handle)
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
    void destroy() {
        _handle.reset();
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
        return _handle.promise<promise_type>();
    }

    promise_type& promise() {
        return _handle.promise<promise_type>();
    }

public:
    void scheduleOn(Executor::Ref executor) & {
        auto& p = promise();
        p.context.executor = std::move(executor);
        p.context.executor->schedule(_handle);
    }

    void schedule() & {
        auto& p = promise();
        p.context.executor->schedule(_handle);
    }

    const StopToken& stopToken() const {
        return promise().context.stopToken;
    }

    void setStopToken(StopToken token) {
        auto& p = promise();
        p.context.stopToken = std::move(token);
    }

    const UserData::Ref& userData() const {
        return promise().context.userData;
    }

    void setUserData(UserData::Ref userData) {
        promise().context.userData = std::move(userData);
    }

private:
    CoroHandle _handle;
};

} // namespace coro

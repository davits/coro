#pragma once

#include "promise.hpp"
#include "handle.hpp"

#include "../detail/utils.hpp"

namespace coro {

class Executor;

template <typename R>
class Task {
public:
    using Type = R;
    using promise_type = Promise<R>;

    Task(CoroHandle handle)
        : _handle(std::move(handle)) {}

public:
    // Move only
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&&) = default;
    Task& operator=(Task&&) = delete;

public:
    void reset() {
        _handle.reset();
    }

    bool ready() const {
        return promise().finished();
    }

public:
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

    CoroHandle handle() const {
        return _handle;
    }

    template <typename Callable>
    void then(Callable&& callable) {
        promise().then(std::forward<Callable>(callable));
    }

private:
    friend struct Awaitable<Task>;
    promise_type& promise() {
        return _handle.promise<promise_type>();
    }

    const promise_type& promise() const {
        return _handle.promise<promise_type>();
    }

private:
    CoroHandle _handle;
};

} // namespace coro

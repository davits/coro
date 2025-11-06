#pragma once

#include "promise.hpp"
#include "handle.hpp"

namespace coro {

class Executor;

/**
 * Asynchronous task wrapping underlying C++ coroutine.
 * Task is designed to be a return value from the C++ coroutine.
 * Task is lazy, meaning it needs to be scheduled on executor to start working. It can be scheduled explicitly via the
 * executor functions or it will be scheduled automatically when co_await(ed) from another task.
 */
template <typename R>
class Task {
public:
    using Type = R;
    using promise_type = Promise<R>;

public:
    Task() = default;

    Task(CoroHandle handle)
        : _handle(std::move(handle)) {}

public:
    // Move only
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&&) = default;
    Task& operator=(Task&&) = default;

public:
    void reset() {
        _handle.reset();
    }

    bool ready() const {
        return promise().finished();
    }

    explicit operator bool() const {
        return static_cast<bool>(_handle);
    }

public:
    const StopToken& stopToken() const {
        return promise().context.stopToken;
    }

    void setStopToken(StopToken token) & {
        auto& p = promise();
        p.context.stopToken = std::move(token);
    }

    Task&& setStopToken(StopToken token) && {
        auto& p = promise();
        p.context.stopToken = std::move(token);
        return std::move(*this);
    }

    const UserData::Ref& userData() const {
        return promise().context.userData;
    }

    void setUserData(UserData::Ref userData) & {
        promise().context.userData = std::move(userData);
    }

    Task&& setUserData(UserData::Ref userData) && {
        promise().context.userData = std::move(userData);
        return std::move(*this);
    }

    const TaskContext& context() const {
        return promise().context;
    }

    void setContext(const TaskContext& context) & {
        promise().context = context;
    }

    Task&& setContext(const TaskContext& context) && {
        promise().context = context;
        return std::move(*this);
    }

    CoroHandle handle() const {
        return _handle;
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

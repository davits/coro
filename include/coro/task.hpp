#pragma once

#include "promise.hpp"

#include <coroutine>

namespace coro {

class Executor;

class TaskBase {
public:
    TaskBase(std::coroutine_handle<> handle)
        : _typeless_handle(handle) {}

    TaskBase(TaskBase&& other)
        : _typeless_handle(other._typeless_handle) {
        other._typeless_handle = nullptr;
    }

    virtual ~TaskBase() {
        if (_typeless_handle) {
            _typeless_handle.destroy();
        }
    }

    bool done() const {
        return _typeless_handle.done();
    }

    void resume() const {
        _typeless_handle.resume();
    }

    std::coroutine_handle<> handle() const {
        return _typeless_handle;
    }

private:
    std::coroutine_handle<> _typeless_handle;
};

template <typename R>
class Task : public TaskBase {
public:
    using Type = R;
    using promise_type = Promise<R>;
    using handle_t = promise_type::handle_t;

    Task(handle_t handle)
        : TaskBase(handle)
        , _handle(handle) {}

    Task(Task&& o)
        : TaskBase(std::move(o))
        , _handle(o._handle) {
        o._handle = nullptr;
    }

    const R& value() const& {
        auto& promise = _handle.promise();
        return promise.value();
    }

    R&& value() && {
        auto& promise = _handle.promise();
        return std::move(promise).value();
    }

    promise_type& promise() {
        return _handle.promise();
    }

    void set_stop_token(StopToken token) {
        promise().set_stop_token(std::move(token));
    }

public:
    void setExecutor(Executor* exec) {
        auto& promise = _handle.promise();
        promise.executor = exec;
    }

private:
    handle_t _handle;
};

template <>
class Task<void> : public TaskBase {
public:
    using Type = void;
    using promise_type = Promise<void>;
    using handle_t = promise_type::handle_t;

    Task(handle_t handle)
        : TaskBase(handle)
        , _handle(handle) {}

    Task(Task&& o)
        : TaskBase(std::move(o))
        , _handle(o._handle) {
        o._handle = nullptr;
    }

    void value() const {
        auto& promise = _handle.promise();
        return promise.value();
    }

    promise_type& promise() {
        return _handle.promise();
    }

    void set_stop_token(StopToken token) {
        promise().set_stop_token(std::move(token));
    }

public:
    void setExecutor(Executor* exec) {
        auto& promise = _handle.promise();
        promise.executor = exec;
    }

private:
    handle_t _handle;
};

} // namespace coro

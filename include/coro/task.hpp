#pragma once

#include "promise.hpp"
#include "expected.hpp"

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
        , _handle(handle) {
        auto& promise = _handle.promise();
        promise.update_storage(&_storage);
    }

    Task(Task&& o)
        : TaskBase(std::move(o))
        , _handle(o._handle)
        , _storage(std::move(o._storage)) {
        o._handle = nullptr;
        if (_handle) {
            promise().update_storage(&_storage);
        }
    }

    Task(const R& value)
        : TaskBase(nullptr)
        , _handle(nullptr)
        , _storage(value) {}

    Task(R&& value)
        : TaskBase(nullptr)
        , _handle(nullptr)
        , _storage(std::move(value)) {}

public:
    bool ready() const {
        return _storage.has_value();
    }

    const R& value() const& {
        return _storage.value();
    }

    R&& value() && {
        return std::move(_storage).value();
    }

    promise_type& promise() {
        return _handle.promise();
    }

public:
    void setExecutor(Executor* exec) {
        auto& promise = _handle.promise();
        promise.executor = exec;
    }

private:
    handle_t _handle;
    expected<R> _storage;
};

template <>
class Task<void> : public TaskBase {
public:
    using Type = void;
    using promise_type = Promise<void>;
    using handle_t = promise_type::handle_t;

    Task(handle_t handle)
        : TaskBase(handle)
        , _handle(handle) {
        promise().update_storage(&_storage);
    }

    Task(Task&& o)
        : TaskBase(std::move(o))
        , _handle(o._handle)
        , _storage(std::move(o._storage)) {
        o._handle = nullptr;
        if (_handle) {
            promise().update_storage(&_storage);
        }
    }

    Task()
        : TaskBase(nullptr)
        , _storage() {
        _storage.emplace_value();
    }

public:
    bool ready() const {
        return _storage.has_value();
    }

    void value() const {
        return _storage.value();
    }

    promise_type& promise() {
        return _handle.promise();
    }

public:
    void setExecutor(Executor* exec) {
        auto& promise = _handle.promise();
        promise.executor = exec;
    }

private:
    handle_t _handle;
    expected<void> _storage;
};

} // namespace coro

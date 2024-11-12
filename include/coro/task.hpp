#pragma once

#include "promise.hpp"
#include "expected.hpp"
#include "executor.hpp"

#include <coroutine>

namespace coro {

class Executor;

template <typename R>
class Task {
public:
    using Type = R;
    using promise_type = Promise<R>;
    using handle_t = promise_type::handle_t;

    Task(handle_t handle)
        : _handle(handle) {}

    Task(Task&& o)
        : _handle(std::exchange(o._handle, {})) {}

    ~Task() {
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

    const R& value() const& {
        return promise().storage().value();
    }

    R&& value() && {
        return std::move(promise().storage()).value();
    }

    promise_type& promise() {
        return _handle.promise();
    }

public:
    void scheduleOn(Executor* executor) {
        auto& p = promise();
        p.executor = executor;
        executor->schedule(_handle);
    }

private:
    handle_t _handle;
};

template <>
class Task<void> {
public:
    using Type = void;
    using promise_type = Promise<void>;
    using handle_t = promise_type::handle_t;

    Task(handle_t handle)
        : _handle(handle) {}

    Task(Task&& o)
        : _handle(std::exchange(o._handle, {})) {}

    ~Task() {
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

    void value() const {
        return promise().storage().value();
    }

    promise_type& promise() const {
        return _handle.promise();
    }

public:
    void scheduleOn(Executor* executor) {
        auto& p = promise();
        p.executor = executor;
        executor->schedule(_handle);
    }

private:
    handle_t _handle;
};

/*
template <typename R>
class ReadyTask {
    ReadyTask(Task<R>&& task);
    ReadyTask(R value);

private:
    Task<R> _task;
    expected<R> _storage;
};
*/

} // namespace coro

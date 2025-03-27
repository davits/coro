#pragma once

// #ifndef CORO_EMSCRIPTEN
// #error This file can only be included in the emscripten build.
// #endif

#include "../core/executor.hpp"
#include "../core/handle.hpp"
#include "../core/task_context.hpp"
#include "../core/handle.inl.hpp"

#include <emscripten/val.h>

namespace coro {

namespace detail {
extern "C" void _coro_lib_await_promise(emscripten::EM_VAL promiseHandle, void* awaiter);
} // namespace detail

class JSError : public std::runtime_error {
public:
    JSError(emscripten::val&& error)
        : std::runtime_error("")
        , _error(std::move(error)) {}

private:
    emscripten::val _error;
};

struct ValAwaitable {
    ValAwaitable(coro::Executor::Ref executor, emscripten::val&& val)
        : _promise(std::move(val))
        , _executor(std::move(executor))
        , _result() {}

    ValAwaitable& operator co_await() {
        detail::_coro_lib_await_promise(_promise.as_handle(), this);
        return *this;
    }

    bool await_ready() noexcept {
        return _ready;
    }

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> continuation) noexcept {
        _continuation = CoroHandle::fromTypedHandle(continuation);
        _executor->external(_continuation);
        _notifiedExecutor = true;
    }

    emscripten::val await_resume() {
        if (_error) {
            throw JSError(std::move(_result));
        }
        return std::move(_result);
    }

    void resolve(emscripten::val&& result) {
        _result = result;
        _ready = true;
        if (_notifiedExecutor) {
            _executor->schedule(std::move(_continuation));
        }
    }

    void reject(emscripten::val&& error) {
        _result = std::move(error);
        _ready = true;
        _error = true;
        if (_notifiedExecutor) {
            _executor->schedule(std::move(_continuation));
        }
    }

private:
    emscripten::val _promise;
    emscripten::val _result = emscripten::val::undefined();
    coro::Executor::Ref _executor = nullptr;
    CoroHandle _continuation;
    bool _ready = false;
    bool _error = false;
    bool _notifiedExecutor = false;
};

template <>
struct await_ready_trait<emscripten::val> {
    static ValAwaitable await_transform(const TaskContext& context, emscripten::val&& awaitable) {
        return ValAwaitable {context.executor, std::move(awaitable)};
    }
};

} // namespace coro

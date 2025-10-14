#pragma once

// #ifndef CORO_EMSCRIPTEN
// #error This file can only be included in the emscripten build.
// #endif

#include "../core/executor.hpp"
#include "../core/handle.hpp"
#include "../core/handle.inl.hpp"
#include "../core/promise_base.hpp"

#include <emscripten/val.h>

namespace coro {

namespace detail {
extern "C" emscripten::EM_VAL _coro_lib_await_promise(emscripten::EM_VAL promiseHandle, void* awaiter);
} // namespace detail

class JSError : public std::runtime_error {
public:
    JSError(emscripten::val&& error)
        : std::runtime_error("")
        , _error(std::move(error)) {}

    const emscripten::val error() const {
        return _error;
    }

private:
    emscripten::val _error;
};

struct ValAwaitable {
    ValAwaitable(emscripten::val&& val, const PromiseBase& promise)
        : _promise(std::move(val))
        , _executor(std::move(promise.executor))
        , _stopToken(promise.context.stopToken) {}

    ValAwaitable& operator co_await() {
        auto controllerHandle = detail::_coro_lib_await_promise(_promise.as_handle(), this);
        _controller = emscripten::val::take_ownership(controllerHandle);
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
        if (_stopToken.stopRequested()) {
            _controller.call<void>("abort");
            _stopToken.throwException();
        }
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
    emscripten::val _controller;
    emscripten::val _result;
    coro::Executor::Ref _executor = nullptr;
    StopToken _stopToken;
    CoroHandle _continuation;
    bool _ready = false;
    bool _error = false;
    bool _notifiedExecutor = false;
};

template <>
struct await_ready_trait<emscripten::val> {
    static ValAwaitable await_transform(const PromiseBase& promise, emscripten::val awaitable) {
        return ValAwaitable {std::move(awaitable), promise};
    }
};

} // namespace coro

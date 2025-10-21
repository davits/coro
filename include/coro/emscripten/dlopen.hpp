#pragma once

#include "../core/executor.hpp"
#include "../core/handle.hpp"
#include "../core/handle.inl.hpp"
#include "../core/promise_base.hpp"

#include <dlfcn.h>
#include <emscripten/emscripten.h>

#include <memory>
#include <string>

namespace coro {

namespace detail {

struct DlopenInfo {
    std::string path;
    int flags;
};

void onDlopenSuccess(void* userData, void* handle);
void onDlopenError(void* userData);

class DlopenAwaitable;

struct DlopenContext {
    DlopenAwaitable* awaitable;
    bool cancelled;

    DlopenContext(DlopenAwaitable* a)
        : awaitable(a)
        , cancelled(false) {}
};

class DlopenAwaitable {
public:
    DlopenAwaitable(const PromiseBase& promise, DlopenInfo&& info)
        : _executor(promise.executor)
        , _stopToken(promise.context.stopToken)
        , _info(std::move(info)) {}

    DlopenAwaitable& operator co_await() {
        // emscripten_dlopen can fire success/error callbacks in place if the requested library has already been opened.
        // so we use operator co_await() to launch it, so if that is the case we can detect it and
        // not suspend awaiting coroutine unnecessarily
        _context = new DlopenContext(this);
        emscripten_dlopen(_info.path.c_str(), _info.flags, _context, onDlopenSuccess, onDlopenError);
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

    void* await_resume() {
        if (_stopToken.stopRequested()) {
            if (_context) {
                _context->cancelled = true;
            }
            _stopToken.throwException();
        }
        return _handle;
    }

    void resolve(void* handle) {
        _handle = handle;
        _context = nullptr;
        _ready = true;
        if (_notifiedExecutor) {
            _executor->schedule(_continuation);
        }
    }

    void reject() {
        _handle = nullptr;
        _context = nullptr;
        _ready = true;
        if (_notifiedExecutor) {
            _executor->schedule(_continuation);
        }
    }

private:
    coro::Executor::Ref _executor;
    CoroHandle _continuation;
    StopToken _stopToken;
    DlopenInfo _info;
    DlopenContext* _context = nullptr;
    void* _handle = nullptr;
    bool _notifiedExecutor = false;
    bool _ready = false;
};

inline void onDlopenSuccess(void* userData, void* handle) {
    std::unique_ptr<DlopenContext> context {static_cast<DlopenContext*>(userData)};
    if (!context->cancelled) {
        context->awaitable->resolve(handle);
    }
}

inline void onDlopenError(void* userData) {
    std::unique_ptr<DlopenContext> context {static_cast<DlopenContext*>(userData)};
    if (!context->cancelled) {
        context->awaitable->reject();
    }
}

} // namespace detail

inline detail::DlopenInfo dlopen(std::string path, int flags) {
    return detail::DlopenInfo {std::move(path), flags};
}

inline const char* dlerror() {
    return ::dlerror();
}

template <>
struct await_ready_trait<detail::DlopenInfo> {
    static detail::DlopenAwaitable await_transform(const PromiseBase& promise, detail::DlopenInfo&& awaitable) {
        return detail::DlopenAwaitable {promise, std::move(awaitable)};
    }
};

} // namespace coro

#pragma once

#include "../core/promise_base.hpp"
#include "../core/handle.hpp"
#include "../core/handle.inl.hpp"

#include <dlfcn.h>
#include <emscripten/emscripten.h>
#include <string>

namespace coro {

namespace detail {

void onEmscriptenDlopenSuccess(void* userData, void* handle);
void onEmscriptenDlopenError(void* userData);

} // namespace detail

struct DlopenResult {
    void* handle = nullptr;
    bool success = false;
    std::string url;
    std::string errorMessage;

    DlopenResult() = default;
    DlopenResult(void* h, bool s, std::string u) 
        : handle(h), success(s), url(std::move(u)) {}
    DlopenResult(void* h, bool s, std::string u, std::string err) 
        : handle(h), success(s), url(std::move(u)), errorMessage(std::move(err)) {}
};

class DlopenAwaitable {
public:
    DlopenAwaitable(coro::Executor::Ref executor, std::string url, int flags)
        : _executor(std::move(executor))
        , _url(std::move(url))
        , _flags(flags) {}

    bool await_ready() noexcept {
        return false;
    }

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> continuation) noexcept {
        _continuation = CoroHandle::fromTypedHandle(continuation);
        auto& promise = _continuation.promise();
        promise.executor->external(_continuation);

        emscripten_dlopen(_url.c_str(), _flags, this, 
                         detail::onEmscriptenDlopenSuccess, 
                         detail::onEmscriptenDlopenError);
    }

    DlopenResult await_resume() {
        return std::move(_result);
    }

    void resolve(void* handle) {
        _result = DlopenResult{handle, true, _url};
        _executor->schedule(_continuation);
    }

    void reject() {
        const char* dlError = dlerror();
        std::string errorMsg = dlError ? dlError : "Unknown dlopen error";
        _result = DlopenResult{nullptr, false, _url, errorMsg};
        _executor->schedule(_continuation);
    }

    template<typename T>
    friend struct await_ready_trait;

private:
    coro::Executor::Ref _executor;
    std::string _url;
    int _flags;
    CoroHandle _continuation;
    DlopenResult _result;
};

namespace detail {

inline void onEmscriptenDlopenSuccess(void* userData, void* handle) {
    auto* awaitable = static_cast<DlopenAwaitable*>(userData);
    awaitable->resolve(handle);
}

inline void onEmscriptenDlopenError(void* userData) {
    auto* awaitable = static_cast<DlopenAwaitable*>(userData);
    awaitable->reject();
}

} // namespace detail

inline DlopenAwaitable dlopenAsync(const std::string& url, int flags = RTLD_NOW | RTLD_GLOBAL) {
    return DlopenAwaitable{nullptr, url, flags};
}

template <>
struct await_ready_trait<DlopenAwaitable> {
    static DlopenAwaitable await_transform(const PromiseBase& promise, DlopenAwaitable&& awaitable) {
        awaitable._executor = promise.executor;
        return std::move(awaitable);
    }
};

} // namespace coro

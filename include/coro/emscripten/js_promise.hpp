#pragma once

#include <emscripten/val.h>

namespace coro::detail {

extern "C" emscripten::EM_VAL _coro_lib_make_promise(emscripten::EM_VAL* resolve, emscripten::EM_VAL* reject);

class JSPromise {
public:
    JSPromise() {}

public:
    static JSPromise create() {
        JSPromise promise;
        emscripten::EM_VAL resolveHandle;
        emscripten::EM_VAL rejectHandle;
        promise._promise = emscripten::val::take_ownership(_coro_lib_make_promise(&resolveHandle, &rejectHandle));
        promise._resolve = emscripten::val::take_ownership(resolveHandle);
        promise._reject = emscripten::val::take_ownership(rejectHandle);
        return promise;
    }

    static JSPromise null() {
        return JSPromise {};
    }

    void resolve(const emscripten::val& value = emscripten::val::undefined()) {
        if (!_resolve.isUndefined()) {
            _resolve(value);
            reset();
        }
    }

    void reject(const char* error) {
        if (!_reject.isUndefined()) {
            _reject(emscripten::val {error});
            reset();
        }
    }

    void reject(const emscripten::val& error) {
        if (!_reject.isUndefined()) {
            _reject(error);
            reset();
        }
    }

    const emscripten::val& promise() const {
        return _promise;
    }

    explicit operator bool() const {
        return !_promise.isUndefined();
    }

    void reset() {
        _promise = emscripten::val::undefined();
        _resolve = emscripten::val::undefined();
        _reject = emscripten::val::undefined();
    }

private:
    emscripten::val _promise;
    emscripten::val _resolve;
    emscripten::val _reject;
};

} // namespace coro::detail

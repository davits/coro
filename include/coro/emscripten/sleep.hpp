#pragma once

#include "bridge.hpp"

#include "../core/stop.hpp"

#include <emscripten/val.h>

namespace coro {

namespace detail {
extern "C" emscripten::EM_VAL _coro_lib_sleep(int32_t ms);

// This class ensures that sleep works in the coro::Task coroutine context
class CancelableSleepHelper : public ValAwaitable {
public:
    CancelableSleepHelper(coro::Executor::Ref executor, emscripten::val sleep, StopToken token)
        : ValAwaitable(executor, sleep["promise"])
        , _stopCallback(token.addStopCallback([sleep]() { sleep.call<void>("cancel"); }))
        , _token(token) {}

    CancelableSleepHelper& operator co_await() {
        ValAwaitable::operator co_await();
        return *this;
    }

    emscripten::val await_resume() {
        try {
            _stopCallback.reset();
            return ValAwaitable::await_resume();
        } catch (const JSError&) {
            _token.throwIfStopped();
            throw;
        }
    }

private:
    Callback::Ref _stopCallback;
    StopToken _token;
};

} // namespace detail

// This class ensures that sleep works in the emscripten::val coroutine context
struct SleepAwaitable : public emscripten::val {
    SleepAwaitable(emscripten::val&& val)
        : emscripten::val(std::move(val)) {}

    auto operator co_await() {
        auto promise = operator[]("promise");
        return emscripten::val::awaiter {promise};
    }
};

/// @brief Asynchronous sleeper for the emscripten environment
/// co_await-ing sleep result will cause coroutine sleep for the specified amount of time.
/// @param ms Time to sleep in milliseconds.
inline SleepAwaitable sleep(uint32_t ms) {
    auto sleep = emscripten::val::take_ownership(detail::_coro_lib_sleep(static_cast<int32_t>(ms)));
    return {std::move(sleep)};
}

template <>
struct await_ready_trait<SleepAwaitable> {
    static detail::CancelableSleepHelper await_transform(const PromiseBase& promise, SleepAwaitable&& awaitable) {
        return {promise.executor, std::move(awaitable), promise.context.stopToken};
    }
};

} // namespace coro

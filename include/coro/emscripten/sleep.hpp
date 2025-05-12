#pragma once

#include "bridge.hpp"

#include "../core/stop.hpp"

#include <emscripten/val.h>

namespace coro {

namespace detail {
extern "C" emscripten::EM_VAL _coro_lib_sleep(int32_t ms);

class CancelableSleepHelper {
public:
    CancelableSleepHelper(coro::Executor::Ref executor, emscripten::val sleep, StopToken token)
        : _promiseAwaitable(std::move(executor), sleep["promise"])
        , _stopCallback(token.addStopCallback([sleep]() { sleep.call<void>("cancel"); })) {}

    ValAwaitable& operator co_await() {
        return _promiseAwaitable.operator co_await();
    }

private:
    ValAwaitable _promiseAwaitable;
    StopCallback::Ref _stopCallback;
};

} // namespace detail

struct SleepAwaitable : public emscripten::val {
    SleepAwaitable(emscripten::val&& val)
        : emscripten::val(std::move(val)) {}

    auto operator co_await() {
        auto promise = operator[]("promise");
        return emscripten::val::awaiter {promise};
    }
};

inline SleepAwaitable sleep(uint32_t ms) {
    auto sleep = emscripten::val::take_ownership(detail::_coro_lib_sleep(static_cast<int32_t>(ms)));
    return {std::move(sleep)};
}

template <>
struct await_ready_trait<SleepAwaitable> {
    static detail::CancelableSleepHelper
    await_transform(const ExecutorRef& executor, const TaskContext& context, SleepAwaitable&& awaitable) {
        return {executor, std::move(awaitable), context.stopToken};
    }
};

} // namespace coro

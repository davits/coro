#pragma once

#include "../core/awaitable.hpp"
#include "../core/callback.hpp"
#include "../core/promise_base.hpp"
#include "../core/traits.hpp"
#include "../core/stop.hpp"

#include <emscripten/val.h>

namespace coro {

class AbortSignal {
private:
    friend class AbortController;
    AbortSignal(emscripten::val signal)
        : _signal(std::move(signal)) {}

public:
    static AbortSignal timeout(int32_t timeout) {
        using namespace emscripten;
        return AbortSignal {val::global("AbortSignal").call<val>("timeout", timeout)};
    }

    template <typename... Args>
        requires(std::same_as<Args, AbortSignal> && ...)
    static AbortSignal any(const Args&... args) {
        using namespace emscripten;
        val array = val::array();
        (array.call<void>("push", args._signal), ...);
        auto signal = val::global("AbortSignal").call<val>("any", array);
        return AbortSignal {std::move(signal)};
    }

public:
    bool aborted() const {
        return _signal["aborted"].as<bool>();
    }

    operator emscripten::val() const {
        return _signal;
    }

private:
    emscripten::val _signal;
};

class AbortController {
public:
    AbortController() {
        _controller = emscripten::val::global("AbortController").new_();
    }

    void abort() {
        _controller.call<void>("abort");
    }

    AbortSignal signal(int32_t timeout = -1) {
        using namespace emscripten;
        AbortSignal signal {_controller["signal"]};
        if (timeout != -1) {
            signal = AbortSignal::any(signal, AbortSignal::timeout(timeout));
        }
        return signal;
    }

private:
    emscripten::val _controller;
};

class AbortToken {
public:
    AbortToken(StopToken token, int32_t timeout)
        : _controller()
        , _signal(_controller.signal(timeout))
        , _callback(token.addStopCallback([this]() { _controller.abort(); })) {}

    operator emscripten::val() {
        return static_cast<emscripten::val>(_signal);
    }

private:
    AbortController _controller;
    AbortSignal _signal;
    Callback::Ref _callback;
};

struct AbortTokenAwaitable {
    int32_t timeout;
};

inline AbortTokenAwaitable abortSignal(int32_t timeout = -1) {
    return AbortTokenAwaitable {timeout};
}

template <>
struct await_ready_trait<AbortTokenAwaitable> {
    static decltype(auto) await_transform(const PromiseBase& promise, const AbortTokenAwaitable& awaitable) {
        return ReadyAwaitable {AbortToken {promise.context.stopToken, awaitable.timeout}};
    }
};

} // namespace coro

#pragma once

#include "callback.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace coro {

class StopError : std::runtime_error {
public:
    StopError()
        : std::runtime_error("Stop was requested via stop token.") {}
};

class StopToken;

class StopState : public std::enable_shared_from_this<StopState> {
public:
    using Ptr = std::shared_ptr<StopState>;

private:
    friend class StopSource;
    struct Tag {};

public:
    StopState(Tag, std::exception_ptr&& exception) {
        _exception = exception ? std::move(exception) : std::make_exception_ptr(StopError {});
    }

public:
    void requestStop() noexcept {
        std::scoped_lock lock {_mutex};
        auto callbacks = std::move(_callbacks);
        for (const auto& weakCB : callbacks) {
            auto callback = weakCB.lock();
            if (callback) {
                callback->invoke();
            }
        }
        _stopRequested.store(true);
    }

    bool stopRequested() const noexcept {
        return _stopRequested.load();
    }

    void addStopCallback(Callback::WeakRef&& callbackWeak) {
        std::scoped_lock lock {_mutex};
        if (_stopRequested) {
            auto callbackRef = callbackWeak.lock();
            if (callbackRef) {
                callbackRef->invoke();
            }
        } else {
            _callbacks.push_back(std::move(callbackWeak));
        }
    }

    std::exception_ptr exception() const {
        return _exception;
    }

private:
    std::atomic<bool> _stopRequested = false;
    std::vector<Callback::WeakRef> _callbacks;
    std::exception_ptr _exception;
    std::mutex _mutex;
};

class StopToken {
public:
    StopToken() = default;
    StopToken(std::nullptr_t)
        : StopToken() {}

    StopToken(const StopToken&) = default;
    StopToken(StopToken&&) = default;
    StopToken& operator=(const StopToken&) = default;
    StopToken& operator=(StopToken&&) = default;

public:
    bool stopRequested() const noexcept {
        return _state && _state->stopRequested();
    }

    void throwIfStopped() const {
        if (stopRequested()) {
            std::rethrow_exception(exception());
        }
    }

    std::exception_ptr exception() const {
        return _state->exception();
    }

public:
    void addStopCallback(Callback::WeakRef callback) {
        if (_state) {
            _state->addStopCallback(std::move(callback));
        }
    }

    Callback::Ref addStopCallback(Callback::Func func) {
        if (!_state) {
            return Callback::Ref {};
        }
        auto result = Callback::create(std::move(func));
        addStopCallback(result);
        return result;
    }

    bool operator==(const StopToken& other) const {
        return _state == other._state;
    }

    explicit operator bool() const {
        return static_cast<bool>(_state);
    }

private:
    friend class StopSource;
    StopToken(StopState::Ptr state)
        : _state(std::move(state)) {}

private:
    StopState::Ptr _state;
};

class StopSource {
public:
    StopSource(std::exception_ptr exception = nullptr)
        : _state(std::make_shared<StopState>(StopState::Tag {}, std::move(exception))) {}

public:
    StopToken token() const noexcept {
        return StopToken {_state};
    }

    void requestStop() noexcept {
        _state->requestStop();
    }

    bool stopRequested() const noexcept {
        return _state->stopRequested();
    }

private:
    StopState::Ptr _state;
};

} // namespace coro

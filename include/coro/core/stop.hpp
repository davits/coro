#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace coro {

class StopError : std::runtime_error {
public:
    StopError()
        : std::runtime_error("Stop was requested via stop token.") {}
};

class StopToken;

class StopCallback {
public:
    using Ref = std::shared_ptr<StopCallback>;
    using WeakRef = std::weak_ptr<StopCallback>;
    using Func = std::function<void()>;

private:
    struct Tag {};

public:
    StopCallback(Tag, Func&& function)
        : _function(std::move(function)) {}

    static Ref create(Func function) {
        return std::make_shared<StopCallback>(Tag {}, std::move(function));
    }

    void invoke() noexcept {
        try {
            _function();
        } catch (...) {
        }
    }

private:
    Func _function;
};

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
        _stopRequested.store(true, std::memory_order_relaxed);
        for (const auto& weakCB : _callbacks) {
            auto callback = weakCB.lock();
            if (callback) {
                callback->invoke();
            }
        }
        _callbacks.clear();
    }

    bool stopRequested() const noexcept {
        return _stopRequested.load(std::memory_order_relaxed);
    }

    void addStopCallback(StopCallback::WeakRef&& callback) {
        _callbacks.push_back(std::move(callback));
    }

    std::exception_ptr exception() const {
        return _exception;
    }

private:
    std::atomic<bool> _stopRequested = false;
    std::vector<StopCallback::WeakRef> _callbacks;
    std::exception_ptr _exception;
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
    void addStopCallback(StopCallback::WeakRef callback) {
        if (_state) {
            _state->addStopCallback(std::move(callback));
        }
    }

    StopCallback::Ref addStopCallback(StopCallback::Func callback) {
        if (!_state) {
            return StopCallback::Ref {};
        }
        auto result = StopCallback::create(std::move(callback));
        addStopCallback(result);
        return result;
    }

    bool operator==(const StopToken& other) const {
        return _state == other._state;
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

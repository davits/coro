#pragma once

#include <memory>

namespace coro {

class StopState {
public:
    using Ptr = std::shared_ptr<StopState>;

private:
    friend class StopSource;
    struct Tag {};

public:
    StopState(Tag) {}

public:
    void request_stop() noexcept {
        _stopRequested = true;
    }

    bool stop_requested() const noexcept {
        return _stopRequested;
    }

private:
    bool _stopRequested = false;
};

class StopToken {
public:
    StopToken() = default;

    StopToken(const StopToken&) = default;
    StopToken(StopToken&&) = default;
    StopToken& operator=(const StopToken&) = default;
    StopToken& operator=(StopToken&&) = default;

public:
    bool stop_requested() const noexcept {
        return _state && _state->stop_requested();
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
    StopSource()
        : _state(std::make_shared<StopState>(StopState::Tag {})) {}

public:
    StopToken get_token() const noexcept {
        return StopToken {_state};
    }

    void request_stop() noexcept {
        _state->request_stop();
    }

    bool stop_requested() const noexcept {
        return _state->stop_requested();
    }

private:
    StopState::Ptr _state;
};

} // namespace coro

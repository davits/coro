#pragma once

#include <utility>

namespace coro::detail {

template <typename F>
concept NoThrowInvocable = requires(F f) {
    { f() } noexcept;
};

template <NoThrowInvocable F>
class AtExit {
public:
    AtExit(F f)
        : _callback(std::move(f)) {}

    ~AtExit() {
        _callback();
    }

private:
    F _callback;
};

} // namespace coro::detail

#pragma once

#include <utility>

namespace coro::detail {

struct OnlyMovable {
    OnlyMovable() = default;

    OnlyMovable(const OnlyMovable&) = delete;
    OnlyMovable& operator=(const OnlyMovable&) = delete;

    OnlyMovable(OnlyMovable&&) = default;
    OnlyMovable& operator=(OnlyMovable&&) = default;
};

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

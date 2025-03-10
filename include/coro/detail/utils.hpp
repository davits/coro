#pragma once

namespace coro::detail {

struct OnlyMovable {
    OnlyMovable() = default;

    OnlyMovable(const OnlyMovable&) = delete;
    OnlyMovable& operator=(const OnlyMovable&) = delete;

    OnlyMovable(OnlyMovable&&) = default;
    OnlyMovable& operator=(OnlyMovable&&) = default;
};

} // namespace coro::detail

#pragma once

namespace coro {

namespace detail {

template <typename T>
constexpr bool False = false;

} // namespace detail

struct TaskContext;

template <typename T>
struct await_ready_trait {
    static T&& await_transform(const TaskContext&, T&&) {
        static_assert(detail::False<T>, "Specialize this template for desired awaitable type.");
    }
};

} // namespace coro

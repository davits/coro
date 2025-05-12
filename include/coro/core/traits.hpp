#pragma once

#include <memory>

namespace coro {

namespace detail {

template <typename T>
constexpr bool False = false;

} // namespace detail

struct TaskContext;
class Executor;
using ExecutorRef = std::shared_ptr<Executor>;

template <typename T>
struct await_ready_trait {
    static T&& await_transform(const ExecutorRef&, const TaskContext&, T&&) {
        static_assert(detail::False<T>, "Specialize this template for desired awaitable type.");
    }
};

} // namespace coro

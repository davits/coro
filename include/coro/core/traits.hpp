#pragma once

namespace coro {

struct TaskContext;

template <typename T>
struct await_ready_trait {
    static T&& await_transform(const TaskContext&, T&&) {
        static_assert(false, "Specialize this template for desired awaitable type.");
    }
};

} // namespace coro

#pragma once

#include <memory>

namespace coro {

class Executor;
using ExecutorRef = std::shared_ptr<Executor>;

template <typename T>
struct await_ready_trait {
    static T&& await_transform(const ExecutorRef&, T&&) {
        static_assert(false, "Specialize this template for desired awaitable type.");
    }
};

} // namespace coro

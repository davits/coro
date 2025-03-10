#pragma once

#include <coroutine>
#include <memory>

namespace coro {

class Executor : public std::enable_shared_from_this<Executor> {
public:
    using Ref = std::shared_ptr<Executor>;

    virtual ~Executor() = default;

protected:
    Executor() = default;

public:
    using handle_t = std::coroutine_handle<>;

    /// Override should schedule given task in some internal storage to be executed later.
    virtual void schedule(handle_t coro) = 0;

    /// Will be called when the given task needs timeout while it is waiting for something to happen.
    virtual void timeout(uint32_t timeout, handle_t coro) = 0;

    /// Will be called to indicate that given coroutine is suspended and waiting
    /// for external event and will be scheduled in the future.
    virtual void external(handle_t coro) = 0;
};

} // namespace coro

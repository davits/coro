#pragma once

#include "handle.hpp"
#include "task.fwd.hpp"

#include <memory>

namespace coro {

class Executor : public std::enable_shared_from_this<Executor> {
public:
    using Ref = std::shared_ptr<Executor>;

    virtual ~Executor() = default;

protected:
    Executor() = default;

public:
    /// Will be called to schedule new independant handle.
    /// Override should schedule given handle in some internal storage to be executed later.
    virtual void schedule(CoroHandle coro) = 0;

    /// Will be called to indicate that execution is awaiting for a completion of the given coroutine.
    /// So it is in the best interest of overall execution to schedule incoming handle in such way,
    /// that it is executed next.
    virtual void next(CoroHandle coro) = 0;

    /// Will be called to indicate that given coroutine is suspended and waiting
    /// for external event and will be scheduled in the future, by external force.
    virtual void external(CoroHandle coro) = 0;

public:
    template <typename R>
    Task<R> schedule(Task<R>&& task);

    template <typename R>
    Task<R> next(Task<R>&& task);
};

} // namespace coro

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
    /// Schedules given task on executor, ideally without blocking the scheduling thread.
    /// Scheduling is done in a FIFO manner, to be executed when all other tasks in front of it are executed.
    /// Returns the same task, which has been marked as executing on this executor. It can be stored and co_await(ed)
    /// even from another executor.
    template <typename R>
    Task<R> schedule(Task<R>&& task);

    /// Schedules given task on executor, ideally without blocking the scheduling thread.
    /// Scheduling is done in a LIFO manner, to be executed right away.
    /// Returns the same task, which has been marked as executing on this executor. It can be stored and co_await(ed)
    /// even from another executor.
    template <typename R>
    Task<R> next(Task<R>&& task);

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
};

} // namespace coro

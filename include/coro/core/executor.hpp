#pragma once

#include "handle.hpp"

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
    /// Override should schedule given task in some internal storage to be executed later.
    virtual void schedule(CoroHandle coro) = 0;

    /// Will be called to indicate that given coroutine is suspended and waiting
    /// for external event and will be scheduled in the future.
    virtual void external(CoroHandle coro) = 0;
};

} // namespace coro

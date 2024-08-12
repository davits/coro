#pragma once

#include "task.fwd.hpp"

#include <coroutine>
#include <deque>

namespace coro {

class Executor {
public:
    template <typename R>
    R&& runAndWait(Task<R>& task);

    void runAndWait(Task<void>& task);

    virtual void schedule(std::coroutine_handle<> handle) = 0;

    virtual void execute(std::coroutine_handle<> root) = 0;
};

class StackedExecutor : public Executor {
public:
    inline void schedule(std::coroutine_handle<> handle) override;

    inline void execute(std::coroutine_handle<> root) override;

private:
    std::deque<std::coroutine_handle<>> _tasks;
};

} // namespace coro

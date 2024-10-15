#pragma once

#include "task.fwd.hpp"
#include "stop_token.hpp"

#include <coroutine>
#include <deque>

namespace coro {

class Executor {
public:
    /// Override should schedule given task in some internal storage to be executed later.
    virtual void schedule(std::coroutine_handle<> handle) = 0;

    /// Should resume/start next scheduled task.
    /// Should return true if there are more tasks remaining and false otherwise.
    virtual bool resumeNext() = 0;

public:
    /// Run given task and synchronously wait for its completion.
    /// Return the result of the task is if caller would "await"ed.
    template <typename R>
    R run(Task<R>& task);
};

class StackedExecutor : public Executor {
public:
    inline void schedule(std::coroutine_handle<> handle) override;

    inline bool resumeNext() override;

private:
    void clearFinished();

private:
    std::deque<std::coroutine_handle<>> _tasks;
};

class CancellableStackedExecutor : public StackedExecutor {
public:
    CancellableStackedExecutor(StopToken token)
        : _token(std::move(token)) {}

    StopToken stopToken() {
        return _token;
    }

private:
    StopToken _token;
};

} // namespace coro

#ifndef CORO_EXECUTOR_INL_HPP
#include "executor.inl.hpp"
#endif

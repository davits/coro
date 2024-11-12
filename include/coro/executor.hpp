#pragma once

#include "task.fwd.hpp"
#include "stop_token.hpp"

#include <coroutine>
#include <queue>

namespace coro {

class Executor {
public:
    template <typename T>
    friend class Task;

    friend class PromiseBase;

protected:
    /// Override should schedule given task in some internal storage to be executed later.
    virtual void schedule(std::coroutine_handle<> coro) = 0;

public:
    virtual void timeout(uint32_t timeout, std::coroutine_handle<> coro) = 0;

public:
    /// Should resume/start next scheduled task.
    /// Should return true if there are more tasks remaining and false otherwise.
    virtual bool step() = 0;

public:
    /// Run given task and synchronously wait for its completion.
    /// Return the result of the task is if caller would "await"ed.
    template <typename R>
    R run(Task<R>& task);
};

class SerialExecutor : public Executor {
protected:
    void schedule(std::coroutine_handle<> coro) override;

    void timeout(uint32_t timeout, std::coroutine_handle<> coro) override;

public:
    bool step() override;

private:
    std::queue<std::coroutine_handle<>> _tasks;
};

class CancellableSerialExecutor : public SerialExecutor {
public:
    CancellableSerialExecutor(StopToken token)
        : _token(std::move(token)) {}

    StopToken stopToken() {
        return _token;
    }

private:
    StopToken _token;
};

} // namespace coro

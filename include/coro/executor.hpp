#pragma once

#include "task.fwd.hpp"
#include "stop_token.hpp"

#include <coroutine>
#include <memory>
#include <queue>

namespace coro {

class Executor : public std::enable_shared_from_this<Executor> {
public:
    using Ref = std::shared_ptr<Executor>;

    virtual ~Executor() = default;

protected:
    Executor() = default;

public:
    template <typename T>
    friend class Task;

    friend class PromiseBase;

public:
    /// Override should schedule given task in some internal storage to be executed later.
    virtual void schedule(std::coroutine_handle<> coro) = 0;

    /// Will be called when the given task needs timeout while it is waiting for something to happen.
    virtual void timeout(uint32_t timeout, std::coroutine_handle<> coro) = 0;

    friend class TimeoutAwaitable;
};

class SerialExecutor : public Executor {
protected:
    struct Tag {};

public:
    SerialExecutor(Tag) {};

    using Ref = std::shared_ptr<SerialExecutor>;

    static Ref create() {
        return std::make_shared<SerialExecutor>(Tag {});
    }

public:
    /// Run given task and synchronously wait for its completion.
    /// Return the result of the task is if caller would "await"ed.
    template <typename R>
    R run(Task<R>& task);

protected:
    void schedule(std::coroutine_handle<> coro) override;

    void timeout(uint32_t timeout, std::coroutine_handle<> coro) override;

protected:
    bool step();

private:
    std::queue<std::coroutine_handle<>> _tasks;
};

class CancellableSerialExecutor : public SerialExecutor {
public:
    CancellableSerialExecutor(SerialExecutor::Tag tag, StopToken token)
        : SerialExecutor(tag)
        , _token(std::move(token)) {}

public:
    using Ref = std::shared_ptr<CancellableSerialExecutor>;
    static Ref create(StopToken token) {
        return std::make_shared<CancellableSerialExecutor>(SerialExecutor::Tag {}, std::move(token));
    }

public:
    StopToken stopToken() {
        return _token;
    }

private:
    StopToken _token;
};

} // namespace coro

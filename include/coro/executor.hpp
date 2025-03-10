#pragma once

#include "task.fwd.hpp"
#include "stop_token.hpp"

#include "detail/containers.hpp"

#include <coroutine>
#include <memory>
#include <set>

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
    using handle_t = std::coroutine_handle<>;

    /// Override should schedule given task in some internal storage to be executed later.
    virtual void schedule(handle_t coro) = 0;

    /// Will be called when the given task needs timeout while it is waiting for something to happen.
    virtual void timeout(uint32_t timeout, handle_t coro) = 0;

    /// Will be called to indicate that given coroutine is suspended and waiting
    /// for external event and will be scheduled in the future.
    virtual void external(handle_t coro) = 0;

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

    using Executor::handle_t;

protected:
    void schedule(handle_t coro) override;

    void timeout(uint32_t timeout, handle_t coro) override;

    void external(handle_t coro) override;

private:
    detail::Queue<handle_t> _tasks;
    std::set<handle_t> _external;
    std::condition_variable _cv;
    std::mutex _mutex;
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

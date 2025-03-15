#pragma once

#include "../detail/containers.hpp"
#include "../core/executor.hpp"
#include "../core/stop_token.hpp"
#include "../core/task.hpp"

#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>

namespace coro {

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
    R run(Task<R>& task) {
        task.scheduleOn(shared_from_this());
        while (true) {
            std::unique_lock lock {_mutex};
            if (_tasks.empty()) {
                if (_external.empty()) {
                    break;
                }
                _cv.wait(lock, [this] { return !_tasks.empty(); });
            }
            auto task = _tasks.pop().value();
            // release lock and give a chance to schedule while task is being executed
            lock.unlock();
            task.resume();
        }
        if constexpr (std::move_constructible<R>) {
            return std::move(task).value();
        } else {
            return task.value();
        }
    }

    // template <typename R>
    // std::future<R> schedule(Task<R> task) {
    //     std::promise<R> promise;
    //     auto future = promise.get_future();
    //     auto wrapper = [](Task<R> task, std::promise<R> promise) -> Task<void> {
    //         promise.set_value(co_await std::move(task));
    //     }();
    //     wrapper.scheduleOn(shared_from_this());
    //     _scheduled.push(std::move(wrapper)); // save wrapper to ensure lifetime
    //     return future;
    // }

    using Executor::handle_t;

protected:
    void schedule(handle_t coro) override {
        {
            std::scoped_lock lock {_mutex};
            _tasks.push(coro);
            _external.erase(coro);
        }
        _cv.notify_one();
    }

    void timeout(uint32_t timeout, handle_t coro) override {
        external(coro);
        std::thread([this, timeout, coro]() {
            std::this_thread::sleep_for(std::chrono::milliseconds {timeout});
            schedule(coro);
        }).detach();
    }

    void external(handle_t coro) override {
        std::scoped_lock lock {_mutex};
        _external.insert(coro);
    }

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

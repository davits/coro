#pragma once

#include "../detail/containers.hpp"
#include "../core/executor.hpp"
#include "../core/stop_token.hpp"
#include "../core/task.hpp"

#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>
#include <iostream>

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
        runScheduled();
        if constexpr (std::move_constructible<R>) {
            return std::move(task).value();
        } else {
            return task.value();
        }
    }

    template <typename R>
    std::future<R> future(Task<R> task) {
        std::promise<R> promise;
        auto future = promise.get_future();
        auto wrapper = [](Task<R> task, std::promise<R> promise) -> Task<void> {
            promise.set_value(co_await std::move(task));
        }(std::move(task), std::move(promise));
        wrapper.scheduleOn(shared_from_this());
        wrapper.destroy();
        return future;
    }

    void runScheduled() {
        auto keepAlive = shared_from_this();
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
    }

protected:
    void schedule(CoroHandle coro) override {
        {
            std::scoped_lock lock {_mutex};
            _tasks.push(coro);
            _external.erase(coro);
        }
        _cv.notify_one();
    }

    void external(CoroHandle coro) override {
        std::scoped_lock lock {_mutex};
        _external.insert(coro);
    }

private:
    detail::Queue<CoroHandle> _tasks;
    std::set<CoroHandle> _external;
    std::condition_variable _cv;
    std::mutex _mutex;
};

} // namespace coro

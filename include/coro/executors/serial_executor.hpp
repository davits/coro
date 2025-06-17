#pragma once

#include "../coro.hpp"
#include "../detail/containers.hpp"

#include <condition_variable>
#include <future>
#include <mutex>
#include <map>

namespace coro {

class SerialExecutor : public Executor {
protected:
    struct Tag {};

public:
    SerialExecutor(Tag) {
        _state = std::make_shared<RunState>();
        _runningThread = std::thread([state = _state]() { SerialExecutor::runScheduled(state); });
    };

    ~SerialExecutor() {
        _state->executorDestroyed();
        _runningThread.detach();
    }

    using Ref = std::shared_ptr<SerialExecutor>;

    static Ref create() {
        return std::make_shared<SerialExecutor>(Tag {});
    }

public:
    using Executor::next;
    using Executor::schedule;

    template <typename R>
    std::future<R> future(Task<R>&& task) {
        std::promise<R> promise;
        std::future<R> future = promise.get_future();
        task.handle().promise().enableContextInheritance(false);
        Task<void> wrapper = [](Task<R> task, std::promise<R> promise) -> Task<void> {
            try {
                if constexpr (std::is_same_v<R, void>) {
                    co_await std::move(task);
                    promise.set_value();
                } else {
                    promise.set_value(co_await std::move(task));
                }
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
        }(std::move(task), std::move(promise));
        Executor::schedule(std::move(wrapper));
        return future;
    }

    /// Schedule given task and synchronously wait for its completion.
    /// Returns value returned by task or throws exception if any
    template <typename R>
    R syncWait(Task<R>&& task) {
        std::future<R> f = future(std::move(task));
        return f.get();
    }

protected:
    void schedule(CoroHandle coro) override {
        _state->schedule(std::move(coro));
    }

    void next(CoroHandle coro) override {
        _state->next(std::move(coro));
    }

    void external(CoroHandle coro) override {
        _state->external(std::move(coro));
    }

private:
    struct RunState {
        using Ref = std::shared_ptr<RunState>;
        detail::Deque<CoroHandle> tasks;
        std::map<CoroHandle, Callback::Ref> externals;
        std::condition_variable cv;
        std::mutex mutex;
        std::atomic<bool> finished = false;

        void schedule(CoroHandle&& handle) {
            {
                std::scoped_lock lock {mutex};
                externals.erase(handle);
                if (handle.promise().finished()) [[unlikely]] {
                    return;
                }
                tasks.pushFront(std::move(handle));
            }
            cv.notify_one();
        }

        void next(CoroHandle&& handle) {
            {
                std::scoped_lock lock {mutex};
                externals.erase(handle);
                if (handle.promise().finished()) [[unlikely]] {
                    return;
                }
                tasks.pushBack(std::move(handle));
            }
            cv.notify_one();
        }

        void external(CoroHandle&& handle) {
            auto& promise = handle.promise();
            auto callback = Callback::create([handle]() mutable { handle.promise().skipExecution(); });
            {
                std::scoped_lock lock {mutex};
                externals.emplace(std::move(handle), callback);
            }
            // Add stop callback can fire the callback in case if token was already signaled
            // which in case will try to skip execution and schedule continuation of the handle.
            // So we release mutex before the call to give the schedule a chance.
            // Might be a better idea to replace the mutex with recursive_mutex.
            promise.context.stopToken.addStopCallback(callback);
        }

        void executorDestroyed() {
            {
                std::scoped_lock lock {mutex};
                finished = true;
            }
            cv.notify_one();
        }
    };

    static void runScheduled(RunState::Ref state) {
        // Runs in a separate thread
        while (true) {
            std::unique_lock lock {state->mutex};
            if (state->tasks.empty() && !state->finished) {
                state->cv.wait(lock, [&state] { return !state->tasks.empty() || state->finished; });
            }
            if (state->finished) {
                // finished flag is only set from destructor and at that point both tasks and externals should be empty.
                if (!state->tasks.empty() || !state->externals.empty()) {
                    std::abort();
                }
                break;
            }
            CoroHandle task = state->tasks.popBack().value();
            // release lock and give a chance to schedule while task is being executed
            lock.unlock();
            if (task.promise().skipExecution()) [[unlikely]] {
                continue;
            }
            task.resume();
        }
    }

private:
    RunState::Ref _state;
    std::thread _runningThread;
};

} // namespace coro

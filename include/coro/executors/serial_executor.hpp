#pragma once

#include "../coro.hpp"
#include "../detail/containers.hpp"

#include <condition_variable>
#include <future>
#include <mutex>
#include <map>
#include <thread>

namespace coro {

/**
 * Single threaded serial executor, executing scheduled task on a single thread in a serial manner.
 * This executor API is thread safe and can be used concurrently from different threads.
 * The execution itself takes place on a separate thread, so scheduling does not block the scheduling thread unless
 specifically requested.
 * The lifetime of each executor is prolonged by tasks scheduled on it, regardless of user holding strong reference to
 * it. So effectively executor lives as long as it takes to finish all the tasks scheduled on it.
 * The following is a valid code for this executor:
 * ```
 * {
 *   auto executor = SerialExecutor::create();
     executor->schedule(someTask());
 * }
 * // executor will live on as long as it takes to finish someTask()
 * ```
 */
class SerialExecutor : public Executor {
public:
    using Ref = std::shared_ptr<SerialExecutor>;

    static Ref create() {
        return std::make_shared<SerialExecutor>(Tag {});
    }

public:
    // See comments in the base Executor class
    using Executor::next;
    using Executor::schedule;

    /**
     * Schedule given task and return std::future, which will be satisfied when task is complete,
     * either with result value of the task or with an exception if there was an error.
     */
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

    /**
     * Blocks the calling thread while the executor has work to do, that is while there are queued tasks,
     * a task being executed, or a task waiting for an external event.
     * Returns immediately if the executor is already idle. Note that tasks scheduled from another thread
     * after this call has returned are not accounted for.
     * Must not be called from the executor thread itself, the queue can not drain while the thread
     * draining it is blocked here.
     */
    void drain() {
        if (std::this_thread::get_id() == _runningThread.get_id()) {
            // would block forever, this is the thread which is supposed to drain the queue
            std::abort();
        }
        _state->drain();
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

private:
    struct RunState {
        using Ref = std::shared_ptr<RunState>;
        detail::Deque<CoroHandle> tasks;
        std::map<CoroHandle, Callback::Ref> externals;
        std::condition_variable cv;
        std::condition_variable idleCV;
        std::mutex mutex;
        std::atomic<bool> finished = false;
        bool running = false;

        void schedule(CoroHandle&& handle) {
            {
                std::scoped_lock lock {mutex};
                externals.erase(handle);
                if (handle.promise().finished()) [[unlikely]] {
                    // nothing to execute, erasing it from the externals might have made the executor idle
                    notifyIfIdle();
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
                    // nothing to execute, erasing it from the externals might have made the executor idle
                    notifyIfIdle();
                    return;
                }
                tasks.pushBack(std::move(handle));
            }
            cv.notify_one();
        }

        void external(CoroHandle&& handle) {
            auto& promise = handle.promise();
            auto callback = Callback::create(
                [handle = handle]() mutable { handle.promise().executor->schedule(std::move(handle)); });
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

        void drain() {
            std::unique_lock lock {mutex};
            idleCV.wait(lock, [this] { return idle() || finished; });
        }

        /// Both must be called while the mutex is held.
        bool idle() const {
            return tasks.empty() && externals.empty() && !running;
        }

        void notifyIfIdle() {
            if (idle()) {
                idleCV.notify_all();
            }
        }

        void executorDestroyed() {
            {
                std::scoped_lock lock {mutex};
                finished = true;
            }
            cv.notify_one();
            idleCV.notify_all();
        }
    };

    static void runScheduled(RunState::Ref state) {
        // Runs in a separate thread
        while (true) {
            std::unique_lock lock {state->mutex};
            // the task popped by the previous iteration, if any, is done executing by now
            state->running = false;
            if (state->tasks.empty() && !state->finished) {
                state->notifyIfIdle();
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
            state->running = true;
            // release lock and give a chance to schedule while task is being executed
            lock.unlock();
            // this can happen during cancellation, when coroutine waiting for external event
            // is cancelled and the external event is fired at the same time.
            if (task.promise().finished()) [[unlikely]] {
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

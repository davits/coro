#pragma once

#include "js_promise.hpp"
#include "sleep.hpp"

#include "../core/executor.hpp"
#include "../core/task.hpp"
#include "../detail/containers.hpp"

#include <map>

#include <emscripten/bind.h>
#include <emscripten/val.h>

namespace coro {

namespace detail {
extern "C" emscripten::EM_VAL _coro_lib_val_from_cpp_exception();
} // namespace detail

/**
 * Single threaded serial executor designed specifically for working in emscripten environment.
 * This executor API is not a thread safe and should not be used from concurrent worker threads.
 * The lifetime of each executor is prolonged by tasks scheduled on it, regardless of user holding strong reference to
 * it. So effectively executor lives as long as it takes to finish all the tasks scheduled on it.
 * The following is a valid code for this executor:
 * ```
 * {
 *   auto executor = SerialWebExecutor::create();
     executor->schedule(someTask());
 * }
 * // executor will live on as long as it takes to finish someTask()
 * ```
 */
class SerialWebExecutor : public coro::Executor {
public:
    using Ref = std::shared_ptr<SerialWebExecutor>;

    /**
     * @brief Create new SerialWebExecutor.
     * @param maxBlockingTime Specify maximum blocking time of the main thread in milliseconds. The executor will try to
     * not exceed maxBlockingTime by periodically checking elapsed time since the last wakeup and defer the execution of
     * the consequent operations to the next event loop sweep when the main thread has been blocked for more time then
     * allowed.
     * @param checkNthOperation Specify how often executor should check for the elapsed time. This is the count of
     * executed operations/coroutines after which the elapsed time will be checked. This argument exists because it is
     * quite slow to get time in the wasm environment and it is not feasable to check time after each operation.
     */
    static Ref create(uint32_t maxBlockingTime = 1000 / 30, uint32_t checkNthOperation = 10) {
        return std::make_shared<SerialWebExecutor>(Tag {}, maxBlockingTime, checkNthOperation);
    }

public:
    // See the comments in the Executor base class.
    using Executor::next;
    using Executor::schedule;

    /**
     * Enqueues task on the executor and returns JS promise wrapped into the emscripten::val.
     * The promise will be fullfilled when the task is finished completely, or rejected if there has been an error
     * during the execution.
     */
    template <typename R>
    emscripten::val promise(Task<R>&& task) {
        task.handle().promise().enableContextInheritance(false);
        auto promise = detail::JSPromise::create();
        auto jsPromise = promise.promise();
        auto wrapper = [](Task<R> task, detail::JSPromise promise) -> Task<void> {
            try {
                if constexpr (std::is_same_v<R, void>) {
                    co_await std::move(task);
                    promise.resolve(emscripten::val::undefined());
                } else {
                    emscripten::val result {co_await std::move(task)};
                    promise.resolve(std::move(result));
                }
            } catch (...) {
                auto error = emscripten::val::take_ownership(detail::_coro_lib_val_from_cpp_exception());
                promise.reject(std::move(error));
            }
        }(std::move(task), std::move(promise));
        Executor::schedule(std::move(wrapper));
        co_return jsPromise;
    }

protected:
    void schedule(CoroHandle handle) override {
        _state->schedule(std::move(handle));
    }

    void next(CoroHandle handle) override {
        _state->next(std::move(handle));
    }

    void external(CoroHandle handle) override {
        _state->external(std::move(handle));
    }

protected:
    struct Tag {};

public:
    SerialWebExecutor(Tag, uint32_t maxBlockingTime, uint32_t checkNthOperation) {
        _state = std::make_shared<RunState>();
        _state->maxBlockingTime = maxBlockingTime;
        _state->checkNthOperation = checkNthOperation;
        _state->runner = runScheduled(_state);
    }

    SerialWebExecutor(const SerialWebExecutor&) = delete;

    ~SerialWebExecutor() {
        _state->executorDestroyed();
    }

private:
    struct RunState {
        using Ref = std::shared_ptr<RunState>;
        coro::detail::Deque<CoroHandle> tasks;
        std::map<CoroHandle, Callback::Ref> externals;
        detail::JSPromise coroScheduled = detail::JSPromise::null();
        emscripten::val runner;
        uint32_t maxBlockingTime = 1000 / 30;
        uint32_t checkNthOperation = std::numeric_limits<uint32_t>::max();
        bool finished = false;

        void schedule(CoroHandle&& handle) {
            externals.erase(handle);
            if (handle.promise().finished()) [[unlikely]] {
                return;
            }
            tasks.pushFront(std::move(handle));
            if (coroScheduled) [[unlikely]] {
                coroScheduled.resolve(emscripten::val {});
            }
        }

        void next(CoroHandle&& handle) {
            externals.erase(handle);
            if (handle.promise().finished()) [[unlikely]] {
                return;
            }
            tasks.pushBack(std::move(handle));
            if (coroScheduled) [[unlikely]] {
                coroScheduled.resolve(emscripten::val {});
            }
        }

        void external(CoroHandle&& handle) {
            auto& promise = handle.promise();
            auto callback = Callback::create([handle]() mutable { handle.promise().executor->schedule(handle); });
            externals.emplace(std::move(handle), callback);
            promise.context.stopToken.addStopCallback(callback);
        }

        void executorDestroyed() {
            finished = true;
            if (coroScheduled) {
                coroScheduled.resolve(emscripten::val {});
            }
        }
    };

    static emscripten::val runScheduled(RunState::Ref state) {
        auto start = now();
        uint32_t opCount = 0;
        while (true) {
            auto next = state->tasks.popBack().value_or(nullptr);
            if (!next) {
                state->coroScheduled = detail::JSPromise::create();
                co_await state->coroScheduled.promise(); // wait untill something is scheduled
                if (state->finished) [[unlikely]] {
                    break;
                }
                continue;
            }
            if (next.promise().finished()) [[unlikely]] {
                continue;
            }
            ++opCount;
            next.resume();
            // reset to force the last task keeping executor alive to be destructed and hence
            // cause destruction of the executor which in turn will set finished flag to true.
            next.reset();
            if (state->finished) [[unlikely]] {
                break;
            }
            if (opCount >= state->checkNthOperation) {
                opCount = 0;
                auto passed = passedTime(start);
                if (passed > state->maxBlockingTime) {
                    co_await sleep(0); // defer the rest to the next JS event loop cycle
                    start = now();
                }
            }
        }
        co_return emscripten::val::undefined();
    }

    static std::chrono::high_resolution_clock::time_point now() {
        return std::chrono::high_resolution_clock::now();
    }

    static uint32_t passedTime(std::chrono::high_resolution_clock::time_point start) {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    }

private:
    RunState::Ref _state;
};

template <typename R>
emscripten::val taskPromise(coro::Task<R> task) {
    auto executor = coro::SerialWebExecutor::create();
    // task and executor will keep each other alive till the end of the task
    // so no need to create yet another coroutine here.
    // Beside there is an error propogation bug in emscripten
    return executor->promise(std::move(task));
}

} // namespace coro

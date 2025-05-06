#pragma once

#include "js_promise.hpp"
#include "sleep.hpp"

#include "../core/executor.hpp"
#include "../core/task.hpp"
#include "../detail/containers.hpp"

#include <set>

#include <emscripten/bind.h>
#include <emscripten/val.h>

namespace coro {

namespace detail {
extern "C" emscripten::EM_VAL _coro_lib_val_from_cpp_exception();
} // namespace detail

class SerialWebExecutor : public coro::Executor {
protected:
    struct Tag {};

public:
    SerialWebExecutor(Tag, uint32_t maxBlockingTime) {
        _state = std::make_shared<RunState>();
        _state->maxBlockingTime = maxBlockingTime;
        _state->runner = runScheduled(_state);
    }

    using Ref = std::shared_ptr<SerialWebExecutor>;
    static Ref create(uint32_t maxBlockingTime = 1000 / 30) {
        return std::make_shared<SerialWebExecutor>(Tag {}, maxBlockingTime);
    }

    SerialWebExecutor(const SerialWebExecutor&) = delete;

    ~SerialWebExecutor() {
        _state->executorDestroyed();
    }

public:
    template <typename R>
    emscripten::val promise(Task<R>&& task) {
        auto taskContext = task.handle().promise().context;
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
        wrapper.handle().promise().context = taskContext;
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

private:
    struct RunState {
        using Ref = std::shared_ptr<RunState>;
        coro::detail::Deque<CoroHandle> tasks;
        std::set<CoroHandle> externals;
        detail::JSPromise coroScheduled = detail::JSPromise::null();
        emscripten::val runner;
        uint32_t maxBlockingTime = 1000 / 30;
        bool finished = false;

        void schedule(CoroHandle&& handle) {
            externals.erase(handle);
            tasks.pushFront(std::move(handle));
            if (coroScheduled) {
                coroScheduled.resolve(emscripten::val {});
            }
        }

        void next(CoroHandle&& handle) {
            externals.erase(handle);
            tasks.pushBack(std::move(handle));
            if (coroScheduled) {
                coroScheduled.resolve(emscripten::val {});
            }
        }

        void external(CoroHandle&& handle) {
            externals.insert(std::move(handle));
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
        while (true) {
            auto next = state->tasks.popBack().value_or(nullptr);
            if (next) {
                next.resume();
                // reset to force the last task keeping executor alive to be destructed and hence
                // cause destruction of the executor which in turn will set finished flag to true.
                next.reset();
                if (state->finished) break;
                auto passed = passedTime(start);
                if (passed > state->maxBlockingTime) {
                    co_await sleep(0); // defer the rest to the next JS event loop cycle
                    start = now();
                }
            } else {
                state->coroScheduled = detail::JSPromise::create();
                co_await state->coroScheduled.promise(); // wait untill something is scheduled
                if (state->finished) break;
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

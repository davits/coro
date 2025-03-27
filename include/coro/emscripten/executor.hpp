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

class SerialWebExecutor : public coro::Executor {
protected:
    struct Tag {};

public:
    SerialWebExecutor(Tag, uint32_t maxBlockingTime)
        : _maxBlockingTime(maxBlockingTime) {}

    using Ref = std::shared_ptr<SerialWebExecutor>;
    static Ref create(uint32_t maxBlockingTime = 1000 / 30) {
        return std::make_shared<SerialWebExecutor>(Tag {}, maxBlockingTime);
    }

    SerialWebExecutor(const SerialWebExecutor&) = delete;

private:
    bool done() const {
        return _tasks.empty() && _externals.empty();
    }

public:
    emscripten::val run() {
        auto start = now();
        while (true) {
            auto next = _tasks.pop().value_or(nullptr);
            if (next) {
                next.resume();
                auto passed = passedTime(start);
                if (passed > _maxBlockingTime) {
                    co_await sleep(0); // defer the rest to the next JS event loop cycle
                    start = now();
                }
            } else if (!_externals.empty()) {
                _coroScheduled = detail::JSPromise::create();
                co_await _coroScheduled.promise(); // wait untill something is scheduled
            } else {
                co_return emscripten::val {};
            }
        }
    }

protected:
    void schedule(CoroHandle handle) override {
        _externals.erase(handle);
        _tasks.push(handle);
        if (_coroScheduled) {
            _coroScheduled.resolve(emscripten::val {});
        }
    }

    void external(CoroHandle handle) override {
        _externals.insert(handle);
    }

private:
    std::chrono::high_resolution_clock::time_point now() {
        return std::chrono::high_resolution_clock::now();
    }

    uint32_t passedTime(std::chrono::high_resolution_clock::time_point start) {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    }

private:
    coro::detail::Stack<CoroHandle> _tasks;
    std::set<CoroHandle> _externals;
    detail::JSPromise _coroScheduled = detail::JSPromise::null();
    uint32_t _maxBlockingTime;
};

template <typename R>
emscripten::val taskPromise(coro::Task<R> task) {
    auto executor = coro::SerialWebExecutor::create();
    task.scheduleOn(executor);
    co_await executor->run();
    if constexpr (std::is_same_v<R, void>) {
        co_return emscripten::val {};
    } else {
        co_return task.value();
    }
}

} // namespace coro

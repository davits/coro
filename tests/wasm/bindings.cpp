#include <emscripten/bind.h>
#include <emscripten/emscripten.h>

// for testing purposes
#define private protected
#include <coro/coro.hpp>
#include <coro/emscripten/abort.hpp>
#include <coro/emscripten/executor.hpp>
#include <coro/emscripten/bridge.hpp>

template <size_t N>
coro::Task<int> sleepy(bool thr) {
    co_return co_await sleepy<N - 1>(thr);
}

template <>
coro::Task<int> sleepy<0>(bool thr) {
    co_await coro::sleep(200);
    if (thr) {
        throw std::runtime_error {"test error"};
    }
    co_return 42;
}

class TestExecutor : public coro::SerialWebExecutor {
protected:
    using Tag = coro::SerialWebExecutor::Tag;

public:
    TestExecutor(Tag)
        : coro::SerialWebExecutor(Tag {}, 30, 10) {
        ++executorCount;
    }

    ~TestExecutor() {
        --executorCount;
    }

public:
    using Ref = std::shared_ptr<TestExecutor>;

    static Ref create() {
        auto executor = std::make_shared<TestExecutor>(Tag {});
        weakRunState = executor->_state;
        return executor;
    }

public:
    static int32_t executorCount;
    static std::weak_ptr<coro::SerialWebExecutor::RunState> weakRunState;
};

int32_t TestExecutor::executorCount = 0;
std::weak_ptr<coro::SerialWebExecutor::RunState> TestExecutor::weakRunState;

struct CancellableTask {
    emscripten::val taskPromise;
    coro::StopSource stop;

    CancellableTask(emscripten::val p, coro::StopSource s)
        : taskPromise(std::move(p))
        , stop(std::move(s)) {}

    void cancel() {
        stop.requestStop();
    }

    emscripten::val promise() const {
        return taskPromise;
    }
};

std::shared_ptr<CancellableTask> launchCancellableTask() {
    auto executor = coro::SerialWebExecutor::create();
    auto task = sleepy<5>(false);
    coro::StopSource stop;
    task.setStopToken(stop.token());
    auto promise = executor->promise(std::move(task));

    return std::make_shared<CancellableTask>(std::move(promise), std::move(stop));
}

emscripten::val testAbortSignal(emscripten::val signal) {
    co_await coro::sleep(100);
    if (signal["aborted"].as<bool>()) {
        co_return emscripten::val {42};
    }
    co_return emscripten::val {11};
}

coro::Task<int> testAbortController() {
    coro::AbortController controller;
    auto task = testAbortSignal(controller.signal());
    co_await coro::sleep(50);
    controller.abort();
    auto result = co_await task;
    co_return result.as<int>();
}

coro::Task<int> testAbortControllerTimeout() {
    coro::AbortController controller;
    auto task = testAbortSignal(controller.signal(50));
    auto result = co_await task;
    co_return result.as<int>();
}

// Create mock promise which will be resolved after 100ms
EM_JS(emscripten::EM_VAL, customPromiseImpl, (), {
    const promise = new Promise((resolve) => {
        setTimeout(() => {
            resolve();
        }, 100);
    });
    return Emval.toHandle(promise);
});

emscripten::val customPromise() {
    return emscripten::val::take_ownership(customPromiseImpl());
}

template <size_t N>
coro::Task<void> someTask(emscripten::val promise) {
    co_await someTask<N - 1>(promise);
}

bool thrown = false;
template <>
coro::Task<void> someTask<0>(emscripten::val promise) {
    try {
        co_await promise;
    } catch (const coro::StopError&) {
        thrown = true;
    }
}

coro::Task<void> testCustomPromiseCancellation() {
    emscripten::val promise = customPromise();
    coro::StopSource stop;

    auto executor = coro::SerialWebExecutor::create();
    auto task = executor->promise(someTask<5>(promise).setStopToken(stop.token()));

    co_await coro::sleep(50);
    stop.requestStop();

    bool except = false;
    try {
        co_await task;
    } catch (...) {
        except = true;
    }
    if (!thrown || !except) {
        throw std::runtime_error {"Exception was not thrown"};
    }
    executor.reset();
    // The promise was disconnected from coroutines during cancellation via stop token
    // awaiting for it here to make sure that promise resolve does not try to resolve now
    // deleted coroutine frame.
    co_await promise;
}

EMSCRIPTEN_BINDINGS(Test) {
    emscripten::function("sleepyTask", +[]() { return coro::taskPromise(sleepy<5>(false)); });
    emscripten::function("failingTask", +[]() { return coro::taskPromise(sleepy<5>(true)); });
    emscripten::function(
        "lifetimeTask", +[]() {
            auto executor = TestExecutor::create();
            return executor->promise(sleepy<5>(false));
        });
    emscripten::function("executorCount", +[]() { return TestExecutor::executorCount; });
    emscripten::function("runStateDestroyed", +[]() { return TestExecutor::weakRunState.lock() == nullptr; });

    emscripten::class_<CancellableTask>("CancellableTask")
        .smart_ptr<std::shared_ptr<CancellableTask>>("CancellableTask")
        .function("cancel", &CancellableTask::cancel)
        .function("promise", &CancellableTask::promise);

    emscripten::function("launchCancellableTask", &launchCancellableTask);
    emscripten::function("testAbortController", +[]() { return coro::taskPromise(testAbortController()); });
    emscripten::function(
        "testAbortControllerTimeout", +[]() { return coro::taskPromise(testAbortControllerTimeout()); });
    emscripten::function(
        "testCustomPromiseCancellation", +[]() { return coro::taskPromise(testCustomPromiseCancellation()); });
}

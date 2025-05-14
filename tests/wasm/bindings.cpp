#include <emscripten/bind.h>

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
        : coro::SerialWebExecutor(Tag {}, 30) {
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
}

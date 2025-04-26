#include <emscripten/bind.h>

// for testing purposes
#define private protected
#include <coro/coro.hpp>
#include <coro/emscripten/executor.hpp>
#include <coro/emscripten/bridge.hpp>

template <size_t N>
coro::Task<int> sleepy(bool thr) {
    co_return co_await sleepy<N - 1>(thr);
}

template <>
coro::Task<int> sleepy<0>(bool thr) {
    co_await coro::sleep(100);
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

EMSCRIPTEN_BINDINGS(Test) {
    emscripten::function("sleepyTask", +[] { return coro::taskPromise(sleepy<5>(false)); });
    emscripten::function("failingTask", +[]() { return coro::taskPromise(sleepy<5>(true)); });
    emscripten::function(
        "lifetimeTask", +[]() {
            auto executor = TestExecutor::create();
            return executor->promise(sleepy<5>(false));
        });
    emscripten::function("executorCount", +[]() { return TestExecutor::executorCount; });
    emscripten::function("runStateDestroyed", +[]() { return TestExecutor::weakRunState.lock() == nullptr; });
}

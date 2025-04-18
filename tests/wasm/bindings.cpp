#include <emscripten/bind.h>

#include <coro/coro.hpp>
#include <coro/emscripten/executor.hpp>
#include <coro/emscripten/bridge.hpp>

coro::Task<int> sleepAndReturn() {
    co_await coro::sleep(100);
    co_return 42;
}

EMSCRIPTEN_BINDINGS(Test) {
    emscripten::function("sleepAndReturn", +[] { return coro::taskPromise(sleepAndReturn()); });
}

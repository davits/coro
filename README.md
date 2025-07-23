# Coro

A modern C++20 coroutine library with Emscripten coroutine integration.

## Overview

coro is a C++20 coroutine library that provides implementation of underlying gears and cranks necessary for C++ coroutines to work. It offers coroutine return type, couple of asynchronous synchronization primitives and integration with emscripten::val promises.

## Features

- Generic return type for coroutine
- Executors for posix and emscripten environments
- Async synchronization primitives
- Cancellation mechanism for tasks
- Seamless integration with emscripten::val coroutines
- Automatic lifetime management

### Coroutine return type

- `coro::Task<T>` generic task type for coroutine results
- Type-safe value and error handling
- Lazy coroutine launch upon scheduling
- Ability to co_await tasks from another executor

### Executors

- Abstract executor interface working with underlying coroutine handles
- Single threaded executor implementation for posix and emscripten
- Support for custom executor implementations

### Synchronization

- Async mutex `coro::Mutex`
- Async latch `coro::Latch`
- Async pipe `coro::Pipe<T>`

### Cancellation

- Automatic cancellation in between separate coroutines via `coro::StopSource` and `coro::StopToken`
- Manual cancellation from long running coroutine frames via `coro::StopToken`

### Emscripten integration

- Custom awaiter for emscripten::val coroutines or JS promises wrapped in emscripten::val
- Seamless integration via co_await(ing) to emscripten::val from coro::Task context

### Lifetime

- Coroutine frames and executors are bound via cyclic strong dependency keeping both alive while at least one task/coroutine is scheduled on the executor

## Requirements

- C++20 compatible compiler
- CMake 3.21 or newer
- Emscripten 4.0.10 or newer (if using anything from coro/emscripten)

## Usage Examples

### Simple usage

```cpp
#include <coro/coro.hpp>
#include <coro/sleep.hpp>
#include <coro/executors/serial_executor.hpp>

coro::Task<int> sleepy() {
    co_await coro::sleep(100);
    co_return 42;
}

coro::Task<int> work() {
    co_return co_await sleepy();
}

// Simple usage demonstrating scheduling
TEST(coro, simple) {
    auto executor = coro::SerialExecutor::create();
    // schedule and forget
    executor->schedule(work());
    // schedule and retrieve associated future
    auto future = executor->future(work());
    // schedule and block untill the task is completed.
    int result = executor->syncWait(work());
    EXPECT_EQ(result, 42);
    // future.get() will block till the task is completed
    EXPECT_EQ(future.get(), 42);
    // note that all tasks in this example will complete at approximately the same time
    // since the sleep in this example is asynchronous and is not blocking overall task execution flow.
}
```

### Error Handling

```cpp
coro::Task<void> throwingTask() {
    throw std::runtime_error{"Some error"};
    co_return;
}

coro::Task<int> work() {
    try {
        // co_await(ing) to throwing task will throw that exception
        co_await throwingTask();
        return 42;
    } catch(...) {
        return -1;
    }
}

TEST(coro, error) {
    auto executor = coro::SerialExecutor::create();
    // schedule and forget will not throw, since there is no result/error consumer
    executor->schedule(throwingTask());
    auto future = executor->future(throwingTask());
    // Note that error will be thrown at the consume point rather then when scheduling with future() function
    EXPECT_THROW(future.get(), std::runtime_error);
    // In case of syncWait() it is a consume point, so the exception will be thrown.
    EXPECT_THROW(executor->syncWait(throwingTask()), std::runtime_error);

    auto result = executor->syncWait(work());
    EXPECT_EQ(result, -1);
}
```

### Synchronization

```cpp
#include <coro/sync/mutex.hpp>

coro::Mutex mutex;

coro::Task<void> mutexExample() {
    {
        auto lock = co_await mutex;
        // Critical section
    }
}
```

### Cancellation

```cpp
coro::Task<void> longRunning() {
    // Get stop token set for current task
    auto stopToken = co_await coro::currentStopToken;
    for (int i = 0; i < 1000'000; ++i) {
        // do some work
        stopToken.throwIfStopped();
    }
}

TEST(coro, cancellation) {
    auto executor = coro::SerialExecutor::create();
    coro::StopSource stopSource;
    auto task = work(); // work function from the first example
    task.setStopToken(stopSource.token());
    // Note: there is no stop checks in the work() task, nevertheless it will be stopped by executor upon reaching
    // end of the current coroutine frame execution.
    auto future1 = executor->future(std::move(task));
    // The long running task will be exited in the middle of coroutine frame execution thanks to the manual checks
    auto future2 = executor->future(longRunning().setStopToken(stop.token()));
    stopSource.requestStop();
    EXPECT_THROW(future1.get(), coro::StopError);
    EXPECT_THROW(future2.get(), coro::StopError);
}
```

### Emscripten Integration

```cpp
#include <coro/coro.hpp>
#include <coro/emscripten/bridge.hpp>
#include <coro/emscripten/executor.hpp>

// emscripten coroutine
emscripten::val fetchJson(std::string url) {
    using namespace emscripten;
    static val fetch = val::global("fetch");
    val response = co_await fetch(url);
    co_return co_await response.call<val>("json");
}

coro::Task<std::string> processJson() {
    // seamless co_await for emscripten::val coroutine, or JS promise wrapped into emscripten::val
    emscripten::val jsonVal = co_await fetchJson("https://url");
    auto json = jsonVal.as<std::string>();
    //process and modify json ...
    co_await coro::sleep(1000); // sleep for 1 second to emulate work
    co_return json;
}

emscripten::val doWork() {
    auto executor = coro::SerialWebExecutor::create();
    // Note that we are scheduling task on the local executor which goes out of scope when this function returns,
    // but no worries, it will be automatically kept alive while the task is being executed.
    return executor->promise(processJson());
}

EMSCRIPTEN_BINDINGS(Sample) {
    // After binding function in the JS, we get function which return JS promise object which can be await(ed)
    emscripten::function("doWork", &doWork);
}
```

```js
// Sample usage on the JavaScript side
wasmModule
  .doWork()
  .then((value) =>
    console.log(`Finished fetch and process on C++ side with result: ${value}`)
  );
// or
const value = await wasmModule.doWork();
console.log(`Finished fetch and process on C++ side with result: ${value}`);
```

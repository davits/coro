#pragma once

#include "awaitable.hpp"
#include "executor.hpp"
#include "stop_token.hpp"

namespace coro {

struct UserData {
    using Ref = std::shared_ptr<UserData>;

    virtual ~UserData() = 0;
};

struct TaskContext {
    Executor::Ref executor = nullptr;
    StopToken stopToken = nullptr;
    UserData::Ref userData = nullptr;
};

struct StopTokenAwaitable {};
inline StopTokenAwaitable currentStopToken;

template <>
struct await_ready_trait<StopTokenAwaitable> {
    static decltype(auto) await_transform(const TaskContext& context, StopTokenAwaitable) {
        return ReadyAwaitable<const StopToken&> {context.stopToken};
    }
};

/// Helper to easily access to the current executor within the coroutine.
/// Executor::Ref executor = co_await coro::currentExecutor;
struct ExecutorAwaitable {};
inline ExecutorAwaitable currentExecutor;

template <>
struct await_ready_trait<ExecutorAwaitable> {
    static decltype(auto) await_transform(const TaskContext& context, ExecutorAwaitable) {
        return ReadyAwaitable<const Executor::Ref&> {context.executor};
    }
};

} // namespace coro

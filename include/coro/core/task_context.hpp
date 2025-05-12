#pragma once

#include "awaitable.hpp"
#include "executor.hpp"
#include "stop.hpp"
#include "traits.hpp"

namespace coro {

struct UserData {
    using Ref = std::shared_ptr<UserData>;

    virtual ~UserData() = 0;
};

inline UserData::~UserData() {}

struct TaskContext {
    StopToken stopToken = nullptr;
    UserData::Ref userData;
};

/// Helper to easily access to the current executor within the coroutine.
/// auto executor = co_await coro::currentExecutor;
struct ExecutorAwaitable {};
inline ExecutorAwaitable currentExecutor;

template <>
struct await_ready_trait<ExecutorAwaitable> {
    static decltype(auto) await_transform(const ExecutorRef& executor, const TaskContext&, ExecutorAwaitable) {
        return ReadyAwaitable<const Executor::Ref&> {executor};
    }
};

/// Helper to easily access to the stop token of the current task
/// auto token = co_await coro::currentStopToken;
struct StopTokenAwaitable {};
inline StopTokenAwaitable currentStopToken;

template <>
struct await_ready_trait<StopTokenAwaitable> {
    static decltype(auto) await_transform(const ExecutorRef&, const TaskContext& context, StopTokenAwaitable) {
        return ReadyAwaitable<const StopToken&> {context.stopToken};
    }
};

/// Helper to easily access to the user data of the current task
/// auto token = co_await coro::currentUserData;
struct UserDataAwaitable {};
inline UserDataAwaitable currentUserData;

template <>
struct await_ready_trait<UserDataAwaitable> {
    static decltype(auto) await_transform(const ExecutorRef&, const TaskContext& context, UserDataAwaitable) {
        return ReadyAwaitable<const UserData::Ref&> {context.userData};
    }
};

} // namespace coro

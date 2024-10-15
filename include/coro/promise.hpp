#pragma once

#include "task.fwd.hpp"
#include "stop_token.hpp"
#include "awaitable.hpp"
#include "expected.hpp"

#include <coroutine>
#include <exception>

namespace coro {

class Executor;

template <typename T>
struct await_ready_trait;

class PromiseBase {
public:
    Executor* executor = nullptr;
    StopToken _token;

public:
    PromiseBase() = default;

    std::suspend_always initial_suspend() {
        return {};
    }
    std::suspend_always final_suspend() noexcept {
        return {};
    }

    template <typename U>
    Awaitable<Task<U>> await_transform(Task<U>&& task);

    template <typename T>
    ReadyAwaitable<std::remove_cvref_t<T>> await_transform(T&& obj) {
        using RawT = std::remove_cvref_t<T>;
        return await_ready_trait<RawT>::await_transform(executor, std::forward<T>(obj));
    }

    void set_stop_token(StopToken token) {
        _token = std::move(token);
    }
};

template <typename R>
class Promise : public PromiseBase {
public:
    using handle_t = std::coroutine_handle<Promise>;
    using return_t = R;

private:
    expected<R> _result;

public:
    Task<R> get_return_object();

    void unhandled_exception() {
        _result.emplace_error(std::current_exception());
    }

    void return_value(R r) {
        _result.emplace_value(std::move(r));
    }

    const R& value() const& {
        return _result.value();
    }

    R&& value() && {
        return std::move(_result).value();
    }
};

template <>
class Promise<void> : public PromiseBase {
public:
    using handle_t = std::coroutine_handle<Promise>;
    using return_t = void;

private:
    expected<void> _result;

public:
    inline Task<void> get_return_object();

    void unhandled_exception() {
        _result.emplace_error(std::current_exception());
    }

    void return_void() {
        _result.emplace_value();
    }

    void value() const {
        return _result.value();
    }
};

} // namespace coro

#ifndef CORO_PROMISE_INL_HPP
#include "promise.inl.hpp"
#endif

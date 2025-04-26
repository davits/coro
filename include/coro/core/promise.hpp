#pragma once

#include "task.fwd.hpp"
#include "awaitable.hpp"
#include "expected.hpp"
#include "executor.hpp"
#include "promise_base.hpp"

#include "traits.hpp"

#include <coroutine>
#include <exception>

namespace coro {

template <typename R>
class Promise : public PromiseBase {
public:
    using handle_t = std::coroutine_handle<Promise>;
    using return_t = R;

public:
    Task<R> get_return_object();

    void unhandled_exception() {
        _storage.emplace_error(std::current_exception());
    }

    void return_value(R r) {
        _storage.emplace_value(std::move(r));
    }

public:
    decltype(auto) value() const& {
        return _storage.value();
    }

    decltype(auto) value() && {
        return std::move(_storage).value();
    }

private:
    expected<R> _storage;
};

template <>
class Promise<void> : public PromiseBase {
public:
    using handle_t = std::coroutine_handle<Promise>;
    using return_t = void;

public:
    Task<void> get_return_object();

    void unhandled_exception() {
        _storage.emplace_error(std::current_exception());
    }

    void return_void() {
        _storage.emplace_value();
    }

public:
    void value() {
        return _storage.value();
    }

private:
    expected<void> _storage;
};

} // namespace coro

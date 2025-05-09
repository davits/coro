#pragma once

#include "task.fwd.hpp"
#include "awaitable.hpp"
#include "executor.hpp"
#include "promise_base.hpp"

#include "traits.hpp"

#include <coroutine>
#include <exception>

namespace coro {

class UninitializedValue : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

template <typename R>
class Promise : public PromiseBase {
public:
    using handle_t = std::coroutine_handle<Promise>;
    using return_t = R;

public:
    Task<R> get_return_object();

    void return_value(R r) {
        emplace_value(std::move(r));
    }

public:
    void emplace_value(R&& value) {
        _value = std::move(value);
        _valueState = PromiseBase::ValueState::Value;
    }

    const R& value() const& {
        switch (_valueState) {
        case PromiseBase::ValueState::Uninitialized:
            throw UninitializedValue("Value is not initialized.");
        case PromiseBase::ValueState::Exception:
            std::rethrow_exception(_exception);
        case PromiseBase::ValueState::Value:
            return _value;
        }
    }

    R&& value() && {
        switch (_valueState) {
        case PromiseBase::ValueState::Uninitialized:
            throw UninitializedValue("Value is not initialized.");
        case PromiseBase::ValueState::Exception:
            std::rethrow_exception(_exception);
        case PromiseBase::ValueState::Value:
            return std::move(_value);
        }
    }

private:
    R _value;
};

template <>
class Promise<void> : public PromiseBase {
public:
    using handle_t = std::coroutine_handle<Promise>;
    using return_t = void;

public:
    Task<void> get_return_object();

    void return_void() {
        emplace_value();
    }

public:
    void emplace_value() {
        _valueState = PromiseBase::ValueState::Value;
    }

    void value() {
        switch (_valueState) {
        case PromiseBase::ValueState::Uninitialized:
            throw UninitializedValue("Value is not initialized.");
        case PromiseBase::ValueState::Exception:
            std::rethrow_exception(_exception);
        case PromiseBase::ValueState::Value:
            return;
        }
    }
};

} // namespace coro

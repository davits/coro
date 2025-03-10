#pragma once

#include <exception>
#include <stdexcept>
#include <utility>

namespace coro {

class uninitialized_state : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

template <typename V>
class expected {
public:
    enum class state {
        uninitialized,
        error,
        value,
    };

    expected() = default;

    expected(const V& value)
        : _value(value)
        , _state(state::value) {}

    expected(V&& value)
        : _value(std::move(value))
        , _state(state::value) {}

    expected(std::exception_ptr error)
        : _error(std::move(error))
        , _state(state::error) {}

    void emplace_value(V v) {
        _value = std::move(v);
        _state = state::value;
    }

    void emplace_error(std::exception_ptr e) {
        _error = e;
        _state = state::error;
    }

public:
    bool has_value() const {
        return _state == state::value;
    }

    const V& value() const& {
        switch (_state) {
        case state::uninitialized:
            throw uninitialized_state("Value is not initialized.");
        case state::error:
            std::rethrow_exception(_error);
        case state::value:
            return _value;
        }
    }

    V&& value() && {
        switch (_state) {
        case state::uninitialized:
            throw uninitialized_state("Value is not initialized.");
        case state::error: {
            std::rethrow_exception(_error);
        }
        case state::value:
            _state = state::uninitialized;
            return std::move(_value);
        }
    }

private:
    V _value = V {};
    std::exception_ptr _error = nullptr;
    state _state = state::uninitialized;
};

template <>
class expected<void> {
public:
    enum class state {
        uninitialized,
        error,
        value,
    };

    void emplace_value() {
        _state = state::value;
    }

    void emplace_error(std::exception_ptr e) {
        _error = e;
        _state = state::error;
    }

public:
    bool has_value() const {
        return _state == state::value;
    }

    void value() const {
        switch (_state) {
        case state::uninitialized:
            throw uninitialized_state("Value is not initialized.");
        case state::error:
            std::rethrow_exception(_error);
        case state::value:
            return;
        }
    }

private:
    std::exception_ptr _error = nullptr;
    state _state = state::uninitialized;
};

} // namespace coro

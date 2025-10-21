#pragma once

#include "../core/task.hpp"
#include "../core/awaitable.hpp"

#include <variant>

namespace coro {

namespace detail {
template <typename R>
struct TaskOrValueAwaitable;
} // namespace detail

/**
 * Optimized value or task holder for cases when the value of some calculation is already known/cached.
 * Using this class for such scenarios will allow to avoid creating coroutine frame when the result is
 * already known.
 * This class itself can not serve as a coroutine frame wrapper, so no co_await/co_return from functions
 * returning TaskOrValue. However one can return both value and task from TaskOrValue function, and the
 * resulting TaskOrValue can be co_await(ed) in a most optimal way.
 */
template <typename R>
class TaskOrValue {
public:
    TaskOrValue(Task<R>&& task)
        : _data(std::move(task)) {}

    TaskOrValue(const R& r)
        : _data(r) {}

    TaskOrValue(R&& r)
        : _data(std::move(r)) {}

public:
    TaskOrValue(const TaskOrValue&) = delete;
    TaskOrValue& operator=(const TaskOrValue&) = delete;

    TaskOrValue(TaskOrValue&&) = default;
    TaskOrValue& operator=(TaskOrValue&&) = default;

public:
    bool isTask() const {
        return _data.index() == 0;
    }

    bool isValue() const {
        return !isTask();
    }

private:
    Task<R>& task() {
        return std::get<0>(_data);
    }

    R value() {
        if constexpr (std::is_move_constructible_v<R>) {
            return std::move(std::get<1>(_data));
        } else {
            return std::get<1>(_data);
        }
    }

private:
    friend struct detail::TaskOrValueAwaitable<R>;
    std::variant<Task<R>, R> _data;
};

template <>
class TaskOrValue<void> {
public:
    TaskOrValue(Task<void>&& task)
        : _data(std::move(task)) {}

    TaskOrValue()
        : _data(std::monostate {}) {}

public:
    TaskOrValue(const TaskOrValue&) = delete;
    TaskOrValue& operator=(const TaskOrValue&) = delete;

    TaskOrValue(TaskOrValue&&) = default;
    TaskOrValue& operator=(TaskOrValue&&) = default;

public:
    bool isTask() const {
        return _data.index() == 0;
    }

    bool isValue() const {
        return !isTask();
    }

private:
    Task<void>& task() {
        return std::get<0>(_data);
    }

private:
    friend struct detail::TaskOrValueAwaitable<void>;
    std::variant<Task<void>, std::monostate> _data;
};
namespace detail {

template <typename R>
struct TaskOrValueAwaitable {
    TaskOrValueAwaitable(TaskOrValue<R>&& tv) {
        if (tv.isTask()) {
            _awaitable = Awaitable<Task<R>> {std::move(tv.task())};
        } else {
            if constexpr (std::is_same_v<R, void>) {
                _awaitable = ReadyAwaitable<void> {};
            } else {
                _awaitable = ReadyAwaitable<R> {tv.value()};
            }
        }
    }

    bool await_ready() noexcept {
        return !isTask();
    }

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> continuation) noexcept {
        taskAwaitable().await_suspend(continuation);
    }

    R await_resume() {
        if (isTask()) {
            return taskAwaitable().await_resume();
        } else {
            return readyAwaitable().await_resume();
        }
    }

private:
    bool isTask() const {
        return _awaitable.index() == 1;
    }

    Awaitable<Task<R>>& taskAwaitable() {
        return std::get<1>(_awaitable);
    }

    ReadyAwaitable<R>& readyAwaitable() {
        return std::get<2>(_awaitable);
    }

private:
    std::variant<std::monostate, Awaitable<Task<R>>, ReadyAwaitable<R>> _awaitable;
};

} // namespace detail

template <typename R>
struct await_ready_trait<TaskOrValue<R>> {
    static detail::TaskOrValueAwaitable<R> await_transform(const PromiseBase&, TaskOrValue<R>&& awaitable) {
        return {std::move(awaitable)};
    }
};

} // namespace coro

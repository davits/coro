#pragma once

#include <mutex>
#include <queue>
#include <optional>

namespace coro::detail {

template <typename T>
class Queue {
public:
    Queue() = default;

    void push(T data) {
        _queue.push(std::move(data));
    }

    std::optional<T> pop() {
        if (_queue.empty()) return std::nullopt;
        std::optional result {std::move(_queue.front())};
        _queue.pop();
        return result;
    }

    bool empty() const {
        return _queue.empty();
    }

    T& front() {
        return _queue.front();
    }

    const T& front() const {
        return _queue.front();
    }

private:
    std::queue<T> _queue;
};

/**
 * Thread safe wrapper for the std::queue.
 */
template <typename T>
class SafeQueue : private Queue<T> {
public:
    SafeQueue() = default;

    void push(T data) {
        std::scoped_lock lock {_mutex};
        Queue<T>::push(std::move(data));
    }

    std::optional<T> pop() {
        std::scoped_lock lock {_mutex};
        return Queue<T>::pop();
    }

    bool empty() const {
        std::scoped_lock lock {_mutex};
        return Queue<T>::empty();
    }

private:
    mutable std::mutex _mutex;
};

} // namespace coro::detail

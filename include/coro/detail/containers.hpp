#pragma once

#include <optional>
#include <mutex>
#include <queue>
#include <stack>

namespace coro::detail {

template <typename T>
class Deque {
public:
    Deque() = default;

    void pushFront(T data) {
        _deque.push_front(std::move(data));
    }

    std::optional<T> popFront() {
        if (_deque.empty()) return std::nullopt;
        std::optional<T> result {std::move(_deque.front())};
        _deque.pop_front();
        return result;
    }

    void pushBack(T data) {
        _deque.push_back(std::move(data));
    }

    std::optional<T> popBack() {
        if (_deque.empty()) return std::nullopt;
        std::optional<T> result {std::move(_deque.back())};
        _deque.pop_back();
        return result;
    }

    bool empty() const {
        return _deque.empty();
    }

private:
    std::deque<T> _deque;
};

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

template <typename T>
class Stack {
public:
    Stack() = default;

    void push(T data) {
        _stack.push(std::move(data));
    }

    std::optional<T> pop() {
        if (_stack.empty()) return std::nullopt;
        std::optional result {std::move(_stack.top())};
        _stack.pop();
        return result;
    }

    bool empty() const {
        return _stack.empty();
    }

    T& top() {
        return _stack.front();
    }

    const T& top() const {
        return _stack.front();
    }

private:
    std::stack<T> _stack;
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

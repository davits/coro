#pragma once

#include "../core/executor.hpp"
#include "../core/promise_base.hpp"
#include "../core/traits.hpp"
#include "../detail/containers.hpp"

#include <coroutine>
#include <queue>
#include <mutex>

namespace coro {

template <typename T>
class PipeDataReader;

template <typename T>
class PipeDataAwaitable;

/**
 * @brief Asynchronous pipe providing means to implement multiple producer multiple consumer pattern.
 * Unlike standard posix pipe, user read writes objects of type T rather then raw bytes.
 * There is no limitation on pipe size and write() is non blocking. Read is asynchronous so in order to read one has to
 * co_await for it.
 */
template <typename T>
class Pipe {
public:
    Pipe() {}

public:
    void write(T data);

    PipeDataReader<T> read() {
        return PipeDataReader<T> {*this};
    }

private:
    friend PipeDataAwaitable<T>;

    std::optional<T> addReader(PipeDataAwaitable<T>* reader) {
        std::scoped_lock lock {_mutex};
        auto data = _data.pop();
        if (data) {
            return data;
        } else {
            _readers.push(reader);
            return std::nullopt;
        }
    }

    std::optional<T> readData() {
        std::scoped_lock lock {_mutex};
        return _data.pop();
    }

private:
    detail::Queue<T> _data;
    detail::Queue<PipeDataAwaitable<T>*> _readers;
    std::mutex _mutex;
};

template <typename T>
class PipeDataAwaitable {
public:
    PipeDataAwaitable(PipeDataReader<T>&& reader, Executor::Ref executor)
        : _pipe(reader._pipe)
        , _executor(std::move(executor)) {}

    PipeDataAwaitable& operator co_await() {
        _data = _pipe.readData();
        return *this;
    }

    bool await_ready() noexcept {
        return _data.has_value();
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> continuation) noexcept {
        _continuation = CoroHandle::fromTypedHandle(continuation);
        auto data = _pipe.addReader(this);
        if (data) {
            _data = std::move(data);
            return false;
        }
        return true;
    }

    T await_resume() {
        _continuation.throwIfStopped();
        return std::move(_data).value();
    }

private:
    friend Pipe<T>;
    void dataAvailable(T&& data) {
        _data = std::move(data);
        _executor->schedule(_continuation);
    }

private:
    Pipe<T>& _pipe;
    std::optional<T> _data;
    Executor::Ref _executor;
    CoroHandle _continuation;
};

template <typename T>
void Pipe<T>::write(T data) {
    std::scoped_lock lock {_mutex};
    auto reader = _readers.pop().value_or(nullptr);
    if (reader) {
        reader->dataAvailable(std::move(data));
    } else {
        _data.push(std::move(data));
    }
}

template <typename T>
class PipeDataReader {
public:
    PipeDataReader(Pipe<T>& pipe)
        : _pipe(pipe) {}

private:
    friend class PipeDataAwaitable<T>;
    Pipe<T>& _pipe;
};

template <typename T>
struct await_ready_trait<PipeDataReader<T>> {
    static PipeDataAwaitable<T> await_transform(const PromiseBase& promise, PipeDataReader<T>&& awaitable) {
        return PipeDataAwaitable<T> {std::move(awaitable), promise.executor};
    }
};

} // namespace coro

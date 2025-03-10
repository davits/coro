#pragma once

#include "executor.hpp"
#include "traits.hpp"
#include "detail/containers.hpp"

#include <coroutine>
#include <queue>
#include <mutex>

namespace coro {

template <typename T>
class AsyncPipe;

template <typename T>
class PipeDataReader;

template <typename T>
class PipeDataAwaitable {
public:
    PipeDataAwaitable(PipeDataReader<T>& other, ExecutorRef executor);

    PipeDataAwaitable& operator co_await();

    bool await_ready() noexcept;

    bool await_suspend(std::coroutine_handle<> continuation) noexcept;

    T await_resume() noexcept;

private:
    friend AsyncPipe<T>;
    void dataAvailable(T&& data);

private:
    AsyncPipe<T>& _pipe;
    std::optional<T> _data;
    ExecutorRef _executor;
    std::coroutine_handle<> _continuation;
};

template <typename T>
class PipeDataReader {
public:
    PipeDataReader(AsyncPipe<T>& pipe)
        : _pipe(pipe) {}

private:
    friend class PipeDataAwaitable<T>;
    AsyncPipe<T>& _pipe;
};

template <typename T>
class AsyncPipe {
    struct Tag {};

public:
    AsyncPipe(Tag) {}

    using Ref = std::shared_ptr<AsyncPipe>;
    Ref create() {
        return std::make_shared<AsyncPipe>(Tag {});
    }

public:
    void write(T data) {
        std::scoped_lock lock {_mutex};
        auto reader = _readers.pop();
        if (reader) {
            reader->dataAvailable(std::move(data));
        } else {
            _data.push(std::move(data));
        }
    }

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
PipeDataAwaitable<T>::PipeDataAwaitable(PipeDataReader<T>& reader, ExecutorRef executor)
    : _pipe(reader._pipe)
    , _executor(std::move(executor)) {}

template <typename T>
PipeDataAwaitable<T>& PipeDataAwaitable<T>::operator co_await() {
    _data = _pipe.readData();
    return *this;
}

template <typename T>
bool PipeDataAwaitable<T>::await_ready() noexcept {
    return _data.has_value();
}

template <typename T>
bool PipeDataAwaitable<T>::await_suspend(std::coroutine_handle<> continuation) noexcept {
    _continuation = continuation;
    auto data = _pipe.addReader(this);
    if (data) {
        _data = std::move(data);
        return false;
    }
    return true;
}

template <typename T>
T PipeDataAwaitable<T>::await_resume() noexcept {
    return std::move(_data).value();
}

template <typename T>
void PipeDataAwaitable<T>::dataAvailable(T&& data) {
    _data = std::move(data);
    _executor->schedule(_continuation);
}

template <typename T>
struct await_ready_trait<PipeDataReader<T>> {
    static PipeDataAwaitable<T> await_transform(const ExecutorRef& executor, PipeDataReader<T>&& awaitable) {
        return PipeDataAwaitable<T> {awaitable, executor};
    }
};

} // namespace coro
